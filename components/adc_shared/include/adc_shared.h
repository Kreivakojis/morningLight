#ifndef ADC_SHARED_H
#define ADC_SHARED_H

#include "esp_err.h"
#include "esp_adc/adc_oneshot.h"

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief Initialize the shared ADC_UNIT_1 oneshot driver
 *
 * ADC_UNIT_1 can only be claimed by one adc_oneshot handle at a time, but
 * multiple components (temperature_manager, power_monitor) need channels
 * on it. This component owns the single handle and mutex so callers never
 * touch adc_oneshot_new_unit() directly.
 *
 * @return ESP_OK on success
 */
esp_err_t adc_shared_init(void);

/**
 * @brief Configure a channel on the shared ADC_UNIT_1 handle
 *
 * @param channel ADC1 channel to configure
 * @param atten Attenuation to use for this channel
 * @return ESP_OK on success
 */
esp_err_t adc_shared_config_channel(adc_channel_t channel, adc_atten_t atten);

/**
 * @brief Read a raw sample from a channel on the shared handle
 *
 * Mutex-protected so concurrent readers (different tasks) can't interleave
 * reads on the shared unit.
 *
 * @param channel ADC1 channel to read
 * @param raw_out Output raw ADC value
 * @return ESP_OK on success
 */
esp_err_t adc_shared_read(adc_channel_t channel, int *raw_out);

#ifdef __cplusplus
}
#endif

#endif // ADC_SHARED_H
