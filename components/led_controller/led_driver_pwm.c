#include "driver/ledc.h"
#include "esp_log.h"
#include "led_driver.h"
#include "config_manager.h"

// External color utilities
extern uint32_t color_utils_gamma_correct(uint8_t value, float gamma);

static const char *TAG = "led_pwm";

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
} pwm_state = {0};

static void apply_pwm(void)
{
    if (!pwm_state.initialized) return;

    // Apply brightness scaling
    float brightness_scale = pwm_state.brightness / 100.0f;

    // Scale RGB by brightness
    uint8_t scaled_r = (uint8_t)(pwm_state.r * brightness_scale);
    uint8_t scaled_g = (uint8_t)(pwm_state.g * brightness_scale);
    uint8_t scaled_b = (uint8_t)(pwm_state.b * brightness_scale);

    // Apply gamma correction to get duty cycle
    // N-channel MOSFETs: HIGH = ON, higher duty = brighter
    uint32_t duty_r = color_utils_gamma_correct(scaled_r, GAMMA_VALUE);
    uint32_t duty_g = color_utils_gamma_correct(scaled_g, GAMMA_VALUE);
    uint32_t duty_b = color_utils_gamma_correct(scaled_b, GAMMA_VALUE);

    ESP_LOGD(TAG, "PWM duty: R=%lu, G=%lu, B=%lu", duty_r, duty_g, duty_b);

    // Set PWM duty cycles
    ledc_set_duty(LEDC_MODE, LEDC_CHANNEL_R, duty_r);
    ledc_update_duty(LEDC_MODE, LEDC_CHANNEL_R);

    ledc_set_duty(LEDC_MODE, LEDC_CHANNEL_G, duty_g);
    ledc_update_duty(LEDC_MODE, LEDC_CHANNEL_G);

    ledc_set_duty(LEDC_MODE, LEDC_CHANNEL_B, duty_b);
    ledc_update_duty(LEDC_MODE, LEDC_CHANNEL_B);
}

static esp_err_t pwm_init(void)
{
    ESP_LOGI(TAG, "Initializing PWM LED driver");

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
    // Initial duty = 0 = LOW = N-channel MOSFET OFF = LED OFF
    ledc_channel_config_t ch_conf = {
        .gpio_num = CONFIG_ML_GPIO_LED_R,
        .speed_mode = LEDC_MODE,
        .channel = LEDC_CHANNEL_R,
        .intr_type = LEDC_INTR_DISABLE,
        .timer_sel = LEDC_TIMER,
        .duty = 0,
        .hpoint = 0,
    };
    ESP_ERROR_CHECK(ledc_channel_config(&ch_conf));

    // Configure Green channel
    ch_conf.gpio_num = CONFIG_ML_GPIO_LED_G;
    ch_conf.channel = LEDC_CHANNEL_G;
    ch_conf.duty = 0;
    ESP_ERROR_CHECK(ledc_channel_config(&ch_conf));

    // Configure Blue channel
    ch_conf.gpio_num = CONFIG_ML_GPIO_LED_B;
    ch_conf.channel = LEDC_CHANNEL_B;
    ch_conf.duty = 0;
    ESP_ERROR_CHECK(ledc_channel_config(&ch_conf));

    pwm_state.initialized = true;
    pwm_state.brightness = 0;
    pwm_state.r = pwm_state.g = pwm_state.b = 0;

    // Apply saved PWM frequency from config
    device_config_t *config = config_manager_get();
    if (config && config->pwm_frequency >= 100 && config->pwm_frequency <= 40000) {
        ledc_set_freq(LEDC_MODE, LEDC_TIMER, config->pwm_frequency);
        ESP_LOGI(TAG, "PWM LED driver initialized: freq=%luHz", config->pwm_frequency);
    } else {
        ESP_LOGI(TAG, "PWM LED driver initialized: freq=%dHz", LEDC_FREQUENCY);
    }

    return ESP_OK;
}

static void pwm_set_rgb(uint8_t r, uint8_t g, uint8_t b)
{
    if (!pwm_state.initialized) return;

    pwm_state.r = r;
    pwm_state.g = g;
    pwm_state.b = b;
    apply_pwm();
}

static void pwm_set_brightness(uint8_t brightness)
{
    if (!pwm_state.initialized) return;

    if (brightness > 100) brightness = 100;
    pwm_state.brightness = brightness;
    apply_pwm();
}

static uint8_t pwm_get_brightness(void)
{
    return pwm_state.brightness;
}

static esp_err_t pwm_set_frequency(uint32_t freq_hz)
{
    if (!pwm_state.initialized) return ESP_ERR_INVALID_STATE;

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

static uint32_t pwm_get_frequency(void)
{
    if (!pwm_state.initialized) return 0;
    return ledc_get_freq(LEDC_MODE, LEDC_TIMER);
}

// Export PWM driver operations
const led_driver_ops_t led_driver_pwm = {
    .init = pwm_init,
    .set_rgb = pwm_set_rgb,
    .set_brightness = pwm_set_brightness,
    .get_brightness = pwm_get_brightness,
    .set_frequency = pwm_set_frequency,
    .get_frequency = pwm_get_frequency,
};
