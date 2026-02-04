#ifndef LED_DRIVER_H
#define LED_DRIVER_H

#include <stdint.h>
#include <stdbool.h>
#include "esp_err.h"

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief LED driver operations interface
 *
 * This structure defines the common interface for different LED driver types
 * (PWM RGB, WS2811 addressable, etc.)
 */
typedef struct led_driver_ops {
    /**
     * @brief Initialize the LED driver
     * @return ESP_OK on success
     */
    esp_err_t (*init)(void);

    /**
     * @brief Set RGB color (0-255 each)
     * @param r Red component
     * @param g Green component
     * @param b Blue component
     */
    void (*set_rgb)(uint8_t r, uint8_t g, uint8_t b);

    /**
     * @brief Set brightness (0-100)
     * @param brightness Percentage brightness
     */
    void (*set_brightness)(uint8_t brightness);

    /**
     * @brief Get current brightness
     * @return Current brightness (0-100)
     */
    uint8_t (*get_brightness)(void);

    /**
     * @brief Set PWM frequency (PWM driver only)
     * @param freq_hz Frequency in Hz
     * @return ESP_OK on success, ESP_ERR_NOT_SUPPORTED if not applicable
     */
    esp_err_t (*set_frequency)(uint32_t freq_hz);

    /**
     * @brief Get current PWM frequency (PWM driver only)
     * @return Frequency in Hz, or 0 if not applicable
     */
    uint32_t (*get_frequency)(void);

    /**
     * @brief Set per-LED brightness values (for wave animations)
     *
     * For WS2811: Each LED gets independent brightness based on position (spatial wave)
     * For PWM: Uses first element only (temporal wave, all LEDs same brightness)
     *
     * The base RGB color should be set first via set_rgb().
     *
     * @param brightness_array Array of brightness percentages (0-100) for each LED
     * @param count Number of elements in the array
     */
    void (*set_pixel_brightnesses)(const uint8_t *brightness_array, uint16_t count);
} led_driver_ops_t;

/**
 * @brief PWM driver operations
 */
extern const led_driver_ops_t led_driver_pwm;

/**
 * @brief WS2811 driver operations
 */
extern const led_driver_ops_t led_driver_ws2811;

#ifdef __cplusplus
}
#endif

#endif // LED_DRIVER_H
