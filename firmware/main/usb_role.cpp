#include "usb_role.hpp"
#include "nvs.h"
#include "nvs_flash.h"
#include "esp_log.h"

namespace usb_role {
bool load() {
    const esp_err_t error = nvs_flash_init();
    if (error != ESP_OK) { ESP_LOGW("usb_role", "NVS unavailable: %s; using device mode", esp_err_to_name(error)); return false; }
    nvs_handle_t handle;
    if (nvs_open("bass", NVS_READONLY, &handle) != ESP_OK) return false;
    uint8_t value = 0;
    nvs_get_u8(handle, "usb_host", &value);
    nvs_close(handle);
    return value == 1;
}
esp_err_t save(bool host) {
    nvs_handle_t handle;
    esp_err_t error = nvs_open("bass", NVS_READWRITE, &handle);
    if (error != ESP_OK) return error;
    error = nvs_set_u8(handle, "usb_host", host ? 1 : 0);
    if (error == ESP_OK) error = nvs_commit(handle);
    nvs_close(handle);
    return error;
}
}
