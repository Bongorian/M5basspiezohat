#pragma once
#include <atomic>
#include <cstdint>
#include "bass_core.hpp"
#include "esp_err.h"
#include "freertos/FreeRTOS.h"
#include "freertos/queue.h"
#include "usb_host_midi.hpp"

class UsbDevice : public bass::MidiSink {
public:
    using Handler = void (*)(const char*, UsbDevice&, void*);
    esp_err_t begin(Handler handler, void* context, bool host_mode = false);
    bool send(bass::MidiMessage message) override;
    void panic();
    void testNote();
    void print(const char* format, ...) __attribute__((format(printf, 2, 3)));
    bool ready() const { return ready_.load(); }
    uint32_t epoch() const { return epoch_.load(); }
    uint32_t dropped() const { return dropped_.load(); }
    uint32_t consoleDrops() const { return console_drops_; }
    bool hostMode() const { return host_mode_; }
    uint32_t hostErrors() const { return host_.errors(); }
private:
    struct Event { bass::MidiMessage message; uint32_t epoch; };
    static void task(void*);
    void service();
    bool writePacket(const std::array<uint8_t, 4>& packet);
    UsbHostMidi host_;
    bool host_mode_ = false;
    Handler handler_ = nullptr;
    void* context_ = nullptr;
    QueueHandle_t queue_ = nullptr;
    std::atomic<bool> ready_{false}, panic_pending_{false};
    std::atomic<uint32_t> epoch_{0}, dropped_{0};
    bool mounted_ = false, pending_ = false, resetting_ = false;
    std::atomic<bool> test_requested_{false};
    bool test_active_ = false;
    uint32_t reset_step_ = 0;
    int64_t test_off_time_ = 0;
    Event pending_event_{};
    char command_[128]{};
    size_t command_length_ = 0;
    bool command_overflow_ = false;
    char console_[4096]{};
    size_t console_read_ = 0, console_write_ = 0;
    uint32_t console_drops_ = 0;
};
