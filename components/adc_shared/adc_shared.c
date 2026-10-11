#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"
#include "esp_log.h"
#include "adc_shared.h"

static const char *TAG = "adc_shared";

static adc_oneshot_unit_handle_t s_handle;
static SemaphoreHandle_t s_mutex;

esp_err_t adc_shared_init(void)
{
    s_mutex = xSemaphoreCreateMutex();
    if (s_mutex == NULL) {
        ESP_LOGE(TAG, "Failed to create mutex");
        return ESP_ERR_NO_MEM;
    }

    adc_oneshot_unit_init_cfg_t unit_cfg = {
        .unit_id = ADC_UNIT_1,
    };
    esp_err_t ret = adc_oneshot_new_unit(&unit_cfg, &s_handle);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Failed to init shared ADC unit: %s", esp_err_to_name(ret));
        return ret;
    }

    ESP_LOGI(TAG, "Shared ADC_UNIT_1 initialized");
    return ESP_OK;
}

esp_err_t adc_shared_config_channel(adc_channel_t channel, adc_atten_t atten)
{
    if (s_handle == NULL) {
        return ESP_ERR_INVALID_STATE;
    }

    adc_oneshot_chan_cfg_t chan_cfg = {
        .atten = atten,
        .bitwidth = ADC_BITWIDTH_12,
    };
    return adc_oneshot_config_channel(s_handle, channel, &chan_cfg);
}

esp_err_t adc_shared_read(adc_channel_t channel, int *raw_out)
{
    if (s_handle == NULL || raw_out == NULL) {
        return ESP_ERR_INVALID_STATE;
    }

    xSemaphoreTake(s_mutex, portMAX_DELAY);
    esp_err_t ret = adc_oneshot_read(s_handle, channel, raw_out);
    xSemaphoreGive(s_mutex);

    return ret;
}
