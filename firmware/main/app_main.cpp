#include <algorithm>
#include <atomic>
#include <cmath>
#include <cstdio>
#include <cstring>
#include "M5Unified.h"
#include "audio_input.hpp"
#include "bass_core.hpp"
#include "es7210.hpp"
#include "usb_device.hpp"
#include "usb_role.hpp"
#include "esp_check.h"
#include "esp_heap_caps.h"
#include "esp_timer.h"
#include "freertos/semphr.h"
#include "freertos/task.h"

namespace {
constexpr char kTag[] = "bass";
AudioInput audio;
Es7210Pair adc;
UsbDevice usb;
bass::Engine engine(board::config());
bass::MidiRouter router(board::kSeparateChannels, board::kMidiBase);
std::atomic<bool> muted{true}, adc_ready{false}, initialization_done{false};
std::atomic<uint32_t> queue_drops{0}, read_errors{0}, received_blocks{0};
std::atomic<esp_err_t> hardware_error{ESP_ERR_INVALID_STATE};
std::atomic<int> requested_role{-1};
QueueHandle_t audio_queue, controls;
SemaphoreHandle_t status_mutex;

struct Mapping { std::array<uint8_t, bass::kMaxStrings> slots; };
struct Status {
    std::array<bass::StringStatus, bass::kMaxStrings> strings{};
    std::array<float, bass::kSlots> slot_rms{}, slot_peak{};
    bass::Config config;
    uint64_t frames = 0, process_us = 0;
    uint32_t max_process_us = 0, discontinuities = 0, max_queue_blocks = 0;
};
Status status;

class RoutedSink : public bass::MidiSink {
public:
    bool send(bass::MidiMessage message) override {
        return muted.load() || !usb.ready() ? true : router.send(message, usb);
    }
} sink;

void mute(bool value) {
    muted.store(value);
    usb.panic();
}

Status snapshot() {
    xSemaphoreTake(status_mutex, portMAX_DELAY);
    const Status copy = status;
    xSemaphoreGive(status_mutex);
    return copy;
}

void receive_task(void*) {
    AudioBlock block;
    // Discard startup/ADC settling audio before assigning input blocks to DSP.
    for (int i = 0; i < 64; ++i) audio.read(block);
    for (;;) {
        if (audio.read(block) != ESP_OK) {
            read_errors.fetch_add(1);
            usb.panic();
            vTaskDelay(pdMS_TO_TICKS(10));
            continue;
        }
        received_blocks.fetch_add(1);
        if (xQueueSend(audio_queue, &block, 0) != pdTRUE) {
            queue_drops.fetch_add(1);
            usb.panic();
        }
    }
}

void analysis_task(void*) {
    AudioBlock block;
    uint32_t previous_sequence = 0, previous_epoch = usb.epoch();
    bool first = true, previous_mute = muted.load(), previous_ready = usb.ready();
    std::array<float, bass::kSlots> slot_power{};
    uint64_t process_us = 0;
    uint32_t max_process_us = 0, discontinuities = 0, max_queue_blocks = 0;
    uint32_t publish_counter = 0;
    for (;;) {
        Mapping mapping;
        while (xQueueReceive(controls, &mapping, 0) == pdTRUE) {
            auto config = engine.config();
            config.slots = mapping.slots;
            if (engine.configure(config, sink)) usb.panic();
        }
        const bool now_mute = muted.load(), now_ready = usb.ready();
        const uint32_t epoch = usb.epoch();
        if (now_mute != previous_mute || now_ready != previous_ready || epoch != previous_epoch) {
            engine.reset(sink, false);
            router.reset();
            previous_mute = now_mute;
            previous_ready = now_ready;
            previous_epoch = epoch;
        }
        if (xQueueReceive(audio_queue, &block, pdMS_TO_TICKS(10)) != pdTRUE) continue;
        max_queue_blocks = std::max(max_queue_blocks, static_cast<uint32_t>(uxQueueMessagesWaiting(audio_queue)));
        const bool gap = block.discontinuity || (!first && block.sequence != previous_sequence + 1);
        first = false;
        previous_sequence = block.sequence;
        if (gap) {
            engine.reset(sink, false);
            router.reset();
            usb.panic();
        }
        const int64_t start = esp_timer_get_time();
        if (engine.config().valid() && router.valid(engine.config().string_count) &&
            !engine.process(block.samples.data(), board::kBlockFrames, sink, !now_mute && now_ready)) usb.panic();
        std::array<float, bass::kSlots> peaks{};
        for (size_t slot = 0; slot < bass::kSlots; ++slot) {
            float sum = 0;
            for (size_t frame = 0; frame < board::kBlockFrames; ++frame) {
                const float value = block.samples[frame * bass::kSlots + slot] / 32768.0f;
                sum += value * value;
                peaks[slot] = std::max(peaks[slot], std::fabs(value));
            }
            slot_power[slot] += (sum / board::kBlockFrames - slot_power[slot]) * 0.1f;
        }
        // Include level diagnostics in the processing wall-time measurement.
        const uint32_t elapsed = static_cast<uint32_t>(esp_timer_get_time() - start);
        process_us += elapsed;
        max_process_us = std::max(max_process_us, elapsed);
        if (gap) ++discontinuities;
        // UI/CDC need a 16 ms snapshot, not cross-core locking every 2 ms block.
        if (++publish_counter % 8 && !gap) continue;
        xSemaphoreTake(status_mutex, portMAX_DELAY);
        status.strings = engine.status();
        status.config = engine.config();
        status.frames = engine.frames();
        status.process_us = process_us;
        status.max_process_us = max_process_us;
        status.discontinuities = discontinuities;
        status.max_queue_blocks = max_queue_blocks;
        for (size_t slot = 0; slot < bass::kSlots; ++slot) {
            status.slot_rms[slot] = std::sqrt(slot_power[slot]);
            status.slot_peak[slot] = peaks[slot];
        }
        xSemaphoreGive(status_mutex);
    }
}

void console(const char* command, UsbDevice& device, void*) {
    if (!std::strcmp(command, "help")) {
        device.print("help | status | slots | adc | arm | mute | panic | test | role [host|device]\r\n"
                     "map S1 S2 S3 S4 [S5] : unique slots 0-7; not persistent\r\n"
                     "Buttons: A arm/mute, hold A 1s test, B panic, hold B 2s USB role. Test: note 36, 300 ms.\r\n");
    } else if (!std::strcmp(command, "status")) {
        const auto s = snapshot();
        device.print("adc=%s usb=%s muted=%d error=%s blocks=%lu dma_ovf=%lu queue_drop=%lu "
                     "read_err=%lu midi_drop=%lu gaps=%lu queue_peak=%lu max_dsp_us=%lu dsp_us=%llu heap=%lu cdc_drop=%lu\r\n",
                     adc_ready.load() ? "ok" : "error", usb.ready() ? "ready" : "offline",
                     muted.load(), esp_err_to_name(hardware_error.load()),
                     static_cast<unsigned long>(received_blocks.load()),
                     static_cast<unsigned long>(audio.overflows()), static_cast<unsigned long>(queue_drops.load()),
                     static_cast<unsigned long>(read_errors.load()), static_cast<unsigned long>(usb.dropped()),
                     static_cast<unsigned long>(s.discontinuities), static_cast<unsigned long>(s.max_queue_blocks),
                     static_cast<unsigned long>(s.max_process_us),
                     static_cast<unsigned long long>(s.process_us),
                     static_cast<unsigned long>(heap_caps_get_free_size(MALLOC_CAP_INTERNAL)),
                     static_cast<unsigned long>(usb.consoleDrops()));
        for (size_t i = 0; i < s.config.string_count; ++i)
            device.print("J%u slot=%u ch=%u open=%u rms=%.5f hz=%.3f confidence=%.3f note=%d clips=%lu\r\n",
                         static_cast<unsigned>(i + 1), s.config.slots[i], board::midiChannel(i),
                         s.config.open_notes[i], s.strings[i].rms, s.strings[i].hz,
                         s.strings[i].confidence, s.strings[i].note,
                         static_cast<unsigned long>(s.strings[i].clips));
    } else if (!std::strcmp(command, "slots")) {
        const auto s = snapshot();
        for (size_t i = 0; i < bass::kSlots; ++i)
            device.print("slot=%u rms=%.5f peak=%.5f\r\n", static_cast<unsigned>(i), s.slot_rms[i], s.slot_peak[i]);
    } else if (!std::strcmp(command, "adc")) {
        if (!initialization_done.load() || !adc.busReady()) { device.print("ERR ADC bus not ready\r\n"); return; }
        constexpr uint8_t registers[] = {0x02, 0x08, 0x0b, 0x0c, 0x11, 0x12, 0x20, 0x21,
                                         0x22, 0x23, 0x3d, 0x3e, 0x3f, 0x43, 0x4b};
        for (uint8_t address : {board::kAdcA, board::kAdcB}) {
            for (uint8_t reg : registers) {
                uint8_t value = 0;
                if (adc.read(address, reg, value)) device.print("%02x:%02x=%02x\r\n", address, reg, value);
                else { device.print("ERR I2C %02x:%02x\r\n", address, reg); break; }
            }
        }
    } else if (!std::strcmp(command, "role")) {
        device.print("USB role=%s; switch reboots, host needs external VBUS\r\n", device.hostMode() ? "host" : "device");
    } else if (!std::strcmp(command, "role host") || !std::strcmp(command, "role device")) {
        mute(true);
        requested_role.store(!std::strcmp(command, "role host") ? 1 : 0);
        device.print("OK role change requested; reboot; host needs external VBUS\r\n");
    } else if (!std::strcmp(command, "arm")) {
        if (!adc_ready.load() || !snapshot().config.valid() || !router.valid(CONFIG_BASS_STRING_COUNT))
            device.print("ERR ADC/configuration not ready\r\n");
        else { mute(false); device.print("OK armed; MIDI base=%u separate=%d\r\n", board::midiChannel(0), board::kSeparateChannels); }
    } else if (!std::strcmp(command, "mute") || !std::strcmp(command, "panic")) {
        mute(true);
        device.print("OK muted; all sound off\r\n");
    } else if (!std::strcmp(command, "test")) {
        mute(true);
        device.testNote();
        device.print("OK test queued: channel %u, note 36, 300 ms; bass muted\r\n", board::midiChannel(0));
    } else if (!std::strncmp(command, "map ", 4)) {
        const auto s = snapshot();
        auto config = s.config;
        if (!bass::parseSlotMapping(command + 4, config.string_count, config.slots) || !config.valid()) {
            device.print("ERR expected %u unique slots in range 0-7\r\n", config.string_count); return;
        }
        mute(true);
        const Mapping mapping{config.slots};
        if (xQueueSend(controls, &mapping, 0) != pdTRUE) device.print("ERR command queue full\r\n");
        else device.print("OK mapping queued; verify status, then arm\r\n");
    } else device.print("ERR unknown command; type help\r\n");
}
} // namespace

extern "C" void app_main() {
    status_mutex = xSemaphoreCreateMutex();
    audio_queue = xQueueCreate(16, sizeof(AudioBlock));
    controls = xQueueCreate(8, sizeof(Mapping));
    if (!status_mutex || !audio_queue || !controls) abort();
    status.config = board::config();
    auto cfg = M5.config();
    cfg.internal_mic = cfg.internal_spk = cfg.internal_imu = cfg.internal_rtc = false;
    cfg.external_display_value = 0;
    cfg.output_power = false;
    cfg.fallback_board = m5::board_t::board_M5StickS3;
    M5.begin(cfg);
    M5.Display.setRotation(1);
    M5.Display.setBrightness(80);
    M5.Display.setTextSize(1);
    bool host_mode = usb_role::load();
    // Holding A during reset is an unconditional device-mode recovery path.
    for (int i = 0; i < 15; ++i) { M5.update(); vTaskDelay(pdMS_TO_TICKS(20)); }
    bool recovery_press = M5.BtnA.isPressed();
    if (recovery_press) host_mode = false;
    M5.BtnB.setHoldThresh(2000);
    M5.BtnA.setHoldThresh(1000);
    ESP_ERROR_CHECK(usb.begin(console, nullptr, host_mode));
    if (M5.getBoard() != m5::board_t::board_M5StickS3) {
        hardware_error.store(ESP_ERR_NOT_SUPPORTED);
        ESP_LOGE(kTag, "Only M5StickS3 is supported; Hat power stays off");
    } else {
        M5.Power.setExtOutput(true);
        vTaskDelay(pdMS_TO_TICKS(50));
        esp_err_t error = audio.begin();
        if (error == ESP_OK) error = adc.begin(CONFIG_BASS_GAIN_DB);
        if (error == ESP_OK) error = audio.restartCapture();
        hardware_error.store(error);
        adc_ready.store(error == ESP_OK);
    }
    initialization_done.store(true);
    ESP_ERROR_CHECK(xTaskCreatePinnedToCore(analysis_task, "bass_dsp", 8192, nullptr, 17, nullptr, 1)
                    == pdPASS ? ESP_OK : ESP_ERR_NO_MEM);
    if (adc_ready.load()) {
        ESP_ERROR_CHECK(xTaskCreatePinnedToCore(receive_task, "bass_rx", 4096, nullptr, 22, nullptr, 0)
                        == pdPASS ? ESP_OK : ESP_ERR_NO_MEM);
#ifndef CONFIG_BASS_START_MUTED
        if (board::config().valid() && router.valid(CONFIG_BASS_STRING_COUNT)) mute(false);
#endif
    }
    int64_t next_display = 0;
    int64_t reboot_at = 0;
    int pending_role = -1;
    for (;;) {
        M5.update();
        if (!recovery_press && M5.BtnA.wasClicked() && adc_ready.load() && snapshot().config.valid() && router.valid(CONFIG_BASS_STRING_COUNT))
            mute(!muted.load());
        if (!recovery_press && M5.BtnA.wasHold()) { mute(true); usb.testNote(); }
        if (!M5.BtnA.isPressed()) recovery_press = false;
        if (M5.BtnB.wasClicked()) mute(true);
        if (M5.BtnB.wasHold()) {
            mute(true);
            requested_role.store(usb.hostMode() ? 0 : 1);
        }
        const int64_t now = esp_timer_get_time();
        const int change = requested_role.exchange(-1);
        if (change >= 0) { mute(true); pending_role = change; reboot_at = now + 300000; }
        if (pending_role >= 0 && now >= reboot_at) {
            const esp_err_t result = usb_role::save(pending_role == 1);
            if (result == ESP_OK) esp_restart();
            ESP_LOGE(kTag, "USB role save failed: %s", esp_err_to_name(result));
            usb.print("ERR role save: %s\r\n", esp_err_to_name(result));
            pending_role = -1;
        }
        if (now >= next_display) {
            next_display = now + 200000;
            const auto s = snapshot();
            M5.Display.startWrite();
            M5.Display.fillScreen(0);
            M5.Display.setCursor(0, 0);
            M5.Display.printf("M5basspiezohat R1\n%s | %s %s\nADC %s  DMA %lu DROP %lu\n",
                muted.load() ? "MUTED" : "ARMED", usb.hostMode() ? "HOST" : "DEVICE",
                usb.ready() ? "ready" : "offline",
                adc_ready.load() ? "OK" : esp_err_to_name(hardware_error.load()),
                static_cast<unsigned long>(audio.overflows()), static_cast<unsigned long>(queue_drops.load()));
            for (size_t i = 0; i < s.config.string_count; ++i)
                M5.Display.printf("J%u S%u %6.1fHz N%2d C%.2f\n", static_cast<unsigned>(i + 1),
                    s.config.slots[i], s.strings[i].hz, s.strings[i].note, s.strings[i].confidence);
            M5.Display.printf("A arm/hold test B USB\nDSP %lu us HOST ERR %lu", static_cast<unsigned long>(s.max_process_us),
                             static_cast<unsigned long>(usb.hostErrors()));
            M5.Display.endWrite();
        }
        vTaskDelay(pdMS_TO_TICKS(20));
    }
}
