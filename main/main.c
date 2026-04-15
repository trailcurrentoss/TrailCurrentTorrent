#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "esp_log.h"
#include "wifi_config.h"
#include "ota.h"
#include "discovery.h"
#include "can_handler.h"
#include "pwm_output.h"

static const char *TAG = "torrent";

#ifndef TORRENT_ADDRESS
#define TORRENT_ADDRESS 0
#endif

void app_main(void)
{
    ESP_LOGI(TAG, "=== TrailCurrent Torrent (addr %d) ===", TORRENT_ADDRESS);
    ESP_LOGI(TAG, "8-Channel PWM Lighting Control Module");

    wifi_config_init();

    char ssid[33], password[64];
    if (wifi_config_load(ssid, sizeof(ssid), password, sizeof(password))) {
        ESP_LOGI(TAG, "WiFi credentials loaded (SSID: %s)", ssid);
    } else {
        ESP_LOGW(TAG, "No WiFi credentials — provision via CAN ID 0x01");
    }

    ota_init();
    discovery_init();

    ESP_LOGI(TAG, "Hostname: %s", wifi_config_get_hostname());

    pwm_output_init();

    ESP_ERROR_CHECK(can_handler_init());

    ESP_LOGI(TAG, "=== Setup Complete ===");

    xTaskCreatePinnedToCore(can_handler_task, "can_task", 4096, NULL, 5, NULL, 1);

    while (1) {
        vTaskDelay(pdMS_TO_TICKS(1000));
    }
}
