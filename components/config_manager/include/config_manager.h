#ifndef CONFIG_MANAGER_H
#define CONFIG_MANAGER_H

#include <stdint.h>
#include <stdbool.h>
#include "esp_err.h"
#include "animation_preset.h"

#ifdef __cplusplus
extern "C" {
#endif

#define DARK_MODE_MAX_SCHEDULES 3

/**
 * @brief WiFi credentials storage
 */
typedef struct {
    char ssid[33];
    char password[65];
} wifi_config_stored_t;

/**
 * @brief Single alarm configuration
 */
typedef struct {
    bool enabled;
    uint8_t hour;           // 0-23
    uint8_t minute;         // 0-59
    uint8_t duration_min;   // Sunrise duration in minutes
    uint8_t days_mask;      // Bit mask: bit 0 = Sunday, bit 6 = Saturday
    uint16_t color_temp;    // Color temperature in Kelvin
    uint8_t brightness;     // Max brightness percentage
    int8_t animation_preset; // -1 = classic, 0-4 = animation preset index
    uint8_t cooldown_min;    // 0 = no auto-off, 1-60 = dim-to-off duration
} alarm_config_t;

/**
 * @brief LED type enumeration for config storage
 */
typedef enum {
    CONFIG_LED_TYPE_PWM = 0,
    CONFIG_LED_TYPE_WS2811 = 1,
} config_led_type_t;

/**
 * @brief Dark mode schedule
 */
typedef struct {
    bool enabled;
    uint8_t start_hour;     // 0-23
    uint8_t start_minute;   // 0-59
    uint8_t end_hour;       // 0-23
    uint8_t end_minute;     // 0-59
    uint8_t days_mask;      // Bit mask: bit 0 = Sunday, bit 6 = Saturday
    bool allow_override;    // When true, this schedule doesn't block lights
} dark_mode_schedule_t;

/**
 * @brief MQTT configuration
 */
typedef struct {
    bool enabled;
    char broker_uri[128];   // "mqtt://192.168.1.100:1883"
    char username[33];
    char password[65];
    char topic_prefix[32];  // default "morninglight"
    char device_name[32];   // default "MorningLight" (for HA display)
} mqtt_config_t;

/**
 * @brief Device configuration structure
 */
typedef struct {
    wifi_config_stored_t wifi;
    char timezone[64];
    alarm_config_t alarms[8];
    uint8_t brightness_max;
    uint32_t pwm_frequency;     // PWM frequency in Hz (100-40000)
    uint8_t led_type;           // config_led_type_t: PWM or WS2811
    uint16_t led_count;         // Number of LEDs (WS2811 mode, 1-300)
    bool setup_complete;

    // Animation presets
    animation_preset_t animation_presets[ANIMATION_MAX_PRESETS];
    int8_t active_animation;    // -1 = none, 0-4 = preset index

    // Dark mode schedules (added at end for NVS compatibility)
    dark_mode_schedule_t dark_schedules[DARK_MODE_MAX_SCHEDULES];

    // MQTT configuration (added at end for NVS compatibility)
    mqtt_config_t mqtt;

    // Per-alarm brightness curve and color temp range (added at end for NVS compat)
    uint8_t alarm_curves[8];            // 0=logarithmic(default), 1=inverse_log, 2=linear, 3=sigmoid, 4=exponential
    uint16_t alarm_color_temp_start[8]; // Starting color temp in K, 0 = same as alarm's color_temp (no ramp)

    // Per-alarm display names (added at end for NVS compat)
    char alarm_names[8][16];            // Optional display name, empty string = unnamed

    // Gamma correction (added at end for NVS compat)
    uint8_t gamma_x10;                  // Gamma * 10: 22 = 2.2. Range 10-40 (1.0-4.0). 0 treated as 22

    // Temperature calibration offsets (added at end for NVS compat)
    int8_t temp_offset_ntc_x10;         // NTC offset in 0.1°C steps, range -50..+50 (-5.0..+5.0°C)
    int8_t temp_offset_ds1_x10;         // DS18B20 #1 offset
    int8_t temp_offset_ds2_x10;         // DS18B20 #2 offset

    // External sensor display names (added at end for NVS compat)
    char temp_name_ds1[17];             // Custom name for External 1, empty = "External 1"
    char temp_name_ds2[17];             // Custom name for External 2, empty = "External 2"
} device_config_t;

/**
 * @brief Initialize the configuration manager
 *
 * Loads configuration from NVS or creates defaults.
 *
 * @return ESP_OK on success
 */
esp_err_t config_manager_init(void);

/**
 * @brief Get pointer to current configuration
 *
 * @return Pointer to config structure (do not free)
 */
device_config_t *config_manager_get(void);

/**
 * @brief Save current configuration to NVS
 *
 * @return ESP_OK on success
 */
esp_err_t config_manager_save(void);

/**
 * @brief Reset configuration to factory defaults
 *
 * @return ESP_OK on success
 */
esp_err_t config_manager_factory_reset(void);

/**
 * @brief Set WiFi credentials
 *
 * @param ssid Network SSID
 * @param password Network password
 * @return ESP_OK on success
 */
esp_err_t config_manager_set_wifi(const char *ssid, const char *password);

/**
 * @brief Get alarm by ID
 *
 * @param id Alarm ID (0-7)
 * @return Pointer to alarm config, NULL if invalid ID
 */
alarm_config_t *config_manager_get_alarm(uint8_t id);

/**
 * @brief Get next enabled alarm
 *
 * @param current_day Current day of week (0=Sunday)
 * @param current_hour Current hour
 * @param current_minute Current minute
 * @param alarm_id Output: ID of next alarm
 * @return Pointer to next alarm, NULL if none
 */
alarm_config_t *config_manager_get_next_alarm(uint8_t current_day,
                                               uint8_t current_hour,
                                               uint8_t current_minute,
                                               uint8_t *alarm_id);

/**
 * @brief Check if dark mode is currently blocking lights
 *
 * @param day Day of week (0=Sunday)
 * @param hour Current hour (0-23)
 * @param minute Current minute (0-59)
 * @return true if dark mode is active and blocking
 */
bool config_manager_is_dark_mode_blocking(uint8_t day, uint8_t hour, uint8_t minute);

#ifdef __cplusplus
}
#endif

#endif // CONFIG_MANAGER_H
