Import("env")

# Add Arduino ESP32 support
env.Append(
    CPPDEFINES=[
        ("ARDUINO", 200),
        ("ESP32", 1),
        ("ESP_PLATFORM", 1),
        ("F_CPU", 240000000),
        ("HAVE_CONFIG_H", 1),
        ("MBEDTLS_CONFIG_FILE", '\\"mbedtls/esp_config.h\\"')
    ],
    CPPPATH=[
        "${PROJECTSRC_DIR}",
        "${PROJECT_DIR}/components/arduino/cores/esp32",
        "${PROJECT_DIR}/components/arduino/variants/esp32"
    ]
)