#include <esp_log.h>
#include <freertos/FreeRTOS.h> // <--- ИСПРАВЛЕНО
#include <freertos/task.h>
#include <driver/gpio.h>

#include <app_driver.h>

using namespace esp_matter;
using namespace esp_matter::attribute;
using namespace esp_matter::endpoint;
using namespace chip::app::Clusters;

static const char *TAG = "app_driver";

#define SOCKET_RELAY_PIN GPIO_NUM_4 

static uint16_t g_socket_endpoint_id = 0;

void app_driver_init(uint16_t socket_endpoint_id)
{
    g_socket_endpoint_id = socket_endpoint_id;
    gpio_reset_pin(SOCKET_RELAY_PIN);
    gpio_set_direction(SOCKET_RELAY_PIN, GPIO_MODE_OUTPUT);
    gpio_set_level(SOCKET_RELAY_PIN, false); 
    ESP_LOGI(TAG, "App driver initialized for Socket on GPIO %d", SOCKET_RELAY_PIN);
}

esp_err_t app_driver_attribute_update(uint16_t endpoint_id, uint32_t cluster_id, uint32_t attribute_id,
                                      esp_matter_attr_val_t *val)
{
    if (endpoint_id == g_socket_endpoint_id) {
        if (cluster_id == OnOff::Id && attribute_id == OnOff::Attributes::OnOff::Id) { // <--- ИСПРАВЛЕНО
            ESP_LOGI(TAG, "Socket command received: %s", val->val.b ? "ON" : "OFF");
            gpio_set_level(SOCKET_RELAY_PIN, val->val.b);
        }
    }
    return ESP_OK;
}