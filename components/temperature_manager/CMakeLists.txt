idf_component_register(
    SRCS "temperature_manager.c"
    INCLUDE_DIRS "include"
    REQUIRES esp_adc freertos
)
