idf_component_register(
    SRCS "mqtt_manager.c"
    INCLUDE_DIRS "include"
    REQUIRES esp_event
    PRIV_REQUIRES config_manager led_controller sunrise_engine animation_engine temperature_manager time_manager espressif__cjson
)
