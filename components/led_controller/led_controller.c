#include <math.h>
#include <string.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/semphr.h"
#include "driver/ledc.h"
#include "esp_log.h"
#include "led_controller.h"
#include "config_manager.h"

// External color utilities
extern void color_utils_hsv_to_rgb(uint16_t h, uint8_t s, uint8_t v,
                                   uint8_t *r, uint8_t *g, uint8_t *b);
extern void color_utils_kelvin_to_rgb(uint16_t kelvin,
                                      uint8_t *r, uint8_t *g, uint8_t *b);
extern uint32_t color_utils_gamma_correct(uint8_t value, float gamma);

static const char *TAG = "led_ctrl";

// PWM Configuration
#define LEDC_TIMER          LEDC_TIMER_0
#define LEDC_MODE           LEDC_LOW_SPEED_MODE
#define LEDC_DUTY_RES       LEDC_TIMER_12_BIT
#define LEDC_DUTY_MAX       4095
#define LEDC_FREQUENCY      CONFIG_ML_LED_PWM_FREQ_HZ

// Channel assignments
#define LEDC_CHANNEL_R      LEDC_CHANNEL_0
#define LEDC_CHANNEL_G      LEDC_CHANNEL_1
#define LEDC_CHANNEL_B      LEDC_CHANNEL_2

// Gamma correction value
#define GAMMA_VALUE         2.2f

// State
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
} led_state = {0};

static void apply_pwm(void)
{
    if (!led_state.initialized) return;

    // Apply brightness scaling
    float brightness_scale = led_state.brightness / 100.0f;

    // Scale RGB by brightness
    uint8_t scaled_r = (uint8_t)(led_state.r * brightness_scale);
    uint8_t scaled_g = (uint8_t)(led_state.g * brightness_scale);
    uint8_t scaled_b = (uint8_t)(led_state.b * brightness_scale);

    // Apply gamma correction to get duty cycle
    uint32_t duty_r = color_utils_gamma_correct(scaled_r, GAMMA_VALUE);
    uint32_t duty_g = color_utils_gamma_correct(scaled_g, GAMMA_VALUE);
    uint32_t duty_b = color_utils_gamma_correct(scaled_b, GAMMA_VALUE);

    // Invert duty cycle for P-channel MOSFETs (active low)
    // P-channel: LOW = ON, HIGH = OFF
    duty_r = LEDC_DUTY_MAX - duty_r;
    duty_g = LEDC_DUTY_MAX - duty_g;
    duty_b = LEDC_DUTY_MAX - duty_b;

    ESP_LOGD(TAG, "PWM duty: R=%lu, G=%lu, B=%lu (inverted for P-channel)",
             duty_r, duty_g, duty_b);

    // Set PWM duty cycles
    ledc_set_duty(LEDC_MODE, LEDC_CHANNEL_R, duty_r);
    ledc_update_duty(LEDC_MODE, LEDC_CHANNEL_R);

    ledc_set_duty(LEDC_MODE, LEDC_CHANNEL_G, duty_g);
    ledc_update_duty(LEDC_MODE, LEDC_CHANNEL_G);

    ledc_set_duty(LEDC_MODE, LEDC_CHANNEL_B, duty_b);
    ledc_update_duty(LEDC_MODE, LEDC_CHANNEL_B);
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
            apply_pwm();
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
        apply_pwm();
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

    // Configure LEDC timer
    ledc_timer_config_t timer_conf = {
        .speed_mode = LEDC_MODE,
        .duty_resolution = LEDC_DUTY_RES,
        .timer_num = LEDC_TIMER,
        .freq_hz = LEDC_FREQUENCY,
        .clk_cfg = LEDC_AUTO_CLK,
    };
    ESP_ERROR_CHECK(ledc_timer_config(&timer_conf));

    // Configure Red channel
    // Initial duty = LEDC_DUTY_MAX (4095) = HIGH = P-channel MOSFET OFF = LED OFF
    ledc_channel_config_t ch_conf = {
        .gpio_num = CONFIG_ML_GPIO_LED_R,
        .speed_mode = LEDC_MODE,
        .channel = LEDC_CHANNEL_R,
        .intr_type = LEDC_INTR_DISABLE,
        .timer_sel = LEDC_TIMER,
        .duty = LEDC_DUTY_MAX,
        .hpoint = 0,
    };
    ESP_ERROR_CHECK(ledc_channel_config(&ch_conf));

    // Configure Green channel
    ch_conf.gpio_num = CONFIG_ML_GPIO_LED_G;
    ch_conf.channel = LEDC_CHANNEL_G;
    ch_conf.duty = LEDC_DUTY_MAX;
    ESP_ERROR_CHECK(ledc_channel_config(&ch_conf));

    // Configure Blue channel
    ch_conf.gpio_num = CONFIG_ML_GPIO_LED_B;
    ch_conf.channel = LEDC_CHANNEL_B;
    ch_conf.duty = LEDC_DUTY_MAX;
    ESP_ERROR_CHECK(ledc_channel_config(&ch_conf));

    led_state.initialized = true;
    led_state.brightness = 0;
    led_state.r = led_state.g = led_state.b = 0;

    // Apply saved PWM frequency from config
    device_config_t *config = config_manager_get();
    if (config && config->pwm_frequency >= 100 && config->pwm_frequency <= 40000) {
        ledc_set_freq(LEDC_MODE, LEDC_TIMER, config->pwm_frequency);
        ESP_LOGI(TAG, "LED controller initialized: freq=%luHz", config->pwm_frequency);
    } else {
        ESP_LOGI(TAG, "LED controller initialized: freq=%dHz", LEDC_FREQUENCY);
    }

    return ESP_OK;
}

void led_controller_set_rgb(uint8_t r, uint8_t g, uint8_t b)
{
    if (!led_state.initialized) return;

    xSemaphoreTake(led_state.mutex, portMAX_DELAY);
    led_state.r = r;
    led_state.g = g;
    led_state.b = b;
    apply_pwm();
    xSemaphoreGive(led_state.mutex);
}

void led_controller_set_brightness(uint8_t brightness)
{
    if (!led_state.initialized) return;

    if (brightness > 100) brightness = 100;

    xSemaphoreTake(led_state.mutex, portMAX_DELAY);
    led_state.brightness = brightness;
    apply_pwm();
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
    uint8_t r, g, b;
    color_utils_kelvin_to_rgb(kelvin, &r, &g, &b);

    ESP_LOGD(TAG, "set_color_temp: %dK @ %d%% -> RGB(%d,%d,%d)",
             kelvin, brightness, r, g, b);

    led_controller_set_rgb(r, g, b);
    led_controller_set_brightness(brightness);
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
    if (!led_state.initialized) return;

    // Clamp values to valid range
    if (duty_r > LEDC_DUTY_MAX) duty_r = LEDC_DUTY_MAX;
    if (duty_g > LEDC_DUTY_MAX) duty_g = LEDC_DUTY_MAX;
    if (duty_b > LEDC_DUTY_MAX) duty_b = LEDC_DUTY_MAX;

    // Invert for P-channel MOSFETs: user sees 0=off, 4095=full on
    // but P-channel needs LOW=on, HIGH=off
    uint32_t hw_r = LEDC_DUTY_MAX - duty_r;
    uint32_t hw_g = LEDC_DUTY_MAX - duty_g;
    uint32_t hw_b = LEDC_DUTY_MAX - duty_b;

    ESP_LOGD(TAG, "Raw PWM: user R=%lu G=%lu B=%lu -> hw R=%lu G=%lu B=%lu",
             duty_r, duty_g, duty_b, hw_r, hw_g, hw_b);

    xSemaphoreTake(led_state.mutex, portMAX_DELAY);

    ledc_set_duty(LEDC_MODE, LEDC_CHANNEL_R, hw_r);
    ledc_update_duty(LEDC_MODE, LEDC_CHANNEL_R);

    ledc_set_duty(LEDC_MODE, LEDC_CHANNEL_G, hw_g);
    ledc_update_duty(LEDC_MODE, LEDC_CHANNEL_G);

    ledc_set_duty(LEDC_MODE, LEDC_CHANNEL_B, hw_b);
    ledc_update_duty(LEDC_MODE, LEDC_CHANNEL_B);

    xSemaphoreGive(led_state.mutex);
}

esp_err_t led_controller_set_frequency(uint32_t freq_hz)
{
    if (!led_state.initialized) return ESP_ERR_INVALID_STATE;

    // Clamp to valid range
    if (freq_hz < 100) freq_hz = 100;
    if (freq_hz > 40000) freq_hz = 40000;

    ESP_LOGI(TAG, "Setting PWM frequency to %lu Hz", freq_hz);

    esp_err_t ret = ledc_set_freq(LEDC_MODE, LEDC_TIMER, freq_hz);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Failed to set frequency: %s", esp_err_to_name(ret));
    }
    return ret;
}

uint32_t led_controller_get_frequency(void)
{
    if (!led_state.initialized) return 0;
    return ledc_get_freq(LEDC_MODE, LEDC_TIMER);
}
