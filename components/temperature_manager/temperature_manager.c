#include <math.h>
#include <string.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "esp_log.h"
#include "esp_adc/adc_oneshot.h"
#include "onewire_bus.h"
#include "ds18b20.h"

#include "temperature_manager.h"

static const char *TAG = "temp_mgr";

// Sensor state
static float s_temperatures[TEMP_SENSOR_COUNT];
static bool s_valid[TEMP_SENSOR_COUNT];

// ADC handle for NTC
static adc_oneshot_unit_handle_t s_adc_handle;

// DS18B20 handles
static ds18b20_device_handle_t s_ds18b20[2];
static onewire_bus_handle_t s_onewire_bus[2];

static float ntc_adc_to_temperature(int adc_raw)
{
    if (adc_raw <= 0 || adc_raw >= 4095) {
        return NAN;
    }

    float r_ntc = (float)CONFIG_ML_NTC_R_PULLUP * (float)adc_raw / (4095.0f - (float)adc_raw);

    // Beta equation: 1/T = 1/T0 + (1/B) * ln(R/R0)
    float t0 = 298.15f; // 25°C in Kelvin
    float inv_t = (1.0f / t0) + (1.0f / (float)CONFIG_ML_NTC_BETA) * logf(r_ntc / (float)CONFIG_ML_NTC_R_NOMINAL);

    return (1.0f / inv_t) - 273.15f; // Convert to Celsius
}

static void init_ntc(void)
{
    adc_oneshot_unit_init_cfg_t unit_cfg = {
        .unit_id = ADC_UNIT_1,
    };
    esp_err_t ret = adc_oneshot_new_unit(&unit_cfg, &s_adc_handle);
    if (ret != ESP_OK) {
        ESP_LOGW(TAG, "Failed to init ADC unit: %s", esp_err_to_name(ret));
        return;
    }

    adc_oneshot_chan_cfg_t chan_cfg = {
        .atten = ADC_ATTEN_DB_12,
        .bitwidth = ADC_BITWIDTH_12,
    };
    ret = adc_oneshot_config_channel(s_adc_handle, ADC_CHANNEL_6, &chan_cfg);
    if (ret != ESP_OK) {
        ESP_LOGW(TAG, "Failed to configure ADC channel: %s", esp_err_to_name(ret));
        return;
    }

    ESP_LOGI(TAG, "NTC thermistor initialized on GPIO %d", CONFIG_ML_GPIO_NTC_ADC);
}

static void init_ds18b20(int index, int gpio_num)
{
    onewire_bus_config_t bus_cfg = {
        .bus_gpio_num = gpio_num,
    };
    onewire_bus_rmt_config_t rmt_cfg = {
        .max_rx_bytes = 10,
    };

    esp_err_t ret = onewire_new_bus_rmt(&bus_cfg, &rmt_cfg, &s_onewire_bus[index]);
    if (ret != ESP_OK) {
        ESP_LOGW(TAG, "DS18B20[%d]: Failed to create 1-Wire bus on GPIO %d: %s",
                 index, gpio_num, esp_err_to_name(ret));
        return;
    }

    onewire_device_iter_handle_t iter = NULL;
    ret = onewire_new_device_iter(s_onewire_bus[index], &iter);
    if (ret != ESP_OK) {
        ESP_LOGW(TAG, "DS18B20[%d]: Failed to create device iterator: %s",
                 index, esp_err_to_name(ret));
        return;
    }

    onewire_device_t device;
    ret = onewire_device_iter_get_next(iter, &device);
    if (ret != ESP_OK) {
        ESP_LOGW(TAG, "DS18B20[%d]: No device found on GPIO %d", index, gpio_num);
        onewire_del_device_iter(iter);
        return;
    }

    ds18b20_config_t ds_cfg = {};
    ret = ds18b20_new_device_from_enumeration(&device, &ds_cfg, &s_ds18b20[index]);
    if (ret != ESP_OK) {
        ESP_LOGW(TAG, "DS18B20[%d]: Failed to create device: %s",
                 index, esp_err_to_name(ret));
        onewire_del_device_iter(iter);
        return;
    }

    onewire_del_device_iter(iter);

    ds18b20_set_resolution(s_ds18b20[index], DS18B20_RESOLUTION_12B);

    ESP_LOGI(TAG, "DS18B20[%d] initialized on GPIO %d", index, gpio_num);
}

static void read_ntc(void)
{
    if (s_adc_handle == NULL) {
        return;
    }

    int adc_raw = 0;
    esp_err_t ret = adc_oneshot_read(s_adc_handle, ADC_CHANNEL_6, &adc_raw);
    if (ret != ESP_OK) {
        s_valid[TEMP_SENSOR_INTERNAL] = false;
        return;
    }

    float temp = ntc_adc_to_temperature(adc_raw);
    if (isnan(temp) || temp < -40.0f || temp > 125.0f) {
        s_valid[TEMP_SENSOR_INTERNAL] = false;
        return;
    }

    s_temperatures[TEMP_SENSOR_INTERNAL] = temp;
    s_valid[TEMP_SENSOR_INTERNAL] = true;
    ESP_LOGD(TAG, "NTC: raw=%d, temp=%.1f°C", adc_raw, temp);
}

static void read_ds18b20(int index)
{
    temp_sensor_id_t sensor_id = TEMP_SENSOR_EXTERNAL_1 + index;

    if (s_ds18b20[index] == NULL) {
        return;
    }

    esp_err_t ret = ds18b20_trigger_temperature_conversion(s_ds18b20[index]);
    if (ret != ESP_OK) {
        s_valid[sensor_id] = false;
        return;
    }

    vTaskDelay(pdMS_TO_TICKS(800));

    float temp = 0;
    ret = ds18b20_get_temperature(s_ds18b20[index], &temp);
    if (ret != ESP_OK || temp < -55.0f || temp > 125.0f) {
        s_valid[sensor_id] = false;
        return;
    }

    s_temperatures[sensor_id] = temp;
    s_valid[sensor_id] = true;
    ESP_LOGD(TAG, "DS18B20[%d]: temp=%.1f°C", index, temp);
}

static void temperature_task(void *arg)
{
    while (1) {
        TickType_t start = xTaskGetTickCount();

        read_ntc();
        read_ds18b20(0);
        read_ds18b20(1);

        // Wait for remaining interval time
        TickType_t elapsed = xTaskGetTickCount() - start;
        TickType_t interval = pdMS_TO_TICKS(CONFIG_ML_TEMP_UPDATE_INTERVAL_MS);
        if (elapsed < interval) {
            vTaskDelay(interval - elapsed);
        }
    }
}

esp_err_t temperature_manager_init(void)
{
    memset(s_temperatures, 0, sizeof(s_temperatures));
    memset(s_valid, 0, sizeof(s_valid));

    init_ntc();
    init_ds18b20(0, CONFIG_ML_GPIO_DS18B20_1);
    init_ds18b20(1, CONFIG_ML_GPIO_DS18B20_2);

    BaseType_t ret = xTaskCreate(temperature_task, "temp_task", 4096, NULL, 3, NULL);
    if (ret != pdPASS) {
        ESP_LOGE(TAG, "Failed to create temperature task");
        return ESP_FAIL;
    }

    ESP_LOGI(TAG, "Temperature manager initialized");
    return ESP_OK;
}

esp_err_t temperature_manager_get_temperature(temp_sensor_id_t sensor_id, float *temperature)
{
    if (sensor_id >= TEMP_SENSOR_COUNT || temperature == NULL) {
        return ESP_ERR_INVALID_ARG;
    }

    if (!s_valid[sensor_id]) {
        return ESP_ERR_NOT_FOUND;
    }

    *temperature = s_temperatures[sensor_id];
    return ESP_OK;
}

bool temperature_manager_is_valid(temp_sensor_id_t sensor_id)
{
    if (sensor_id >= TEMP_SENSOR_COUNT) {
        return false;
    }
    return s_valid[sensor_id];
}
