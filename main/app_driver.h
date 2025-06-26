#pragma once
#include <esp_err.h>
#include <stdint.h>
#include <esp_matter_core.h> // <--- ИСПРАВЛЕНО

using esp_matter::attribute::callback_type_t;

void app_driver_init(uint16_t socket_endpoint_id);

esp_err_t app_driver_attribute_update(uint16_t endpoint_id, uint32_t cluster_id, uint32_t attribute_id,
                                      esp_matter_attr_val_t *val);