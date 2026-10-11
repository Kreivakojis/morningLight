idf_component_register(
    SRCS "power_monitor.c"
    INCLUDE_DIRS "include"
    REQUIRES esp_adc esp_timer freertos config_manager adc_shared
)
