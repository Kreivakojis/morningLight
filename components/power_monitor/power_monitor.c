#include <math.h>
#include <string.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "esp_log.h"
#include "esp_timer.h"
#include "adc_shared.h"

#include "power_monitor.h"
#include "config_manager.h"

static const char *TAG = "power_mon";

#define ADC_VREF_V      3.3f
#define ADC_MAX_COUNTS  4095.0f
#define DC_OVERSAMPLES  64

// Raw (pre-calibration) readings; calibration is applied at read time so
// settings changes take effect on the next getter call, not the next poll.
static float s_ac_vrms;
static float s_dc_current_v;
static float s_dc_voltage_v;
static bool s_ac_valid;
static bool s_dc_current_valid;
static bool s_dc_voltage_valid;

static bool s_adc_ready;

static void sample_ac_current(void)
{
    int64_t deadline = esp_timer_get_time() + (int64_t)CONFIG_ML_AC_SAMPLE_WINDOW_MS * 1000;
    double sum = 0.0;
    double sumsq = 0.0;
    uint32_t count = 0;

    while (esp_timer_get_time() < deadline) {
        int raw = 0;
        if (adc_shared_read(ADC_CHANNEL_7, &raw) != ESP_OK) {
            continue;
        }
        sum += raw;
        sumsq += (double)raw * (double)raw;
        count++;
    }

    if (count == 0) {
        s_ac_valid = false;
        return;
    }

    double mean = sum / count;
    double variance = (sumsq / count) - (mean * mean);
    if (variance < 0.0) {
        variance = 0.0;
    }
    double rms_counts = sqrt(variance);

    s_ac_vrms = (float)(rms_counts * (ADC_VREF_V / ADC_MAX_COUNTS));
    s_ac_valid = true;
    ESP_LOGD(TAG, "AC CT: %lu samples, rms_counts=%.1f, vrms=%.4f", (unsigned long)count, rms_counts, s_ac_vrms);
}

static bool oversample_channel(adc_channel_t channel, float *volts_out)
{
    int64_t sum = 0;
    uint32_t count = 0;

    for (int i = 0; i < DC_OVERSAMPLES; i++) {
        int raw = 0;
        if (adc_shared_read(channel, &raw) != ESP_OK) {
            continue;
        }
        sum += raw;
        count++;
    }

    if (count == 0) {
        return false;
    }

    float avg = (float)sum / (float)count;
    *volts_out = avg * (ADC_VREF_V / ADC_MAX_COUNTS);
    return true;
}

static void sample_dc_current(void)
{
    float volts;
    if (!oversample_channel(ADC_CHANNEL_6, &volts)) {
        s_dc_current_valid = false;
        return;
    }
    s_dc_current_v = volts;
    s_dc_current_valid = true;
}

static void sample_dc_voltage(void)
{
    float volts;
    if (!oversample_channel(ADC_CHANNEL_3, &volts)) {
        s_dc_voltage_valid = false;
        return;
    }
    s_dc_voltage_v = volts;
    s_dc_voltage_valid = true;
}

static void power_task(void *arg)
{
    while (1) {
        TickType_t start = xTaskGetTickCount();

        if (s_adc_ready) {
            sample_ac_current();
            sample_dc_current();
            sample_dc_voltage();
        }

        TickType_t elapsed = xTaskGetTickCount() - start;
        TickType_t interval = pdMS_TO_TICKS(CONFIG_ML_POWER_UPDATE_INTERVAL_MS);
        if (elapsed < interval) {
            vTaskDelay(interval - elapsed);
        }
    }
}

esp_err_t power_monitor_init(void)
{
    esp_err_t ret = adc_shared_config_channel(ADC_CHANNEL_7, ADC_ATTEN_DB_12);
    if (ret != ESP_OK) {
        ESP_LOGW(TAG, "Failed to configure AC CT channel: %s", esp_err_to_name(ret));
        return ESP_OK; // non-fatal, device still boots without this sensor
    }
    ret = adc_shared_config_channel(ADC_CHANNEL_6, ADC_ATTEN_DB_12);
    if (ret != ESP_OK) {
        ESP_LOGW(TAG, "Failed to configure DC current channel: %s", esp_err_to_name(ret));
        return ESP_OK;
    }
    ret = adc_shared_config_channel(ADC_CHANNEL_3, ADC_ATTEN_DB_12);
    if (ret != ESP_OK) {
        ESP_LOGW(TAG, "Failed to configure DC voltage channel: %s", esp_err_to_name(ret));
        return ESP_OK;
    }

    s_adc_ready = true;

    BaseType_t task_ret = xTaskCreate(power_task, "power_task", 4096, NULL, 3, NULL);
    if (task_ret != pdPASS) {
        ESP_LOGE(TAG, "Failed to create power monitor task");
        return ESP_FAIL;
    }

    ESP_LOGI(TAG, "Power monitor initialized (AC CT GPIO %d, DC current GPIO %d, DC voltage GPIO %d)",
             CONFIG_ML_GPIO_CT_AC_ADC, CONFIG_ML_GPIO_DC_CURRENT_ADC, CONFIG_ML_GPIO_DC_VOLTAGE_ADC);
    return ESP_OK;
}

esp_err_t power_monitor_get_ac_current(float *amps)
{
    if (amps == NULL) {
        return ESP_ERR_INVALID_ARG;
    }
    if (!s_ac_valid) {
        return ESP_ERR_NOT_FOUND;
    }

    device_config_t *cfg = config_manager_get();
    float cal = cfg ? cfg->ac_current_cal : 0.0f;
    *amps = s_ac_vrms * cal;
    return ESP_OK;
}

esp_err_t power_monitor_get_dc_current(float *amps)
{
    if (amps == NULL) {
        return ESP_ERR_INVALID_ARG;
    }
    if (!s_dc_current_valid) {
        return ESP_ERR_NOT_FOUND;
    }

    device_config_t *cfg = config_manager_get();
    float scale = cfg ? cfg->dc_current_scale : 0.0f;
    float offset = cfg ? cfg->dc_current_offset_v : 0.0f;
    *amps = (s_dc_current_v - offset) * scale;
    return ESP_OK;
}

esp_err_t power_monitor_get_dc_voltage(float *volts)
{
    if (volts == NULL) {
        return ESP_ERR_INVALID_ARG;
    }
    if (!s_dc_voltage_valid) {
        return ESP_ERR_NOT_FOUND;
    }

    device_config_t *cfg = config_manager_get();
    float scale = cfg ? cfg->dc_voltage_scale : 0.0f;
    *volts = s_dc_voltage_v * scale;
    return ESP_OK;
}
