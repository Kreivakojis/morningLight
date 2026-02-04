#ifndef CONFIG_MANAGER_H
#define CONFIG_MANAGER_H

#include <stdint.h>
#include <stdbool.h>
#include "esp_err.h"

#ifdef __cplusplus
extern "C" {
#endif

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
} alarm_config_t;

/**
 * @brief LED type enumeration for config storage
 */
typedef enum {
    CONFIG_LED_TYPE_PWM = 0,
    CONFIG_LED_TYPE_WS2811 = 1,
} config_led_type_t;

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

#ifdef __cplusplus
}
#endif

#endif // CONFIG_MANAGER_H
