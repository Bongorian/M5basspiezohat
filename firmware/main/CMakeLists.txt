idf_component_register(
    SRCS "app_main.cpp" "es7210.cpp" "audio_input.cpp" "usb_device.cpp" "usb_host_midi.cpp" "usb_role.cpp"
    INCLUDE_DIRS "."
    REQUIRES bass_core m5unified m5gfx esp_tinyusb esp_driver_i2s esp_timer esp_system usb nvs_flash)
target_compile_features(${COMPONENT_LIB} PUBLIC cxx_std_17)
