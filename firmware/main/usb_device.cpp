#include "usb_device.hpp"
#include "board.hpp"
#include <cstdarg>
#include <algorithm>
#include <cstdio>
#include <cstring>
#include "esp_mac.h"
#include "esp_timer.h"
#include "freertos/task.h"
#include "tinyusb.h"
#include "tusb_cdc_acm.h"
#include "tusb.h"

namespace {
constexpr uint16_t kLength = TUD_CONFIG_DESC_LEN + TUD_CDC_DESC_LEN + TUD_MIDI_DESC_LEN;
const uint8_t kConfiguration[] = {
    TUD_CONFIG_DESCRIPTOR(1, 4, 0, kLength, 0, 500),
    TUD_CDC_DESCRIPTOR(0, 4, 0x81, 8, 0x02, 0x82, 64),
    TUD_MIDI_DESCRIPTOR(2, 5, 0x03, 0x83, 64),
};
// Espressif's development VID/PID. Override for a separately identified product.
const tusb_desc_device_t kDevice = {
    .bLength = sizeof(tusb_desc_device_t), .bDescriptorType = TUSB_DESC_DEVICE,
    .bcdUSB = 0x0200, .bDeviceClass = TUSB_CLASS_MISC,
    .bDeviceSubClass = MISC_SUBCLASS_COMMON, .bDeviceProtocol = MISC_PROTOCOL_IAD,
    .bMaxPacketSize0 = 64, .idVendor = 0x303a, .idProduct = 0x4001,
    .bcdDevice = 0x0100, .iManufacturer = 1, .iProduct = 2, .iSerialNumber = 3,
    .bNumConfigurations = 1,
};
char serial_number[13];
const char* kStrings[] = {"\x09\x04", "M5basspiezohat", "M5basspiezohat R1",
                          serial_number, "Diagnostics", "Bass MIDI"};
}

esp_err_t UsbDevice::begin(Handler handler, void* context, bool host_mode) {
    host_mode_ = host_mode;
    handler_ = handler;
    context_ = context;
    queue_ = xQueueCreate(128, sizeof(Event));
    if (!queue_) return ESP_ERR_NO_MEM;
    if (host_mode_) {
        const esp_err_t error = host_.begin();
        if (error != ESP_OK) return error;
        return xTaskCreatePinnedToCore(task, "bass_usb_host", 8192, this, 6, nullptr, 0) == pdPASS
            ? ESP_OK : ESP_ERR_NO_MEM;
    }
    uint8_t mac[6];
    ESP_ERROR_CHECK(esp_efuse_mac_get_default(mac));
    std::snprintf(serial_number, sizeof(serial_number), "%02X%02X%02X%02X%02X%02X",
                  mac[0], mac[1], mac[2], mac[3], mac[4], mac[5]);
    tinyusb_config_t config{};
    config.device_descriptor = &kDevice;
    config.string_descriptor = kStrings;
    config.string_descriptor_count = sizeof(kStrings) / sizeof(kStrings[0]);
    config.external_phy = false;
    config.configuration_descriptor = kConfiguration;
    const esp_err_t result = tinyusb_driver_install(&config);
    if (result != ESP_OK) return result;
    tinyusb_config_cdcacm_t cdc{};
    cdc.usb_dev = TINYUSB_USBDEV_0;
    cdc.cdc_port = TINYUSB_CDC_ACM_0;
    const esp_err_t cdc_result = tusb_cdc_acm_init(&cdc);
    if (cdc_result != ESP_OK) return cdc_result;
    return xTaskCreatePinnedToCore(task, "bass_usb", 8192, this, 6, nullptr, 0) == pdPASS
        ? ESP_OK : ESP_ERR_NO_MEM;
}

bool UsbDevice::send(bass::MidiMessage message) {
    if (!ready()) return false;
    const Event event{message, epoch()};
    if (xQueueSend(queue_, &event, 0) == pdTRUE) return true;
    dropped_.fetch_add(1);
    panic();
    return false;
}

void UsbDevice::panic() {
    ready_.store(false);
    epoch_.fetch_add(1);
    panic_pending_.store(true);
}

void UsbDevice::testNote() {
    panic();
    test_requested_ = true;
}

void UsbDevice::print(const char* format, ...) {
    if (host_mode_) return; // No CDC interface while the port is the USB host.
    char text[512];
    va_list args;
    va_start(args, format);
    const int formatted = std::vsnprintf(text, sizeof(text), format, args);
    va_end(args);
    if (formatted <= 0) return;
    const size_t length = std::min<size_t>(formatted, sizeof(text) - 1);
    const size_t used = (console_write_ + sizeof(console_) - console_read_) % sizeof(console_);
    if (length >= sizeof(console_) - used) { ++console_drops_; return; }
    for (size_t i = 0; i < length; ++i) {
        console_[console_write_] = text[i];
        console_write_ = (console_write_ + 1) % sizeof(console_);
    }
}

bool UsbDevice::writePacket(const std::array<uint8_t, 4>& packet) {
    return host_mode_ ? host_.write(packet) : tud_midi_packet_write(packet.data());
}

void UsbDevice::service() {
    if (host_mode_) host_.service();
    const bool mounted = host_mode_ ? host_.mounted() : tud_midi_mounted() && !tud_suspended();
    if (mounted != mounted_) {
        mounted_ = mounted;
        panic();
    }
    if (panic_pending_.exchange(false)) {
        xQueueReset(queue_);
        if (host_mode_) host_.discardPending();
        pending_ = false;
        test_active_ = false;
        reset_step_ = 0;
        resetting_ = true;
    }
    if (mounted_) {
        if (resetting_) {
            // Clear only the configured channels. Bend RPN is optional.
            constexpr uint8_t controls[][2] = {{120, 0}, {123, 0}, {101, 0}, {100, 0},
                                                {6, 2}, {38, 0}, {101, 127}, {100, 127}};
#ifdef CONFIG_BASS_PITCH_BEND
            constexpr size_t steps = 9;
#else
            constexpr size_t steps = 3;
#endif
            const size_t channel_count = std::min<size_t>(16 - board::kMidiBase,
                board::kSeparateChannels ? CONFIG_BASS_STRING_COUNT : 1);
            for (int batch = 0; batch < 8 && reset_step_ < channel_count * steps; ++batch) {
                const uint8_t channel = board::kMidiBase + reset_step_ / steps;
                const size_t step = reset_step_ % steps;
                const bass::MidiMessage message = step == steps - 1
                    ? bass::MidiMessage{static_cast<uint8_t>(0xe0 | channel), 0, 64}
                    : bass::MidiMessage{static_cast<uint8_t>(0xb0 | channel), controls[step][0], controls[step][1]};
                const auto packet = message.usbPacket();
                if (!writePacket(packet)) break;
                ++reset_step_;
            }
            if (reset_step_ == channel_count * steps) { resetting_ = false; ready_.store(true); }
        } else if (ready()) {
            if (test_requested_.exchange(false)) {
                test_active_ = send({static_cast<uint8_t>(0x90 | board::kMidiBase), 36, 80});
                test_off_time_ = esp_timer_get_time() + 300000;
            }
            if (test_active_ && esp_timer_get_time() >= test_off_time_) {
                if (!send({static_cast<uint8_t>(0x80 | board::kMidiBase), 36, 0})) panic();
                test_active_ = false;
            }
            for (int batch = 0; batch < 16; ++batch) {
                if (!pending_) pending_ = xQueueReceive(queue_, &pending_event_, 0) == pdTRUE;
                if (!pending_) break;
                if (pending_event_.epoch != epoch()) { pending_ = false; continue; }
                const auto packet = pending_event_.message.usbPacket();
                if (!writePacket(packet)) break;
                pending_ = false;
            }
        }
        if (!host_mode_) {
            uint8_t discarded[4];
            while (tud_midi_available()) if (!tud_midi_packet_read(discarded)) break;
        }
    }
    if (host_mode_) { host_.flush(); return; }
    while (tud_cdc_available()) {
        const char ch = static_cast<char>(tud_cdc_read_char());
        if (ch == '\r' || ch == '\n') {
            if (command_overflow_) print("ERR command too long\r\n");
            else if (command_length_) {
                command_[command_length_] = 0;
                if (handler_) handler_(command_, *this, context_);
            }
            command_length_ = 0;
            command_overflow_ = false;
        } else if (ch == '\b' || ch == 127) {
            if (command_length_ && !command_overflow_) --command_length_;
        } else if (ch >= 32 && ch < 127) {
            if (command_length_ + 1 < sizeof(command_) && !command_overflow_) command_[command_length_++] = ch;
            else command_overflow_ = true;
        }
    }
    if (tud_cdc_connected()) {
        while (console_read_ != console_write_ && tud_cdc_write_available()) {
            const size_t contiguous = console_write_ > console_read_
                ? console_write_ - console_read_ : sizeof(console_) - console_read_;
            const uint32_t written = tud_cdc_write(console_ + console_read_,
                std::min<size_t>(contiguous, tud_cdc_write_available()));
            if (!written) break;
            console_read_ = (console_read_ + written) % sizeof(console_);
        }
        tud_cdc_write_flush();
    }
}

void UsbDevice::task(void* context) {
    auto& device = *static_cast<UsbDevice*>(context);
    for (;;) { device.service(); vTaskDelay(pdMS_TO_TICKS(1)); }
}
