#pragma once

#include "esp_err.h"

#ifdef __cplusplus
extern "C" {
#endif

esp_err_t app_temp_sensor_init(void);
esp_err_t app_temp_sensor_read(float *temperature_celsius);

#ifdef __cplusplus
}
#endif
