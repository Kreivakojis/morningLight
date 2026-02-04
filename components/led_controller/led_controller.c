#include <math.h>
#include <string.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/semphr.h"
#include "esp_log.h"
#include "led_controller.h"
#include "led_driver.h"
#include "config_manager.h"

// External color utilities
extern void color_utils_hsv_to_rgb(uint16_t h, uint8_t s, uint8_t v,
                                   uint8_t *r, uint8_t *g, uint8_t *b);
extern void color_utils_kelvin_to_rgb(uint16_t kelvin,
                                      uint8_t *r, uint8_t *g, uint8_t *b);

static const char *TAG = "led_ctrl";

// Current driver
static const led_driver_ops_t *driver = NULL;

// State (for fading, independent of driver)
static struct {
    uint8_t r, g, b;
    uint8_t brightness;
    bool initialized;
    SemaphoreHandle_t mutex;

    // Fade state
    bool fading;
    TaskHandle_t fade_task;
    uint8_t fade_start_r, fade_start_g, fade_start_b, fade_start_brightness;
    uint8_t fade_end_r, fade_end_g, fade_end_b, fade_end_brightness;
    uint32_t fade_duration_ms;
    uint32_t fade_start_time;

    // LED type tracking
    led_type_t led_type;
    uint16_t led_count;
} led_state = {0};

static void apply_color(void)
{
    if (!led_state.initialized || !driver) return;

    driver->set_rgb(led_state.r, led_state.g, led_state.b);
    driver->set_brightness(led_state.brightness);
}

static void fade_task_fn(void *arg)
{
    while (led_state.fading) {
        uint32_t elapsed = xTaskGetTickCount() * portTICK_PERIOD_MS - led_state.fade_start_time;

        if (elapsed >= led_state.fade_duration_ms) {
            // Fade complete
            xSemaphoreTake(led_state.mutex, portMAX_DELAY);
            led_state.r = led_state.fade_end_r;
            led_state.g = led_state.fade_end_g;
            led_state.b = led_state.fade_end_b;
            led_state.brightness = led_state.fade_end_brightness;
            apply_color();
            led_state.fading = false;
            xSemaphoreGive(led_state.mutex);
            break;
        }

        // Calculate interpolation factor (0.0 to 1.0)
        float t = (float)elapsed / led_state.fade_duration_ms;

        // Linear interpolation
        xSemaphoreTake(led_state.mutex, portMAX_DELAY);
        led_state.r = led_state.fade_start_r + (int)(t * (led_state.fade_end_r - led_state.fade_start_r));
        led_state.g = led_state.fade_start_g + (int)(t * (led_state.fade_end_g - led_state.fade_start_g));
        led_state.b = led_state.fade_start_b + (int)(t * (led_state.fade_end_b - led_state.fade_start_b));
        led_state.brightness = led_state.fade_start_brightness +
                               (int)(t * (led_state.fade_end_brightness - led_state.fade_start_brightness));
        apply_color();
        xSemaphoreGive(led_state.mutex);

        vTaskDelay(pdMS_TO_TICKS(20)); // 50Hz update rate
    }

    led_state.fade_task = NULL;
    vTaskDelete(NULL);
}

esp_err_t led_controller_init(void)
{
    ESP_LOGI(TAG, "Initializing LED controller");

    // Create mutex
    led_state.mutex = xSemaphoreCreateMutex();
    if (led_state.mutex == NULL) {
        ESP_LOGE(TAG, "Failed to create mutex");
        return ESP_ERR_NO_MEM;
    }

    // Get LED type from config
    device_config_t *cfg = config_manager_get();
    led_type_t led_type = LED_TYPE_PWM;
    uint16_t led_count = 30;

    if (cfg) {
        ESP_LOGI(TAG, "Config: led_type=%d, led_count=%d", cfg->led_type, cfg->led_count);
        led_type = (led_type_t)cfg->led_type;
        led_count = cfg->led_count;
        if (led_count == 0) led_count = 30;
    } else {
        ESP_LOGW(TAG, "Config is NULL, using defaults");
    }

    led_state.led_type = led_type;
    led_state.led_count = led_count;

    // Select driver based on LED type
    ESP_LOGI(TAG, "Selecting driver for led_type=%d", led_type);
    if (led_type == LED_TYPE_WS2811) {
        driver = &led_driver_ws2811;
        ESP_LOGI(TAG, "Using WS2811 driver (%d LEDs)", led_count);
    } else {
        driver = &led_driver_pwm;
        ESP_LOGI(TAG, "Using PWM driver");
    }

    // Initialize driver
    esp_err_t ret = driver->init();
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Driver init failed: %s", esp_err_to_name(ret));
        return ret;
    }

    led_state.initialized = true;
    led_state.brightness = 0;
    led_state.r = led_state.g = led_state.b = 0;

    ESP_LOGI(TAG, "LED controller initialized");
    return ESP_OK;
}

void led_controller_set_rgb(uint8_t r, uint8_t g, uint8_t b)
{
    if (!led_state.initialized) return;

    xSemaphoreTake(led_state.mutex, portMAX_DELAY);
    led_state.r = r;
    led_state.g = g;
    led_state.b = b;
    apply_color();
    xSemaphoreGive(led_state.mutex);
}

void led_controller_set_brightness(uint8_t brightness)
{
    if (!led_state.initialized) return;

    if (brightness > 100) brightness = 100;

    xSemaphoreTake(led_state.mutex, portMAX_DELAY);
    led_state.brightness = brightness;
    apply_color();
    xSemaphoreGive(led_state.mutex);
}

uint8_t led_controller_get_brightness(void)
{
    return led_state.brightness;
}

void led_controller_set_hsv(uint16_t h, uint8_t s, uint8_t v)
{
    uint8_t r, g, b;
    color_utils_hsv_to_rgb(h, s, v, &r, &g, &b);
    led_controller_set_rgb(r, g, b);
}

void led_controller_set_color_temp(uint16_t kelvin, uint8_t brightness)
{
    if (!led_state.initialized) return;

    uint8_t r, g, b;
    color_utils_kelvin_to_rgb(kelvin, &r, &g, &b);

    if (brightness > 100) brightness = 100;

    ESP_LOGD(TAG, "set_color_temp: %dK @ %d%% -> RGB(%d,%d,%d)",
             kelvin, brightness, r, g, b);

    // Set both RGB and brightness atomically with single apply_color() call
    // to avoid multiple LED strip refreshes
    xSemaphoreTake(led_state.mutex, portMAX_DELAY);
    led_state.r = r;
    led_state.g = g;
    led_state.b = b;
    led_state.brightness = brightness;
    apply_color();
    xSemaphoreGive(led_state.mutex);
}

void led_controller_fade_to(uint8_t r, uint8_t g, uint8_t b,
                            uint8_t brightness, uint32_t duration_ms)
{
    if (!led_state.initialized) return;

    // Stop any existing fade
    led_controller_fade_stop();

    xSemaphoreTake(led_state.mutex, portMAX_DELAY);

    // Store start values
    led_state.fade_start_r = led_state.r;
    led_state.fade_start_g = led_state.g;
    led_state.fade_start_b = led_state.b;
    led_state.fade_start_brightness = led_state.brightness;

    // Store end values
    led_state.fade_end_r = r;
    led_state.fade_end_g = g;
    led_state.fade_end_b = b;
    led_state.fade_end_brightness = brightness;

    led_state.fade_duration_ms = duration_ms;
    led_state.fade_start_time = xTaskGetTickCount() * portTICK_PERIOD_MS;
    led_state.fading = true;

    xSemaphoreGive(led_state.mutex);

    // Create fade task
    xTaskCreate(fade_task_fn, "led_fade", 2048, NULL, 5, &led_state.fade_task);
}

void led_controller_fade_stop(void)
{
    if (led_state.fade_task != NULL) {
        led_state.fading = false;
        // Give the task time to exit
        vTaskDelay(pdMS_TO_TICKS(50));
    }
}

bool led_controller_is_fading(void)
{
    return led_state.fading;
}

void led_controller_off(void)
{
    led_controller_fade_stop();
    led_controller_set_brightness(0);
}

void led_controller_set_duty_raw(uint32_t duty_r, uint32_t duty_g, uint32_t duty_b)
{
    // This function is PWM-specific and maintained for backward compatibility
    // For WS2811, we convert duty to RGB values
    if (!led_state.initialized) return;

    // Clamp values to valid range (12-bit)
    if (duty_r > 4095) duty_r = 4095;
    if (duty_g > 4095) duty_g = 4095;
    if (duty_b > 4095) duty_b = 4095;

    // Convert 12-bit duty to 8-bit RGB
    uint8_t r = (duty_r * 255) / 4095;
    uint8_t g = (duty_g * 255) / 4095;
    uint8_t b = (duty_b * 255) / 4095;

    ESP_LOGD(TAG, "Raw duty: R=%lu G=%lu B=%lu -> RGB(%d,%d,%d)",
             duty_r, duty_g, duty_b, r, g, b);

    xSemaphoreTake(led_state.mutex, portMAX_DELAY);
    led_state.r = r;
    led_state.g = g;
    led_state.b = b;
    led_state.brightness = 100; // Raw mode assumes full brightness
    apply_color();
    xSemaphoreGive(led_state.mutex);
}

esp_err_t led_controller_set_frequency(uint32_t freq_hz)
{
    if (!led_state.initialized || !driver) return ESP_ERR_INVALID_STATE;
    return driver->set_frequency(freq_hz);
}

uint32_t led_controller_get_frequency(void)
{
    if (!led_state.initialized || !driver) return 0;
    return driver->get_frequency();
}

led_type_t led_controller_get_type(void)
{
    return led_state.led_type;
}

uint16_t led_controller_get_led_count(void)
{
    return led_state.led_count;
}

void led_controller_set_pixel_brightnesses(const uint8_t *brightness_array, uint16_t count)
{
    if (!led_state.initialized || !driver || !driver->set_pixel_brightnesses) {
        return;
    }

    xSemaphoreTake(led_state.mutex, portMAX_DELAY);
    driver->set_pixel_brightnesses(brightness_array, count);
    xSemaphoreGive(led_state.mutex);
}
