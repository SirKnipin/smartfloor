#include <esp_err.h>
#include <esp_log.h>
#include <nvs_flash.h>

#include <esp_matter.h>
#include <esp_matter_console.h>

#include <common_macros.h>
#include <app_priv.h>
#include <app_reset.h>
#include <app_driver.h> // Подключаем наш обновленный драйвер

#include <app/server/CommissioningWindowManager.h>
#include <app/server/Server.h>

#include "app_driver_temp_sensor.h"
#include "app_driver_heater.h"
#include "pid_controller.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

static const char *TAG = "app_main";

// --- ГЛОБАЛЬНЫЕ ПЕРЕМЕННЫЕ ---
uint16_t floor_endpoint_id = 1;  // ID для теплого пола ("лампочки")
uint16_t socket_endpoint_id = 0; // ID для розетки

static float g_target_temperature = 0.0f;
static pid_controller_t g_pid;
#define TEMP_CONTROL_TASK_PERIOD_MS 1000

using namespace esp_matter;
using namespace esp_matter::attribute;
using namespace esp_matter::endpoint;
using namespace chip::app::Clusters;

// --- ЗАДАЧА УПРАВЛЕНИЯ ТЕМПЕРАТУРОЙ (С АВАРИЙНЫМ ОТКЛЮЧЕНИЕМ) ---
static void temp_control_task(void *arg)
{
    while (true) {
        float current_temp = 0.0f;
        if (app_temp_sensor_read(&current_temp) == ESP_OK) {
            // --- АВАРИЙНОЕ ОТКЛЮЧЕНИЕ ПРИ ПЕРЕГРЕВЕ ---
            if (current_temp >= 95.0f && g_target_temperature > 0.0f) {
                g_target_temperature = 0.0f;
                ESP_LOGE(TAG, "EMERGENCY SHUTDOWN! Temperature %.2f C >= 95 C. Turning off heating.", current_temp);

                // Обновляем состояние "лампочки" в Matter, чтобы Алиса видела, что она выключилась
                esp_matter_attr_val_t on_val = esp_matter_bool(false);
                attribute::update(floor_endpoint_id, OnOff::Id, OnOff::Attributes::OnOff::Id, &on_val);
                esp_matter_attr_val_t level_val = esp_matter_uint8(0);
                attribute::update(floor_endpoint_id, LevelControl::Id, LevelControl::Attributes::CurrentLevel::Id, &level_val);
            }
            // ------------------------------------------

            float power;
            if (g_target_temperature == 0.0f) {
                power = 0.0f;
            } else {
                power = pid_compute(&g_pid, g_target_temperature, current_temp);
            }

            app_heater_set_power(power);
            ESP_LOGI("temp_ctrl", "T=%.2f C -> power=%.1f%% (target=%.1f C)", current_temp, power, g_target_temperature);
        } else {
            ESP_LOGW("temp_ctrl", "Sensor read failed");
        }
        vTaskDelay(pdMS_TO_TICKS(TEMP_CONTROL_TASK_PERIOD_MS));
    }
}

// --- ОБРАБОТЧИК СОБЫТИЙ MATTER (С ПЕЧАТЬЮ КОДА) ---
static void app_event_cb(const ChipDeviceEvent *event, intptr_t arg)
{
    switch (event->Type) {
        case chip::DeviceLayer::DeviceEventType::kCommissioningWindowOpened:
            ESP_LOGI(TAG, "Commissioning window opened");
           // esp_matter::console::print_onboarding_payload(); // Печатаем код подключения
            break;
        // Другие события можно оставить для отладки
        default:
            break;
    }
}

static esp_err_t app_identification_cb(identification::callback_type_t type, uint16_t endpoint_id, uint8_t effect_id,
                                       uint8_t effect_variant, void *priv_data)
{
    ESP_LOGI(TAG, "Identification callback: type: %u, endpoint: %d", type, endpoint_id);
    return ESP_OK;
}

// --- ГЛАВНЫЙ ОБРАБОТЧИК КОМАНД ОТ АЛИСЫ ---
static esp_err_t app_attribute_update_cb(attribute::callback_type_t type, uint16_t endpoint_id, uint32_t cluster_id,
                                         uint32_t attribute_id, esp_matter_attr_val_t *val, void *priv_data)
{
    if (type == PRE_UPDATE) {
        // --- Логика для Теплого Пола ("Лампочки") ---
        if (endpoint_id == floor_endpoint_id) {
            if (cluster_id == OnOff::Id && attribute_id == OnOff::Attributes::OnOff::Id) {
                if (val->val.b == false) { // Если пришла команда "Выключить"
                    g_target_temperature = 0.0f;
                    ESP_LOGI(TAG, "Floor heating OFF");
                } else { // Если пришла команда "Включить"
                    esp_matter_attr_val_t current_level;
                    attribute_t *level_attr = attribute::get(floor_endpoint_id, LevelControl::Id, LevelControl::Attributes::CurrentLevel::Id);
                    attribute::get_val(level_attr, &current_level);
                    if (current_level.val.u8 == 0) { // Если яркость была 0, ставим хотя бы минимум
                        current_level.val.u8 = 1;
                        attribute::update(floor_endpoint_id, LevelControl::Id, LevelControl::Attributes::CurrentLevel::Id, &current_level);
                    }
                    g_target_temperature = 15.0f + ((float)current_level.val.u8 / 254.0f) * 30.0f;
                    ESP_LOGI(TAG, "Floor heating ON, target temp restored to %.2f C", g_target_temperature);
                }
            } else if (cluster_id == LevelControl::Id && attribute_id == LevelControl::Attributes::CurrentLevel::Id) {
                // Команда "Яркость на X%"
                if (val->val.u8 == 0) {
                    g_target_temperature = 0.0f;
                } else {
                    g_target_temperature = 15.0f + ((float)val->val.u8 / 254.0f) * 30.0f;
                }
                ESP_LOGI(TAG, "New target temperature: %.2f C (from brightness %d)", g_target_temperature, val->val.u8);
            }
        }
        // --- Логика для Розетки (передаем в app_driver) ---
        else if (endpoint_id == socket_endpoint_id) {
            app_driver_attribute_update(endpoint_id, cluster_id, attribute_id, val);
        }
    }
    return ESP_OK;
}

// --- ГЛАВНАЯ ФУНКЦИЯ ---
extern "C" void app_main()
{
    esp_err_t err = ESP_OK;
    // Инициализация
    nvs_flash_init();
    app_temp_sensor_init();
    app_heater_init();
    pid_init(&g_pid, 5.0f, 0.5f, 1.0f, 0.0f, 100.0f);
    xTaskCreate(temp_control_task, "temp_control_task", 4096, NULL, 5, NULL);

    // Создание узла Matter
    node::config_t node_config;
    node_t *node = node::create(&node_config, app_attribute_update_cb, app_identification_cb);
    ABORT_APP_ON_FAILURE(node != nullptr, ESP_LOGE(TAG, "Failed to create Matter node"));

    // --- СОЗДАЕМ ТЕПЛЫЙ ПОЛ (КАК ДИММИРУЕМУЮ ЛАМПОЧКУ) ---
    dimmable_light::config_t floor_config;
    floor_config.on_off.on_off = false; // По умолчанию выключен
    floor_config.level_control.current_level = nullptr; // По умолчанию 0%
    endpoint_t *floor_ep = dimmable_light::create(node, &floor_config, ENDPOINT_FLAG_NONE, nullptr);
    ABORT_APP_ON_FAILURE(floor_ep != nullptr, ESP_LOGE(TAG, "Failed to create Dimmable Light (Floor) endpoint"));
    floor_endpoint_id = endpoint::get_id(floor_ep);
    ESP_LOGI(TAG, "Floor (as Dimmable Light) created with endpoint_id %d", floor_endpoint_id);

    // --- СОЗДАЕМ РОЗЕТКУ ---
    on_off_plugin_unit::config_t socket_config;
    socket_config.on_off.on_off = false; // По умолчанию выключена
    endpoint_t *socket_ep = on_off_plugin_unit::create(node, &socket_config, ENDPOINT_FLAG_NONE, nullptr);
    ABORT_APP_ON_FAILURE(socket_ep != nullptr, ESP_LOGE(TAG, "Failed to create On/Off Plugin (Socket) endpoint"));
    socket_endpoint_id = endpoint::get_id(socket_ep);
    ESP_LOGI(TAG, "Socket (as On/Off Plugin) created with endpoint_id %d", socket_endpoint_id);

    // Инициализируем наш драйвер, передавая ему ID розетки
    app_driver_init(socket_endpoint_id);

    // Запуск Matter
    err = esp_matter::start(app_event_cb);
    ABORT_APP_ON_FAILURE(err == ESP_OK, ESP_LOGE(TAG, "Failed to start Matter, err:%d", err));
} 