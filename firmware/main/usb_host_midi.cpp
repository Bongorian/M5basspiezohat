#include "usb_host_midi.hpp"
#include <cstring>
#include "esp_log.h"
#include "esp_timer.h"
#include "freertos/task.h"

namespace { constexpr char tag[] = "midi_host"; }

esp_err_t UsbHostMidi::begin() {
    usb_host_config_t cfg{};
    cfg.intr_flags = ESP_INTR_FLAG_LEVEL1;
    const esp_err_t result = usb_host_install(&cfg);
    if (result != ESP_OK) return result;
    if (xTaskCreatePinnedToCore(daemon, "usb_host_lib", 4096, nullptr, 5, nullptr, 0) != pdPASS) {
        usb_host_uninstall();
        return ESP_ERR_NO_MEM;
    }
    return ESP_OK;
}

void UsbHostMidi::daemon(void*) {
    for (;;) {
        uint32_t flags = 0;
        usb_host_lib_handle_events(pdMS_TO_TICKS(20), &flags);
        if (flags & USB_HOST_LIB_EVENT_FLAGS_NO_CLIENTS) usb_host_device_free_all();
    }
}

void UsbHostMidi::event(const usb_host_client_event_msg_t* event, void* context) {
    auto& self = *static_cast<UsbHostMidi*>(context);
    if (event->event == USB_HOST_CLIENT_EVENT_NEW_DEV && !self.device_)
        self.new_address_ = event->new_dev.address;
    else if (event->event == USB_HOST_CLIENT_EVENT_DEV_GONE && event->dev_gone.dev_hdl == self.device_)
        self.closing_ = true;
}

void UsbHostMidi::complete(usb_transfer_t* transfer) {
    auto& self = *static_cast<UsbHostMidi*>(transfer->context);
    self.busy_ = false;
    if (!self.closing_ && (transfer->status != USB_TRANSFER_STATUS_COMPLETED ||
                          transfer->actual_num_bytes != transfer->num_bytes)) {
        ++self.errors_;
        self.fault_ = true;
        ESP_LOGW(tag, "MIDI OUT failed, status=%d; reconnect the device", transfer->status);
    }
}

void UsbHostMidi::open(uint8_t address) {
    if (usb_host_device_open(client_, address, &device_) != ESP_OK) { ++errors_; return; }
    const usb_config_desc_t* config = nullptr;
    usb_device_info_t info{};
    if (usb_host_device_info(device_, &info) != ESP_OK || info.speed != USB_SPEED_FULL ||
        usb_host_get_active_config_descriptor(device_, &config) != ESP_OK ||
        !bass::findMidiOutput(reinterpret_cast<const uint8_t*>(config), config->wTotalLength, endpoint_)) {
        ESP_LOGW(tag, "No supported full-speed USB MIDI 1.0 OUT interface");
        usb_host_device_close(client_, device_); device_ = nullptr; endpoint_ = {}; return;
    }
    if (usb_host_interface_claim(client_, device_, endpoint_.interface_number, 0) != ESP_OK) {
        ++errors_; usb_host_device_close(client_, device_); device_ = nullptr; endpoint_ = {}; return;
    }
    if (usb_host_transfer_alloc(endpoint_.max_packet, 0, &transfer_) != ESP_OK) {
        ++errors_; closing_ = true; return;
    }
    transfer_->device_handle = device_;
    transfer_->bEndpointAddress = endpoint_.address;
    transfer_->callback = complete;
    transfer_->context = this;
    batch_.clear();
    busy_ = fault_ = cancel_sent_ = false;
    ESP_LOGI(tag, "MIDI OUT interface=%u endpoint=%02x packet=%u cable=0",
             endpoint_.interface_number, endpoint_.address, endpoint_.max_packet);
}

void UsbHostMidi::close() {
    if (busy_) {
        if (!cancel_sent_) {
            usb_host_endpoint_halt(device_, endpoint_.address);
            usb_host_endpoint_flush(device_, endpoint_.address);
            cancel_sent_ = true;
        }
        return; // Completion callback must return before resources are freed.
    }
    if (transfer_) { usb_host_transfer_free(transfer_); transfer_ = nullptr; }
    usb_host_interface_release(client_, device_, endpoint_.interface_number);
    usb_host_device_close(client_, device_);
    device_ = nullptr; endpoint_ = {};
    batch_.clear();
    closing_ = fault_ = cancel_sent_ = false;
}

void UsbHostMidi::service() {
    if (!registered_) {
        usb_host_client_config_t cfg{};
        cfg.max_num_event_msg = 8;
        cfg.async.client_event_callback = event;
        cfg.async.callback_arg = this;
        if (usb_host_client_register(&cfg, &client_) != ESP_OK) { ++errors_; return; }
        registered_ = true;
    }
    usb_host_client_handle_events(client_, 0);
    if (closing_ && device_) close();
    if (new_address_ && !device_) {
        const auto address = new_address_; new_address_ = 0; open(address);
    }
    // ESP-IDF has no bulk-transfer timeout. Cancel an unresponsive device after 1 s.
    if (busy_ && !fault_ && esp_timer_get_time() - submitted_at_ > 1000000) {
        ++errors_; fault_ = true;
        usb_host_endpoint_halt(device_, endpoint_.address);
        usb_host_endpoint_flush(device_, endpoint_.address);
        ESP_LOGW(tag, "MIDI OUT timed out; reconnect the device");
    }
}

bool UsbHostMidi::write(const std::array<uint8_t, 4>& packet) {
    return mounted() && transfer_ && batch_.append(packet, endpoint_.max_packet);
}

void UsbHostMidi::flush() {
    if (!mounted() || busy_ || !transfer_ || !batch_.size()) return;
    std::memcpy(transfer_->data_buffer, batch_.data(), batch_.size());
    transfer_->num_bytes = batch_.size();
    if (usb_host_transfer_submit(transfer_) != ESP_OK) {
        ++errors_; fault_ = true; batch_.clear(); return;
    }
    batch_.clear();
    busy_ = true;
    submitted_at_ = esp_timer_get_time();
}
