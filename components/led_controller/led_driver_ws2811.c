#include <string.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "led_strip.h"
#include "esp_log.h"
#include "led_driver.h"
#include "config_manager.h"

static const char *TAG = "led_ws2811";

// Max supported LEDs
#define MAX_LED_COUNT       300

// Gamma correction value (same as PWM driver)
#define GAMMA_VALUE         2.2f

// External color utilities
extern uint32_t color_utils_gamma_correct(uint8_t value, float gamma);

// State
static struct {
    led_strip_handle_t strip;
    uint16_t led_count;
    uint8_t r, g, b;
    uint8_t brightness;
    bool initialized;
} ws2811_state = {0};

static void update_pixels(void)
{
    if (!ws2811_state.initialized || !ws2811_state.strip) {
        ESP_LOGW(TAG, "update_pixels: not initialized");
        return;
    }

    // Apply brightness scaling
    float brightness_scale = ws2811_state.brightness / 100.0f;

    // Scale RGB by brightness
    uint8_t scaled_r = (uint8_t)(ws2811_state.r * brightness_scale);
    uint8_t scaled_g = (uint8_t)(ws2811_state.g * brightness_scale);
    uint8_t scaled_b = (uint8_t)(ws2811_state.b * brightness_scale);

    // Apply gamma correction (scale from 12-bit to 8-bit)
    uint32_t gamma_r = color_utils_gamma_correct(scaled_r, GAMMA_VALUE);
    uint32_t gamma_g = color_utils_gamma_correct(scaled_g, GAMMA_VALUE);
    uint32_t gamma_b = color_utils_gamma_correct(scaled_b, GAMMA_VALUE);

    // Convert 12-bit PWM value to 8-bit for LED strip
    uint8_t out_r = (gamma_r * 255) / 4095;
    uint8_t out_g = (gamma_g * 255) / 4095;
    uint8_t out_b = (gamma_b * 255) / 4095;

    ESP_LOGD(TAG, "LED update: RGB(%d,%d,%d) bright=%d%% -> out RGB(%d,%d,%d)",
             ws2811_state.r, ws2811_state.g, ws2811_state.b,
             ws2811_state.brightness, out_r, out_g, out_b);

    // Set all LEDs to the same color
    // WS2811 color order correction: swap G and B
    for (uint16_t i = 0; i < ws2811_state.led_count; i++) {
        led_strip_set_pixel(ws2811_state.strip, i, out_r, out_b, out_g);
    }

    // Refresh the strip
    esp_err_t ret = led_strip_refresh(ws2811_state.strip);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "LED strip refresh failed: %s", esp_err_to_name(ret));
    }
}

static esp_err_t ws2811_init(void)
{
    ESP_LOGI(TAG, "Initializing WS2811 LED driver using led_strip component");

    // Get LED count from config
    device_config_t *config = config_manager_get();
    uint16_t led_count = config ? config->led_count : 30;

    // Clamp LED count
    if (led_count == 0) led_count = 30;
    if (led_count > MAX_LED_COUNT) led_count = MAX_LED_COUNT;

    ws2811_state.led_count = led_count;

    ESP_LOGI(TAG, "Configuring LED strip: %d LEDs on GPIO %d", led_count, CONFIG_ML_GPIO_LED_DATA);

    // Configure LED strip using RMT backend
    led_strip_config_t strip_config = {
        .strip_gpio_num = CONFIG_ML_GPIO_LED_DATA,
        .max_leds = led_count,
        .led_pixel_format = LED_PIXEL_FORMAT_GRB,  // Most common format
        .led_model = LED_MODEL_WS2812,  // WS2812 timing works for most strips
        .flags.invert_out = false,
    };

    led_strip_rmt_config_t rmt_config = {
        .clk_src = RMT_CLK_SRC_DEFAULT,
        .resolution_hz = 10 * 1000 * 1000, // 10MHz
        .mem_block_symbols = 128,  // Larger buffer for more stable transmission
        .flags.with_dma = false,   // DMA causes crashes on some ESP32 variants
    };

    esp_err_t ret = led_strip_new_rmt_device(&strip_config, &rmt_config, &ws2811_state.strip);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Failed to create LED strip: %s", esp_err_to_name(ret));
        return ret;
    }

    ws2811_state.initialized = true;
    ws2811_state.brightness = 0;
    ws2811_state.r = ws2811_state.g = ws2811_state.b = 0;

    ESP_LOGI(TAG, "LED strip initialized: %d LEDs on GPIO %d", led_count, CONFIG_ML_GPIO_LED_DATA);

    // Clear LEDs on startup
    led_strip_clear(ws2811_state.strip);

    return ESP_OK;
}

static void ws2811_set_rgb(uint8_t r, uint8_t g, uint8_t b)
{
    if (!ws2811_state.initialized) return;

    // Just store values - don't refresh here
    // set_brightness() will trigger the actual refresh
    ws2811_state.r = r;
    ws2811_state.g = g;
    ws2811_state.b = b;
}

static void ws2811_set_brightness(uint8_t brightness)
{
    if (!ws2811_state.initialized) return;

    if (brightness > 100) brightness = 100;
    ws2811_state.brightness = brightness;
    update_pixels();
}

static uint8_t ws2811_get_brightness(void)
{
    return ws2811_state.brightness;
}

static esp_err_t ws2811_set_frequency(uint32_t freq_hz)
{
    // LED strip doesn't support frequency adjustment
    (void)freq_hz;
    return ESP_ERR_NOT_SUPPORTED;
}

static uint32_t ws2811_get_frequency(void)
{
    // LED strip doesn't have a configurable frequency
    return 0;
}

// Export WS2811 driver operations
const led_driver_ops_t led_driver_ws2811 = {
    .init = ws2811_init,
    .set_rgb = ws2811_set_rgb,
    .set_brightness = ws2811_set_brightness,
    .get_brightness = ws2811_get_brightness,
    .set_frequency = ws2811_set_frequency,
    .get_frequency = ws2811_get_frequency,
};
