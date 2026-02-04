#include <string.h>
#include "nvs_flash.h"
#include "nvs.h"
#include "esp_log.h"
#include "esp_event.h"
#include "config_manager.h"

static const char *TAG = "config_mgr";

#define NVS_NAMESPACE "morninglight"
#define NVS_KEY_CONFIG "config"
#define CONFIG_VERSION 1

// Forward declare app events
extern esp_event_base_t APP_EVENTS;
enum {
    APP_EVENT_CONFIG_CHANGED = 16,
    APP_EVENT_CONFIG_SAVED,
};

// Current configuration
static device_config_t config;
static bool initialized = false;

static void set_defaults(void)
{
    memset(&config, 0, sizeof(config));

    // Default timezone
    strncpy(config.timezone, CONFIG_ML_DEFAULT_TIMEZONE, sizeof(config.timezone) - 1);

    // Default brightness
    config.brightness_max = CONFIG_ML_LED_MAX_BRIGHTNESS;

    // Default PWM frequency
    config.pwm_frequency = CONFIG_ML_LED_PWM_FREQ_HZ;

    // Default LED type and count
#ifdef CONFIG_ML_LED_TYPE_WS2811
    config.led_type = CONFIG_LED_TYPE_WS2811;
#else
    config.led_type = CONFIG_LED_TYPE_PWM;
#endif
    config.led_count = CONFIG_ML_LED_COUNT_DEFAULT;

    // Default alarm (disabled)
    config.alarms[0].hour = 7;
    config.alarms[0].minute = 0;
    config.alarms[0].duration_min = CONFIG_ML_SUNRISE_DEFAULT_DURATION_MIN;
    config.alarms[0].days_mask = 0b0111110;  // Mon-Fri
    config.alarms[0].color_temp = 3000;
    config.alarms[0].brightness = 100;
    config.alarms[0].enabled = false;

    config.setup_complete = false;
}

esp_err_t config_manager_init(void)
{
    ESP_LOGI(TAG, "Initializing config manager");

    if (initialized) {
        return ESP_OK;
    }

    // Set defaults first
    set_defaults();

    // Try to load from NVS
    nvs_handle_t handle;
    esp_err_t err = nvs_open(NVS_NAMESPACE, NVS_READONLY, &handle);

    if (err == ESP_OK) {
        size_t required_size = sizeof(device_config_t);
        err = nvs_get_blob(handle, NVS_KEY_CONFIG, &config, &required_size);

        if (err == ESP_OK) {
            ESP_LOGI(TAG, "Loaded config from NVS (size=%d)", required_size);
            ESP_LOGI(TAG, "Config values: led_type=%d, led_count=%d, pwm_freq=%lu",
                     config.led_type, config.led_count, config.pwm_frequency);
        } else {
            ESP_LOGW(TAG, "Failed to read config: %s, using defaults", esp_err_to_name(err));
            set_defaults();
        }

        nvs_close(handle);
    } else if (err == ESP_ERR_NVS_NOT_FOUND) {
        ESP_LOGI(TAG, "No config in NVS, using defaults");
    } else {
        ESP_LOGE(TAG, "Failed to open NVS: %s", esp_err_to_name(err));
        return err;
    }

    initialized = true;
    ESP_LOGI(TAG, "Config manager initialized, setup_complete=%d", config.setup_complete);

    return ESP_OK;
}

device_config_t *config_manager_get(void)
{
    return &config;
}

esp_err_t config_manager_save(void)
{
    if (!initialized) {
        return ESP_ERR_INVALID_STATE;
    }

    ESP_LOGI(TAG, "Saving config to NVS");

    nvs_handle_t handle;
    esp_err_t err = nvs_open(NVS_NAMESPACE, NVS_READWRITE, &handle);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "Failed to open NVS: %s", esp_err_to_name(err));
        return err;
    }

    err = nvs_set_blob(handle, NVS_KEY_CONFIG, &config, sizeof(device_config_t));
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "Failed to write config: %s", esp_err_to_name(err));
        nvs_close(handle);
        return err;
    }

    err = nvs_commit(handle);
    nvs_close(handle);

    if (err == ESP_OK) {
        ESP_LOGI(TAG, "Config saved successfully");
        esp_event_post(APP_EVENTS, APP_EVENT_CONFIG_SAVED, NULL, 0, 0);
        esp_event_post(APP_EVENTS, APP_EVENT_CONFIG_CHANGED, NULL, 0, 0);
    }

    return err;
}

esp_err_t config_manager_factory_reset(void)
{
    ESP_LOGW(TAG, "Factory reset requested");

    nvs_handle_t handle;
    esp_err_t err = nvs_open(NVS_NAMESPACE, NVS_READWRITE, &handle);
    if (err == ESP_OK) {
        nvs_erase_all(handle);
        nvs_commit(handle);
        nvs_close(handle);
    }

    set_defaults();
    return ESP_OK;
}

esp_err_t config_manager_set_wifi(const char *ssid, const char *password)
{
    if (!initialized) {
        return ESP_ERR_INVALID_STATE;
    }

    if (ssid == NULL) {
        return ESP_ERR_INVALID_ARG;
    }

    strncpy(config.wifi.ssid, ssid, sizeof(config.wifi.ssid) - 1);
    config.wifi.ssid[sizeof(config.wifi.ssid) - 1] = '\0';

    if (password) {
        strncpy(config.wifi.password, password, sizeof(config.wifi.password) - 1);
        config.wifi.password[sizeof(config.wifi.password) - 1] = '\0';
    } else {
        config.wifi.password[0] = '\0';
    }

    config.setup_complete = true;

    return config_manager_save();
}

alarm_config_t *config_manager_get_alarm(uint8_t id)
{
    if (id >= 8) {
        return NULL;
    }
    return &config.alarms[id];
}

alarm_config_t *config_manager_get_next_alarm(uint8_t current_day,
                                               uint8_t current_hour,
                                               uint8_t current_minute,
                                               uint8_t *alarm_id)
{
    // Convert current time to minutes since midnight
    uint16_t current_mins = current_hour * 60 + current_minute;

    alarm_config_t *best_alarm = NULL;
    uint8_t best_id = 0;
    int best_delta = INT32_MAX;

    // Check all alarms
    for (uint8_t i = 0; i < 8; i++) {
        alarm_config_t *alarm = &config.alarms[i];

        if (!alarm->enabled) continue;

        uint16_t alarm_mins = alarm->hour * 60 + alarm->minute;

        // Check each day of the week
        for (int day_offset = 0; day_offset < 7; day_offset++) {
            uint8_t check_day = (current_day + day_offset) % 7;

            // Check if alarm is enabled for this day
            if (!(alarm->days_mask & (1 << check_day))) {
                continue;
            }

            // Calculate minutes until alarm
            int delta;
            if (day_offset == 0 && alarm_mins >= current_mins) {
                // Same day, alarm is now or in future
                delta = alarm_mins - current_mins;
            } else if (day_offset > 0) {
                // Future day
                delta = (day_offset * 24 * 60) + alarm_mins - current_mins;
            } else {
                // Alarm already passed today, skip
                continue;
            }

            if (delta < best_delta) {
                best_delta = delta;
                best_alarm = alarm;
                best_id = i;
            }

            break;  // Found nearest occurrence for this alarm
        }
    }

    if (alarm_id) {
        *alarm_id = best_id;
    }

    return best_alarm;
}
