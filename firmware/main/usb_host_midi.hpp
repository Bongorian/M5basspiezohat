#pragma once
#include <array>
#include <atomic>
#include "esp_err.h"
#include "usb/usb_host.h"
#include "midi_descriptor.hpp"
#include "midi_packet_batch.hpp"

// All client APIs and callbacks run in the transport worker; the daemon is separate.
class UsbHostMidi {
public:
    esp_err_t begin();
    void service();
    bool mounted() const { return device_ && !closing_ && !fault_ && endpoint_.address; }
    bool write(const std::array<uint8_t, 4>& packet);
    void flush();
    void discardPending() { batch_.clear(); }
    uint32_t errors() const { return errors_.load(); }
private:
    static void daemon(void*);
    static void event(const usb_host_client_event_msg_t*, void*);
    static void complete(usb_transfer_t*);
    void open(uint8_t address);
    void close();
    usb_host_client_handle_t client_ = nullptr;
    usb_device_handle_t device_ = nullptr;
    usb_transfer_t* transfer_ = nullptr;
    bass::MidiEndpoint endpoint_{};
    bass::MidiPacketBatch batch_;
    uint8_t new_address_ = 0;
    bool registered_ = false, closing_ = false, busy_ = false, fault_ = false, cancel_sent_ = false;
    int64_t submitted_at_ = 0;
    std::atomic<uint32_t> errors_{0};
};
