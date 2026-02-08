#ifndef TEMPERATURE_MANAGER_H
#define TEMPERATURE_MANAGER_H

#include <stdbool.h>
#include "esp_err.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef enum {
    TEMP_SENSOR_INTERNAL = 0,   // NTC on GPIO 34
    TEMP_SENSOR_EXTERNAL_1,     // DS18B20 on GPIO 15
    TEMP_SENSOR_EXTERNAL_2,     // DS18B20 on GPIO 16
    TEMP_SENSOR_COUNT
} temp_sensor_id_t;

esp_err_t temperature_manager_init(void);
esp_err_t temperature_manager_get_temperature(temp_sensor_id_t sensor_id, float *temperature);
bool temperature_manager_is_valid(temp_sensor_id_t sensor_id);

#ifdef __cplusplus
}
#endif

#endif // TEMPERATURE_MANAGER_H
