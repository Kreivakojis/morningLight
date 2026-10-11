#include <string.h>
#include <math.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "driver/gpio.h"
#include "led_strip.h"
#include "esp_log.h"
#include "led_driver.h"
#include "config_manager.h"

static const char *TAG = "led_ws2811";

// Max supported LEDs per strip
#define MAX_LED_COUNT       300
#define WS2811_INSTANCES    2

static float get_gamma(void) {
    device_config_t *cfg = config_manager_get();
    uint8_t g = cfg ? cfg->gamma_x10 : 0;
    return (g >= 10 && g <= 40) ? g / 10.0f : 2.2f;
}

static bool get_dither_enabled(void) {
    device_config_t *cfg = config_manager_get();
    return cfg ? cfg->dither_enabled : false;
}

// Per-instance state
typedef struct {
    led_strip_handle_t strip;
    uint16_t led_count;
    uint8_t r, g, b;
    uint8_t brightness;
    bool initialized;      // led_strip/RMT device created (only if config-enabled)
    gpio_num_t enable_gpio;
    bool enable_gpio_valid;
    int enable_level;      // last level written to the enable GPIO (-1 = unknown)

    // Temporal dithering error accumulators (one slot per LED per channel).
    // Carries the rounding error from each 8-bit quantization into the next
    // frame's target so consecutive frames alternate between adjacent output
    // levels in a ratio that averages to the true fractional brightness,
    // instead of visibly snapping between them. See set_pixel_brightnesses_impl().
    float dither_err_r[MAX_LED_COUNT];
    float dither_err_g[MAX_LED_COUNT];
    float dither_err_b[MAX_LED_COUNT];
} ws2811_state_t;

static ws2811_state_t ws2811_state[WS2811_INSTANCES] = {0};

static bool array_has_nonzero(const uint8_t *arr, uint16_t count)
{
    for (uint16_t i = 0; i < count; i++) {
        if (arr[i] > 0) {
            return true;
        }
    }
    return false;
}

// Quantizes a float target (0-255) to uint8_t, carrying the leftover
// fractional error forward in *err_acc so it's added into next frame's
// target. Over several frames the output's time-average converges on the
// true target even when the target itself only ever rounds to one of two
// adjacent integers each frame.
static uint8_t dither_quantize(float target, float *err_acc)
{
    float value = target + *err_acc;
    if (value < 0.0f) value = 0.0f;
    if (value > 255.0f) value = 255.0f;

    uint8_t out = (uint8_t)(value + 0.5f);
    *err_acc = value - out;
    return out;
}

// Power-gates the strip's ground return via its low-side MOSFET. Only
// writes the GPIO on a transition to avoid redundant writes every
// animation frame (~30fps).
static void set_enable(int inst, bool on)
{
    ws2811_state_t *s = &ws2811_state[inst];
    if (!s->enable_gpio_valid) {
        return;
    }

    int level = on ? 1 : 0;
    if (s->enable_level == level) {
        return;
    }

    gpio_set_level(s->enable_gpio, level);
    s->enable_level = level;
}

static void update_pixels_impl(int inst)
{
    ws2811_state_t *s = &ws2811_state[inst];
    if (!s->initialized || !s->strip) {
        return;
    }

    float brightness_scale = s->brightness / 100.0f;

    // Gamma-correct the color at full intensity, then scale by brightness.
    // This preserves low-brightness resolution. The old approach of scaling
    // first then gamma-correcting crushed small values to zero through the
    // 12-bit PWM intermediary (e.g. 8% brightness → output 0).
    float gamma = get_gamma();
    uint8_t out_r = (uint8_t)(powf(s->r / 255.0f, gamma) * brightness_scale * 255.0f + 0.5f);
    uint8_t out_g = (uint8_t)(powf(s->g / 255.0f, gamma) * brightness_scale * 255.0f + 0.5f);
    uint8_t out_b = (uint8_t)(powf(s->b / 255.0f, gamma) * brightness_scale * 255.0f + 0.5f);

    // Set all LEDs to the same color
    // WS2811 color order correction: swap G and B
    for (uint16_t i = 0; i < s->led_count; i++) {
        led_strip_set_pixel(s->strip, i, out_r, out_b, out_g);
    }

    esp_err_t ret = led_strip_refresh(s->strip);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "[%d] LED strip refresh failed: %s", inst, esp_err_to_name(ret));
    }

    set_enable(inst, (out_r > 0 || out_g > 0 || out_b > 0));
}

static esp_err_t init_impl(int inst, gpio_num_t data_gpio, gpio_num_t enable_gpio,
                           bool cfg_enabled, uint16_t led_count)
{
    ws2811_state_t *s = &ws2811_state[inst];

    // Configure the enable GPIO as output, driven LOW immediately — fail-safe
    // dark, even if this instance ends up config-disabled (defends against a
    // floating gate if the connector is physically present but unused).
    gpio_config_t io_cfg = {
        .pin_bit_mask = 1ULL << enable_gpio,
        .mode = GPIO_MODE_OUTPUT,
        .pull_up_en = GPIO_PULLUP_DISABLE,
        .pull_down_en = GPIO_PULLDOWN_DISABLE,
        .intr_type = GPIO_INTR_DISABLE,
    };
    esp_err_t ret = gpio_config(&io_cfg);
    if (ret != ESP_OK) {
        ESP_LOGW(TAG, "[%d] Failed to configure enable GPIO %d: %s", inst, enable_gpio, esp_err_to_name(ret));
    }
    s->enable_gpio = enable_gpio;
    s->enable_gpio_valid = (ret == ESP_OK);
    s->enable_level = -1;
    set_enable(inst, false);

    if (!cfg_enabled) {
        ESP_LOGI(TAG, "[%d] Addressable strip disabled in config, skipping strip init", inst);
        return ESP_OK;
    }

    if (led_count == 0) led_count = 30;
    if (led_count > MAX_LED_COUNT) led_count = MAX_LED_COUNT;
    s->led_count = led_count;

    ESP_LOGI(TAG, "[%d] Configuring LED strip: %d LEDs on GPIO %d (enable GPIO %d)",
             inst, led_count, data_gpio, enable_gpio);

    led_strip_config_t strip_config = {
        .strip_gpio_num = data_gpio,
        .max_leds = led_count,
        .led_pixel_format = LED_PIXEL_FORMAT_GRB,
        .led_model = LED_MODEL_WS2812,
        .flags.invert_out = false,
    };

    // SPI+DMA backend instead of RMT: classic ESP32's RMT has no DMA path and
    // only 64 words of hardware buffer per channel, so a long WS2811 frame
    // depends on a timely interrupt-driven refill — which WiFi's driver (its
    // critical sections mask all software interrupt priorities 1-3) was
    // starving badly enough to corrupt individual pixels. SPI+DMA transmits
    // the whole frame via a hardware-walked DMA descriptor once started, with
    // no per-chunk CPU intervention required mid-transfer, so a WiFi stall
    // can only delay when we learn the transfer finished, not corrupt bits
    // already in flight. Each instance gets its own dedicated SPI host
    // (ESP32 classic has exactly 2 general-purpose SPI hosts/DMA channels —
    // a 1:1 fit for WS1+WS2, with none spare for a third SPI-based output).
    led_strip_spi_config_t spi_config = {
        .clk_src = SPI_CLK_SRC_DEFAULT,
        .spi_bus = (inst == 0) ? SPI2_HOST : SPI3_HOST,
        .flags.with_dma = true,
    };

    ret = led_strip_new_spi_device(&strip_config, &spi_config, &s->strip);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "[%d] Failed to create LED strip: %s", inst, esp_err_to_name(ret));
        return ret;
    }

    s->initialized = true;
    s->brightness = 0;
    s->r = s->g = s->b = 0;
    memset(s->dither_err_r, 0, sizeof(s->dither_err_r));
    memset(s->dither_err_g, 0, sizeof(s->dither_err_g));
    memset(s->dither_err_b, 0, sizeof(s->dither_err_b));

    led_strip_clear(s->strip);

    ESP_LOGI(TAG, "[%d] LED strip initialized: %d LEDs on GPIO %d", inst, led_count, data_gpio);
    return ESP_OK;
}

static void set_rgb_impl(int inst, uint8_t r, uint8_t g, uint8_t b)
{
    ws2811_state_t *s = &ws2811_state[inst];
    if (!s->initialized) return;

    // Just store values - don't refresh here
    // set_brightness() will trigger the actual refresh
    s->r = r;
    s->g = g;
    s->b = b;
}

static void set_brightness_impl(int inst, uint8_t brightness)
{
    ws2811_state_t *s = &ws2811_state[inst];
    if (!s->initialized) return;

    if (brightness > 100) brightness = 100;
    s->brightness = brightness;
    update_pixels_impl(inst);
}

static uint8_t get_brightness_impl(int inst)
{
    return ws2811_state[inst].brightness;
}

static void set_pixel_brightnesses_impl(int inst, const uint8_t *brightness_array, uint16_t count)
{
    ws2811_state_t *s = &ws2811_state[inst];
    if (!s->initialized || !s->strip) {
        return;
    }
    if (brightness_array == NULL || count == 0) {
        return;
    }
    if (count > s->led_count) {
        count = s->led_count;
    }

    // Gamma-correct base color once at full intensity, then scale per-LED
    float gamma = get_gamma();
    float gamma_r = powf(s->r / 255.0f, gamma) * 255.0f;
    float gamma_g = powf(s->g / 255.0f, gamma) * 255.0f;
    float gamma_b = powf(s->b / 255.0f, gamma) * 255.0f;
    bool dither = get_dither_enabled();

    for (uint16_t i = 0; i < count; i++) {
        uint8_t brightness = brightness_array[i];
        if (brightness > 100) brightness = 100;

        float scale = brightness / 100.0f;
        uint8_t out_r, out_g, out_b;
        if (dither) {
            out_r = dither_quantize(gamma_r * scale, &s->dither_err_r[i]);
            out_g = dither_quantize(gamma_g * scale, &s->dither_err_g[i]);
            out_b = dither_quantize(gamma_b * scale, &s->dither_err_b[i]);
        } else {
            out_r = (uint8_t)(gamma_r * scale + 0.5f);
            out_g = (uint8_t)(gamma_g * scale + 0.5f);
            out_b = (uint8_t)(gamma_b * scale + 0.5f);
        }

        // WS2811 color order correction: swap G and B
        led_strip_set_pixel(s->strip, i, out_r, out_b, out_g);
    }

    // Fill remaining LEDs with zero if count < led_count
    for (uint16_t i = count; i < s->led_count; i++) {
        led_strip_set_pixel(s->strip, i, 0, 0, 0);
        s->dither_err_r[i] = 0.0f;
        s->dither_err_g[i] = 0.0f;
        s->dither_err_b[i] = 0.0f;
    }

    led_strip_refresh(s->strip);

    // Latch the enable gate ON the moment this animation/wave path renders any
    // nonzero pixel; never latch it back OFF from here. Per-frame wave/noise
    // content legitimately drives every LED to 0 at the trough of a low
    // base-brightness cycle, and re-deriving the enable GPIO from that per
    // frame power-cycled the whole strip's ground return in sync with the
    // wave — visible as flicker. The WS2812 protocol already renders (0,0,0)
    // as black with the gate held high, so there's no need to also cut power
    // mid-animation; only the explicit off path (set_brightness(0), via
    // update_pixels_impl) should de-assert it.
    if (array_has_nonzero(brightness_array, count) && (gamma_r > 0 || gamma_g > 0 || gamma_b > 0)) {
        set_enable(inst, true);
    }
}

// ---------------------------------------------------------------------
// Instance 1 (addressable strip 1) trampolines
// ---------------------------------------------------------------------

static esp_err_t ws2811_1_init(void)
{
    device_config_t *cfg = config_manager_get();
    bool enabled = cfg ? cfg->ws1_enabled : true;
    uint16_t count = cfg ? cfg->ws1_led_count : 30;
    return init_impl(0, CONFIG_ML_GPIO_WS1_DATA, CONFIG_ML_GPIO_WS1_ENABLE, enabled, count);
}

static void ws2811_1_set_rgb(uint8_t r, uint8_t g, uint8_t b) { set_rgb_impl(0, r, g, b); }
static void ws2811_1_set_brightness(uint8_t brightness) { set_brightness_impl(0, brightness); }
static uint8_t ws2811_1_get_brightness(void) { return get_brightness_impl(0); }
static esp_err_t ws2811_1_set_frequency(uint32_t freq_hz) { (void)freq_hz; return ESP_ERR_NOT_SUPPORTED; }
static uint32_t ws2811_1_get_frequency(void) { return 0; }
static void ws2811_1_set_pixel_brightnesses(const uint8_t *arr, uint16_t count) { set_pixel_brightnesses_impl(0, arr, count); }

// ---------------------------------------------------------------------
// Instance 2 (addressable strip 2) trampolines
// ---------------------------------------------------------------------

static esp_err_t ws2811_2_init(void)
{
    device_config_t *cfg = config_manager_get();
    bool enabled = cfg ? cfg->ws2_enabled : false;
    uint16_t count = cfg ? cfg->ws2_led_count : 30;
    return init_impl(1, CONFIG_ML_GPIO_WS2_DATA, CONFIG_ML_GPIO_WS2_ENABLE, enabled, count);
}

static void ws2811_2_set_rgb(uint8_t r, uint8_t g, uint8_t b) { set_rgb_impl(1, r, g, b); }
static void ws2811_2_set_brightness(uint8_t brightness) { set_brightness_impl(1, brightness); }
static uint8_t ws2811_2_get_brightness(void) { return get_brightness_impl(1); }
static esp_err_t ws2811_2_set_frequency(uint32_t freq_hz) { (void)freq_hz; return ESP_ERR_NOT_SUPPORTED; }
static uint32_t ws2811_2_get_frequency(void) { return 0; }
static void ws2811_2_set_pixel_brightnesses(const uint8_t *arr, uint16_t count) { set_pixel_brightnesses_impl(1, arr, count); }

// Export WS2811 driver operations
const led_driver_ops_t led_driver_ws2811_1 = {
    .init = ws2811_1_init,
    .set_rgb = ws2811_1_set_rgb,
    .set_brightness = ws2811_1_set_brightness,
    .get_brightness = ws2811_1_get_brightness,
    .set_frequency = ws2811_1_set_frequency,
    .get_frequency = ws2811_1_get_frequency,
    .set_pixel_brightnesses = ws2811_1_set_pixel_brightnesses,
};

const led_driver_ops_t led_driver_ws2811_2 = {
    .init = ws2811_2_init,
    .set_rgb = ws2811_2_set_rgb,
    .set_brightness = ws2811_2_set_brightness,
    .get_brightness = ws2811_2_get_brightness,
    .set_frequency = ws2811_2_set_frequency,
    .get_frequency = ws2811_2_get_frequency,
    .set_pixel_brightnesses = ws2811_2_set_pixel_brightnesses,
};
