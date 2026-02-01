#ifndef SUNRISE_ENGINE_H
#define SUNRISE_ENGINE_H

#include <stdint.h>
#include <stdbool.h>
#include "esp_err.h"

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief Sunrise engine states
 */
typedef enum {
    SUNRISE_STATE_IDLE,         // No active sunrise
    SUNRISE_STATE_SCHEDULED,    // Waiting for next alarm
    SUNRISE_STATE_ACTIVE,       // Sunrise in progress
    SUNRISE_STATE_COMPLETE,     // Sunrise finished, holding brightness
    SUNRISE_STATE_CANCELLED,    // Cancelled by user
} sunrise_state_t;

/**
 * @brief Brightness curve types
 */
typedef enum {
    SUNRISE_CURVE_LINEAR,       // Linear ramp
    SUNRISE_CURVE_LOGARITHMIC,  // Log curve (most natural)
    SUNRISE_CURVE_SIGMOID,      // S-curve
    SUNRISE_CURVE_EXPONENTIAL,  // Exponential
} sunrise_curve_t;

/**
 * @brief Initialize the sunrise engine
 *
 * @return ESP_OK on success
 */
esp_err_t sunrise_engine_init(void);

/**
 * @brief Update alarm schedule from config
 *
 * Should be called after config changes or time sync.
 */
void sunrise_engine_update_schedule(void);

/**
 * @brief Get current state
 *
 * @return Current state
 */
sunrise_state_t sunrise_engine_get_state(void);

/**
 * @brief Get state as string
 *
 * @return State name string
 */
const char *sunrise_engine_get_state_str(void);

/**
 * @brief Get current progress
 *
 * @return Progress percentage (0-100), -1 if not active
 */
int sunrise_engine_get_progress(void);

/**
 * @brief Get ID of currently active or scheduled alarm
 *
 * @return Alarm ID, -1 if none
 */
int sunrise_engine_get_active_alarm(void);

/**
 * @brief Start sunrise manually (for testing)
 *
 * @param duration_min Duration in minutes
 * @param color_temp Color temperature in Kelvin
 * @param brightness Max brightness percentage
 * @return ESP_OK on success
 */
esp_err_t sunrise_engine_start_manual(uint8_t duration_min, uint16_t color_temp, uint8_t brightness);

/**
 * @brief Cancel active sunrise
 */
void sunrise_engine_cancel(void);

/**
 * @brief Stop and turn off LEDs
 */
void sunrise_engine_stop(void);

/**
 * @brief Set the brightness curve
 *
 * @param curve Curve type
 */
void sunrise_engine_set_curve(sunrise_curve_t curve);

/**
 * @brief Deinitialize sunrise engine
 */
void sunrise_engine_deinit(void);

#ifdef __cplusplus
}
#endif

#endif // SUNRISE_ENGINE_H
