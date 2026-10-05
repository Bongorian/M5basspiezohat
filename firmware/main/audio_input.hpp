#pragma once
#include <array>
#include "board.hpp"
#include "esp_attr.h"
#include "freertos/FreeRTOS.h"
#include "driver/i2s_tdm.h"

struct AudioBlock {
    std::array<int16_t, board::kBlockSamples> samples{};
    uint32_t sequence = 0;
    bool discontinuity = false;
};

class AudioInput {
public:
    esp_err_t begin();
    esp_err_t restartCapture();
    esp_err_t read(AudioBlock& block);
    uint32_t overflows() const;
private:
    static bool IRAM_ATTR overflow(i2s_chan_handle_t, i2s_event_data_t*, void*);
    i2s_chan_handle_t rx_ = nullptr;
    mutable portMUX_TYPE overflow_mux_ = portMUX_INITIALIZER_UNLOCKED;
    uint32_t overflows_ = 0;
    uint32_t previous_overflows_ = 0;
    uint32_t sequence_ = 0;
};
