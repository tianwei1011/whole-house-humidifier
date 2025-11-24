set(SKIP_PACKAGE_VERSION_CHECK 1)
set(IDF_VERSION_CHECK 0)

idf_component_register(
    SRCS
        "dummy.cpp"  # Ensure this file exists
    INCLUDE_DIRS
        "cores/esp32"
        "variants/esp32"
    REQUIRES
        driver
        esp_timer
        freertos
        nvs_flash
)

# Add compile definitions globally for this component
add_compile_definitions(
    ESP32
    ARDUINO=200
)