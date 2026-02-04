#ifndef LED_CONTROLLER_H
#define LED_CONTROLLER_H

#include <stdint.h>
#include <stdbool.h>
#include "esp_err.h"

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief LED type enumeration
 */
typedef enum {
    LED_TYPE_PWM = 0,      /**< PWM RGB (3-channel analog) */
    LED_TYPE_WS2811 = 1,   /**< WS2811 addressable LEDs */
} led_type_t;

/**
 * @brief Initialize the LED controller
 *
 * Selects and initializes the appropriate driver based on configuration.
 * For PWM: Configures PWM channels for RGB LED control with 12-bit resolution.
 * For WS2811: Configures RMT peripheral for addressable LED control.
 *
 * @return ESP_OK on success
 */
esp_err_t led_controller_init(void);

/**
 * @brief Set RGB color values (0-255 each)
 *
 * Values are gamma corrected for perceptually uniform brightness.
 *
 * @param r Red component (0-255)
 * @param g Green component (0-255)
 * @param b Blue component (0-255)
 */
void led_controller_set_rgb(uint8_t r, uint8_t g, uint8_t b);

/**
 * @brief Set overall brightness
 *
 * @param brightness Brightness percentage (0-100)
 */
void led_controller_set_brightness(uint8_t brightness);

/**
 * @brief Get current brightness
 *
 * @return Current brightness percentage (0-100)
 */
uint8_t led_controller_get_brightness(void);

/**
 * @brief Set color from HSV values
 *
 * @param h Hue (0-360)
 * @param s Saturation (0-100)
 * @param v Value/brightness (0-100)
 */
void led_controller_set_hsv(uint16_t h, uint8_t s, uint8_t v);

/**
 * @brief Set color temperature
 *
 * @param kelvin Color temperature in Kelvin (1000-10000)
 * @param brightness Brightness percentage (0-100)
 */
void led_controller_set_color_temp(uint16_t kelvin, uint8_t brightness);

/**
 * @brief Fade to target color over duration
 *
 * @param r Target red (0-255)
 * @param g Target green (0-255)
 * @param b Target blue (0-255)
 * @param brightness Target brightness (0-100)
 * @param duration_ms Fade duration in milliseconds
 */
void led_controller_fade_to(uint8_t r, uint8_t g, uint8_t b,
                            uint8_t brightness, uint32_t duration_ms);

/**
 * @brief Stop any active fade
 */
void led_controller_fade_stop(void);

/**
 * @brief Check if fade is in progress
 *
 * @return true if fading, false otherwise
 */
bool led_controller_is_fading(void);

/**
 * @brief Turn off all LEDs immediately
 */
void led_controller_off(void);

/**
 * @brief Set raw PWM duty cycles directly (bypasses gamma correction)
 *
 * For PWM mode, sets duty cycles directly.
 * For WS2811 mode, converts to RGB values.
 *
 * @param duty_r Red duty cycle (0-4095)
 * @param duty_g Green duty cycle (0-4095)
 * @param duty_b Blue duty cycle (0-4095)
 */
void led_controller_set_duty_raw(uint32_t duty_r, uint32_t duty_g, uint32_t duty_b);

/**
 * @brief Set PWM frequency (PWM mode only)
 *
 * @param freq_hz Frequency in Hz (100-40000)
 * @return ESP_OK on success, ESP_ERR_NOT_SUPPORTED for WS2811 mode
 */
esp_err_t led_controller_set_frequency(uint32_t freq_hz);

/**
 * @brief Get current PWM frequency (PWM mode only)
 *
 * @return Current frequency in Hz, or 0 for WS2811 mode
 */
uint32_t led_controller_get_frequency(void);

/**
 * @brief Get current LED type
 *
 * @return Current LED type (PWM or WS2811)
 */
led_type_t led_controller_get_type(void);

/**
 * @brief Get configured LED count (WS2811 mode)
 *
 * @return Number of LEDs configured (1-300)
 */
uint16_t led_controller_get_led_count(void);

#ifdef __cplusplus
}
#endif

#endif // LED_CONTROLLER_H
