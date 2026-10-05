#include "es7210.hpp"
#include "board.hpp"
#include "esp_check.h"
#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

namespace {
constexpr char kTag[] = "es7210";
constexpr uint32_t kI2cFrequency = 100000;
}

bool Es7210Pair::read(uint8_t address, uint8_t reg, uint8_t& value) const {
    return bus_.readRegister(address, reg, &value, 1, kI2cFrequency);
}

esp_err_t Es7210Pair::write(uint8_t address, uint8_t reg, uint8_t value) const {
    if (!bus_.writeRegister8(address, reg, value, kI2cFrequency)) {
        ESP_LOGE(kTag, "I2C write failed: address=%02x register=%02x", address, reg);
        return ESP_FAIL;
    }
    return ESP_OK;
}

esp_err_t Es7210Pair::initialize(uint8_t address, bool last, unsigned gain_db) {
    uint8_t id1 = 0, id0 = 0, revision = 0;
    if (!read(address, 0x3d, id1) || !read(address, 0x3e, id0) ||
        !read(address, 0x3f, revision)) return ESP_ERR_NOT_FOUND;
    ESP_LOGI(kTag, "address=%02x id=%02x%02x revision=%02x", address, id1, id0, revision);
    if (id1 != 0x72 || id0 != 0x10) return ESP_ERR_INVALID_RESPONSE;
    ESP_RETURN_ON_ERROR(write(address, 0x00, 0xff), kTag, "reset");
    ESP_RETURN_ON_ERROR(write(address, 0x00, 0x41), kTag, "release reset");
    ESP_RETURN_ON_ERROR(write(address, 0x01, 0x3f), kTag, "clock preparation");
    struct Register { uint8_t reg, value; };
#ifdef CONFIG_BASS_TDM_PHILIPS
    constexpr uint8_t interface_format = 0x60; // 16-bit Philips, one-bit delay.
#else
    constexpr uint8_t interface_format = 0x63; // 16-bit DSP A, SP_LRP=0.
#endif
    const Register setup[] = {
        {0x09, 0x30}, {0x0a, 0x30},
        {0x02, 0xc1}, // MCLK 256fs, ADC clock x2; 16 kHz / 4.096 MHz.
        {0x03, 0x02}, {0x04, 0x01}, {0x05, 0x00}, {0x07, 0x20},
        {0x08, 0x44}, // Eight channels, slave, EQ bypass.
        {0x0c, 0x00}, // Interrupts disabled (INT pins are NC).
        {0x10, 0x00}, // Analog inputs, DMIC disabled.
        {0x11, interface_format},
        {0x12, static_cast<uint8_t>(last ? 0x03 : 0x07)}, // Cascade, last-stage flag.
        {0x16, 0x00}, // ALC off; preserve attack amplitude.
        {0x20, 0x0d}, {0x21, 0x0d}, {0x22, 0x0d}, {0x23, 0x0d}, // Slow HPF, auto offset.
        {0x40, 0x43}, {0x41, 0x70}, {0x42, 0x70},
        {0x43, static_cast<uint8_t>(0x10 | (gain_db / 3))},
        {0x44, static_cast<uint8_t>(0x10 | (gain_db / 3))},
        {0x45, static_cast<uint8_t>(0x10 | (gain_db / 3))},
        {0x46, static_cast<uint8_t>(0x10 | (gain_db / 3))},
        {0x47, 0x08}, {0x48, 0x08}, {0x49, 0x08}, {0x4a, 0x08},
        {0x4b, 0x40}, {0x4c, 0x40}, // ADC/PGA on; unused MICBIAS outputs off.
        {0x06, static_cast<uint8_t>(last ? 0x02 : 0x00)},
        {0x01, 0x20}, // Slave-mode clock paths on; master clock generator off.
    };
    for (const auto& entry : setup)
        ESP_RETURN_ON_ERROR(write(address, entry.reg, entry.value), kTag, "configure");
    ESP_RETURN_ON_ERROR(write(address, 0x00, 0x71), kTag, "ADC reset");
    ESP_RETURN_ON_ERROR(write(address, 0x00, 0x41), kTag, "ADC start");
    // Check static configuration before any MIDI notes can be emitted.
    for (const auto& entry : setup) {
        uint8_t actual = 0;
        if (!read(address, entry.reg, actual) || actual != entry.value) {
            ESP_LOGE(kTag, "readback failed: %02x:%02x expected=%02x actual=%02x",
                     address, entry.reg, entry.value, actual);
            return ESP_ERR_INVALID_RESPONSE;
        }
    }
    return ESP_OK;
}

esp_err_t Es7210Pair::begin(unsigned gain_db) {
    if (gain_db > 30) return ESP_ERR_INVALID_ARG;
    if (!bus_.begin(I2C_NUM_1, board::kSda, board::kScl)) return ESP_FAIL;
    bus_ready_ = true;
    ESP_RETURN_ON_ERROR(initialize(board::kAdcA, false, gain_db), kTag, "U3");
    ESP_RETURN_ON_ERROR(initialize(board::kAdcB, true, gain_db), kTag, "U4");
    vTaskDelay(pdMS_TO_TICKS(120));
    for (uint8_t address : {board::kAdcA, board::kAdcB}) {
        uint8_t state = 0;
        if (!read(address, 0x0b, state) || (state & 3) != 2) {
            ESP_LOGE(kTag, "ADC state is not normal: address=%02x state=%02x", address, state);
            return ESP_ERR_INVALID_STATE;
        }
    }
    return ESP_OK;
}

void Es7210Pair::dump() const {
    for (uint8_t address : {board::kAdcA, board::kAdcB}) {
        for (uint8_t reg = 0; reg <= 0x4c; ++reg) {
            uint8_t value = 0;
            if (!read(address, reg, value)) { ESP_LOGE(kTag, "read failed %02x:%02x", address, reg); break; }
            ESP_LOGI(kTag, "%02x:%02x=%02x", address, reg, value);
        }
    }
}
