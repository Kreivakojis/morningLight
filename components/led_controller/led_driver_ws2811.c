#include <string.h>
#include <math.h>
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

    float brightness_scale = ws2811_state.brightness / 100.0f;

    // Gamma-correct the color at full intensity, then scale by brightness.
    // This preserves low-brightness resolution. The old approach of scaling
    // first then gamma-correcting crushed small values to zero through the
    // 12-bit PWM intermediary (e.g. 8% brightness → output 0).
    uint8_t out_r = (uint8_t)(powf(ws2811_state.r / 255.0f, GAMMA_VALUE) * brightness_scale * 255.0f);
    uint8_t out_g = (uint8_t)(powf(ws2811_state.g / 255.0f, GAMMA_VALUE) * brightness_scale * 255.0f);
    uint8_t out_b = (uint8_t)(powf(ws2811_state.b / 255.0f, GAMMA_VALUE) * brightness_scale * 255.0f);

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

static void ws2811_set_pixel_brightnesses(const uint8_t *brightness_array, uint16_t count)
{
    if (!ws2811_state.initialized || !ws2811_state.strip) {
        return;
    }
    if (brightness_array == NULL || count == 0) {
        return;
    }
    if (count > ws2811_state.led_count) {
        count = ws2811_state.led_count;
    }

    // Gamma-correct base color once at full intensity, then scale per-LED
    float gamma_r = powf(ws2811_state.r / 255.0f, GAMMA_VALUE) * 255.0f;
    float gamma_g = powf(ws2811_state.g / 255.0f, GAMMA_VALUE) * 255.0f;
    float gamma_b = powf(ws2811_state.b / 255.0f, GAMMA_VALUE) * 255.0f;

    for (uint16_t i = 0; i < count; i++) {
        uint8_t brightness = brightness_array[i];
        if (brightness > 100) brightness = 100;

        float scale = brightness / 100.0f;
        uint8_t out_r = (uint8_t)(gamma_r * scale);
        uint8_t out_g = (uint8_t)(gamma_g * scale);
        uint8_t out_b = (uint8_t)(gamma_b * scale);

        // WS2811 color order correction: swap G and B
        led_strip_set_pixel(ws2811_state.strip, i, out_r, out_b, out_g);
    }

    // Fill remaining LEDs with zero if count < led_count
    for (uint16_t i = count; i < ws2811_state.led_count; i++) {
        led_strip_set_pixel(ws2811_state.strip, i, 0, 0, 0);
    }

    led_strip_refresh(ws2811_state.strip);
}

// Export WS2811 driver operations
const led_driver_ops_t led_driver_ws2811 = {
    .init = ws2811_init,
    .set_rgb = ws2811_set_rgb,
    .set_brightness = ws2811_set_brightness,
    .get_brightness = ws2811_get_brightness,
    .set_frequency = ws2811_set_frequency,
    .get_frequency = ws2811_get_frequency,
    .set_pixel_brightnesses = ws2811_set_pixel_brightnesses,
};
