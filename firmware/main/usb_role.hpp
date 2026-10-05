#pragma once
#include "esp_err.h"
namespace usb_role {
bool load();
esp_err_t save(bool host);
}
