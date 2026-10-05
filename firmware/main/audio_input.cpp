#include "audio_input.hpp"
#include "esp_check.h"

namespace { constexpr char kTag[] = "audio"; }

bool IRAM_ATTR AudioInput::overflow(i2s_chan_handle_t, i2s_event_data_t*, void* context) {
    auto& self = *static_cast<AudioInput*>(context);
    portENTER_CRITICAL_ISR(&self.overflow_mux_);
    ++self.overflows_;
    portEXIT_CRITICAL_ISR(&self.overflow_mux_);
    return false;
}

esp_err_t AudioInput::begin() {
    i2s_chan_config_t channel = I2S_CHANNEL_DEFAULT_CONFIG(I2S_NUM_0, I2S_ROLE_MASTER);
    channel.dma_desc_num = 8;
    channel.dma_frame_num = board::kBlockFrames;
    channel.intr_priority = 2;
    ESP_RETURN_ON_ERROR(i2s_new_channel(&channel, nullptr, &rx_), kTag, "new RX");
    i2s_tdm_config_t tdm{};
    tdm.clk_cfg = I2S_TDM_CLK_DEFAULT_CONFIG(bass::kInputRate);
    tdm.clk_cfg.mclk_multiple = I2S_MCLK_MULTIPLE_256;
    const auto mask = static_cast<i2s_tdm_slot_mask_t>(0xff);
#ifdef CONFIG_BASS_TDM_PHILIPS
    tdm.slot_cfg = I2S_TDM_PHILIPS_SLOT_DEFAULT_CONFIG(I2S_DATA_BIT_WIDTH_16BIT, I2S_SLOT_MODE_STEREO, mask);
#else
    tdm.slot_cfg = I2S_TDM_PCM_SHORT_SLOT_DEFAULT_CONFIG(I2S_DATA_BIT_WIDTH_16BIT, I2S_SLOT_MODE_STEREO, mask);
#endif
    tdm.slot_cfg.slot_bit_width = I2S_SLOT_BIT_WIDTH_16BIT;
    tdm.slot_cfg.total_slot = bass::kSlots;
    tdm.gpio_cfg.mclk = static_cast<gpio_num_t>(board::kMclk);
    tdm.gpio_cfg.bclk = static_cast<gpio_num_t>(board::kBclk);
    tdm.gpio_cfg.ws = static_cast<gpio_num_t>(board::kWs);
    tdm.gpio_cfg.dout = I2S_GPIO_UNUSED;
    tdm.gpio_cfg.din = static_cast<gpio_num_t>(board::kData);
    ESP_RETURN_ON_ERROR(i2s_channel_init_tdm_mode(rx_, &tdm), kTag, "TDM mode");
    i2s_event_callbacks_t callbacks{};
    callbacks.on_recv_q_ovf = overflow;
    ESP_RETURN_ON_ERROR(i2s_channel_register_event_callback(rx_, &callbacks, this), kTag, "overflow callback");
    return i2s_channel_enable(rx_); // Clocks run before the ADC state machine starts.
}

esp_err_t AudioInput::read(AudioBlock& block) {
    size_t bytes = 0;
    const esp_err_t result = i2s_channel_read(rx_, block.samples.data(),
                                            sizeof(block.samples), &bytes, 50);
    if (result != ESP_OK || bytes != sizeof(block.samples)) {
        previous_overflows_ = UINT32_MAX; // Force a discontinuity on the next good block.
        return result == ESP_OK ? ESP_ERR_INVALID_SIZE : result;
    }
    block.sequence = sequence_++;
    const uint32_t overflows = this->overflows();
    block.discontinuity = overflows != previous_overflows_;
    previous_overflows_ = overflows;
    return ESP_OK;
}

esp_err_t AudioInput::restartCapture() {
    ESP_RETURN_ON_ERROR(i2s_channel_disable(rx_), kTag, "disable RX");
    portENTER_CRITICAL(&overflow_mux_);
    overflows_ = 0;
    portEXIT_CRITICAL(&overflow_mux_);
    previous_overflows_ = sequence_ = 0;
    return i2s_channel_enable(rx_);
}

uint32_t AudioInput::overflows() const {
    portENTER_CRITICAL(&overflow_mux_);
    const uint32_t value = overflows_;
    portEXIT_CRITICAL(&overflow_mux_);
    return value;
}
