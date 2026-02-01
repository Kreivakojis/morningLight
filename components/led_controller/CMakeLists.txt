idf_component_register(
    SRCS "led_controller.c" "color_utils.c"
    INCLUDE_DIRS "include"
    REQUIRES esp_driver_ledc esp_event config_manager
)
