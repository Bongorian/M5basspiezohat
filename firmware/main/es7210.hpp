#pragma once
#include <cstdint>
#include "M5Unified.h"
#include "esp_err.h"

class Es7210Pair {
public:
    esp_err_t begin(unsigned gain_db);
    bool read(uint8_t address, uint8_t reg, uint8_t& value) const;
    void dump() const;
    bool busReady() const { return bus_ready_; }
private:
    esp_err_t write(uint8_t address, uint8_t reg, uint8_t value) const;
    esp_err_t initialize(uint8_t address, bool last, unsigned gain_db);
    m5::I2C_Class bus_;
    bool bus_ready_ = false;
};
