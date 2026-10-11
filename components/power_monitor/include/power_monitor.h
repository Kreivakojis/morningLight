#ifndef POWER_MONITOR_H
#define POWER_MONITOR_H

#include "esp_err.h"

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief Initialize power monitoring (AC CT current, DC current, DC voltage)
 *
 * Non-fatal: the device operates normally without sensors attached, readings
 * simply stay unavailable (ESP_ERR_NOT_FOUND from the getters).
 *
 * @return ESP_OK on success
 */
esp_err_t power_monitor_init(void);

/**
 * @brief Get the AC current transformer RMS current reading
 *
 * @param amps Output current in Amps RMS
 * @return ESP_OK on success, ESP_ERR_NOT_FOUND if no reading yet
 */
esp_err_t power_monitor_get_ac_current(float *amps);

/**
 * @brief Get the DC shunt/op-amp current reading
 *
 * @param amps Output current in Amps
 * @return ESP_OK on success, ESP_ERR_NOT_FOUND if no reading yet
 */
esp_err_t power_monitor_get_dc_current(float *amps);

/**
 * @brief Get the DC voltage divider reading
 *
 * @param volts Output voltage in Volts
 * @return ESP_OK on success, ESP_ERR_NOT_FOUND if no reading yet
 */
esp_err_t power_monitor_get_dc_voltage(float *volts);

#ifdef __cplusplus
}
#endif

#endif // POWER_MONITOR_H
