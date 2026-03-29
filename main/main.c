#include <stdio.h>
#include <string.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "driver/ledc.h"
#include "driver/twai.h"
#include "esp_log.h"
#include "esp_timer.h"
#include "esp_app_desc.h"
#include "esp_mac.h"
#include "wifi_config.h"
#include "ota.h"
#include "discovery.h"

static const char *TAG = "torrent";

// =============================================================================
// Module address (0-2) — set at build time: idf.py build -DTORRENT_ADDRESS=1
// =============================================================================

#ifndef TORRENT_ADDRESS
#define TORRENT_ADDRESS 0
#endif
#if TORRENT_ADDRESS < 0 || TORRENT_ADDRESS > 2
#error "TORRENT_ADDRESS must be 0-2"
#endif

// =============================================================================
// Pin Definitions — ESP32-WROOM-32 8-Channel PWM Output Module
// =============================================================================

// CAN bus pins
#define CAN_TX_PIN   15
#define CAN_RX_PIN   13

// 8 PWM output pins
static const int OUTPUT_PINS[8] = {
    32,  // OUTPUT01
    33,  // OUTPUT02
    26,  // OUTPUT03
    14,  // OUTPUT04
     4,  // OUTPUT05
    17,  // OUTPUT06
    19,  // OUTPUT07
    23,  // OUTPUT08
};

#define NUM_OUTPUTS 8

// =============================================================================
// CAN Bus Configuration
// =============================================================================

// CAN protocol IDs — shared (no address offset)
#define CAN_ID_OTA              0x00
#define CAN_ID_WIFI_CONFIG      0x01
#define CAN_ID_DISCOVERY_TRIGGER 0x02

// CAN ID bases — each instance offsets by TORRENT_ADDRESS
#define CAN_ID_BRIGHTNESS_BASE  0x15
#define CAN_ID_TOGGLE_BASE      0x18
#define CAN_ID_STATUS_BASE      0x1B
#define CAN_ID_SEQUENCE_BASE    0x33

#define CAN_ID_BRIGHTNESS   (CAN_ID_BRIGHTNESS_BASE + TORRENT_ADDRESS)
#define CAN_ID_TOGGLE       (CAN_ID_TOGGLE_BASE + TORRENT_ADDRESS)
#define CAN_STATUS_ID       (CAN_ID_STATUS_BASE + TORRENT_ADDRESS)
#define CAN_ID_SEQUENCE     (CAN_ID_SEQUENCE_BASE + TORRENT_ADDRESS)

#define CAN_BAUDRATE        500000
#define STATUS_TX_INTERVAL_MS  33   // ~30 Hz
#define TX_PROBE_INTERVAL_MS  2000 // slow probe when no peers detected

// =============================================================================
// LEDC PWM Configuration
// =============================================================================

#define LEDC_TIMER          LEDC_TIMER_0
#define LEDC_MODE           LEDC_LOW_SPEED_MODE
#define LEDC_DUTY_RES       LEDC_TIMER_8_BIT   // 0-255
#define LEDC_FREQUENCY      5000                // 5 kHz

// =============================================================================
// Global State
// =============================================================================

static uint8_t s_light_values[NUM_OUTPUTS] = {0};

// =============================================================================
// LEDC PWM Helpers
// =============================================================================

static void pwm_init(void)
{
    ledc_timer_config_t timer_cfg = {
        .speed_mode      = LEDC_MODE,
        .duty_resolution = LEDC_DUTY_RES,
        .timer_num       = LEDC_TIMER,
        .freq_hz         = LEDC_FREQUENCY,
        .clk_cfg         = LEDC_AUTO_CLK,
    };
    ledc_timer_config(&timer_cfg);

    for (int i = 0; i < NUM_OUTPUTS; i++) {
        ledc_channel_config_t ch_cfg = {
            .gpio_num   = OUTPUT_PINS[i],
            .speed_mode = LEDC_MODE,
            .channel    = (ledc_channel_t)i,
            .timer_sel  = LEDC_TIMER,
            .duty       = 0,
            .hpoint     = 0,
        };
        ledc_channel_config(&ch_cfg);
    }

    ESP_LOGI(TAG, "LEDC PWM initialized: 8 channels at %d Hz", LEDC_FREQUENCY);
}

static void pwm_set(int channel, uint8_t duty)
{
    if (channel < 0 || channel >= NUM_OUTPUTS) return;
    s_light_values[channel] = duty;
    ledc_set_duty(LEDC_MODE, (ledc_channel_t)channel, duty);
    ledc_update_duty(LEDC_MODE, (ledc_channel_t)channel);
}

static void pwm_set_all(uint8_t duty)
{
    for (int i = 0; i < NUM_OUTPUTS; i++) {
        pwm_set(i, duty);
    }
}

// =============================================================================
// Light Sequences (blocking — run from CAN task context)
// =============================================================================

static void light_sequence_startup(void)
{
    pwm_set_all(0);
    vTaskDelay(pdMS_TO_TICKS(500));

    // Section 1: Progressive wave
    for (int wave = 0; wave < 3; wave++) {
        for (int i = 0; i < NUM_OUTPUTS; i++) {
            pwm_set(i, 255);
            vTaskDelay(pdMS_TO_TICKS(100));
            pwm_set(i, 0);
        }
    }
    vTaskDelay(pdMS_TO_TICKS(300));

    // Section 2: All lights pulse together
    for (int pulse = 0; pulse < 4; pulse++) {
        for (int v = 0; v <= 255; v += 25) {
            pwm_set_all((uint8_t)v);
            vTaskDelay(pdMS_TO_TICKS(30));
        }
        for (int v = 255; v >= 0; v -= 25) {
            pwm_set_all((uint8_t)v);
            vTaskDelay(pdMS_TO_TICKS(30));
        }
    }
    vTaskDelay(pdMS_TO_TICKS(300));

    // Section 3: Alternating pattern
    for (int alt = 0; alt < 5; alt++) {
        for (int i = 0; i < NUM_OUTPUTS; i++) {
            pwm_set(i, (i % 2 == 0) ? 255 : 0);
        }
        vTaskDelay(pdMS_TO_TICKS(200));
        for (int i = 0; i < NUM_OUTPUTS; i++) {
            pwm_set(i, (i % 2 == 1) ? 255 : 0);
        }
        vTaskDelay(pdMS_TO_TICKS(200));
        pwm_set_all(0);
        vTaskDelay(pdMS_TO_TICKS(100));
    }
    vTaskDelay(pdMS_TO_TICKS(300));

    // Section 4: Spiral pattern
    for (int spiral = 0; spiral < 3; spiral++) {
        for (int i = 0; i < NUM_OUTPUTS; i++) {
            pwm_set(i, 255);
            vTaskDelay(pdMS_TO_TICKS(80));
        }
        vTaskDelay(pdMS_TO_TICKS(200));
        for (int i = NUM_OUTPUTS - 1; i >= 0; i--) {
            pwm_set(i, 0);
            vTaskDelay(pdMS_TO_TICKS(80));
        }
        vTaskDelay(pdMS_TO_TICKS(200));
    }
    vTaskDelay(pdMS_TO_TICKS(300));

    // Section 5: Rapid flash finale
    for (int flash = 0; flash < 8; flash++) {
        pwm_set_all(255);
        vTaskDelay(pdMS_TO_TICKS(200));
        pwm_set_all(0);
        vTaskDelay(pdMS_TO_TICKS(200));
    }

    pwm_set_all(0);
}

static void light_sequence_interior(void)
{
    // Channels 4-7 (OUTPUT05-OUTPUT08)
    for (int i = 4; i < 8; i++) pwm_set(i, 0);

    // Fade in
    for (int v = 0; v <= 255; v++) {
        for (int i = 4; i < 8; i++) pwm_set(i, (uint8_t)v);
        vTaskDelay(pdMS_TO_TICKS(10));
    }
    // Fade out
    for (int v = 255; v >= 0; v--) {
        for (int i = 4; i < 8; i++) pwm_set(i, (uint8_t)v);
        vTaskDelay(pdMS_TO_TICKS(10));
    }

    // Rotating pattern
    for (int cycle = 0; cycle <= 30; cycle++) {
        for (int step = 0; step < 4; step++) {
            for (int i = 4; i < 8; i++) {
                pwm_set(i, (i - 4 == step) ? 255 : 0);
            }
            vTaskDelay(pdMS_TO_TICKS(60));
        }
        for (int step = 2; step >= 1; step--) {
            for (int i = 4; i < 8; i++) {
                pwm_set(i, (i - 4 == step) ? 255 : 0);
            }
            vTaskDelay(pdMS_TO_TICKS(60));
        }
        pwm_set(7, 255);
        for (int i = 4; i < 7; i++) pwm_set(i, 0);
        vTaskDelay(pdMS_TO_TICKS(60));
    }
    for (int i = 4; i < 8; i++) pwm_set(i, 0);
}

static void light_sequence_exterior(void)
{
    // Channels 2-3 (OUTPUT03-OUTPUT04)
    pwm_set(2, 0);
    pwm_set(3, 0);

    // Fade in
    for (int v = 0; v <= 255; v++) {
        pwm_set(2, (uint8_t)v);
        pwm_set(3, (uint8_t)v);
        vTaskDelay(pdMS_TO_TICKS(10));
    }
    // Fade out
    for (int v = 255; v >= 0; v--) {
        pwm_set(2, (uint8_t)v);
        pwm_set(3, (uint8_t)v);
        vTaskDelay(pdMS_TO_TICKS(10));
    }

    // Alternating pattern
    for (int cycle = 0; cycle <= 30; cycle++) {
        pwm_set(2, 255);
        pwm_set(3, 0);
        vTaskDelay(pdMS_TO_TICKS(250));
        pwm_set(2, 0);
        pwm_set(3, 255);
        vTaskDelay(pdMS_TO_TICKS(250));
    }
    pwm_set(2, 0);
    pwm_set(3, 0);
}

// =============================================================================
// CAN Message Handlers
// =============================================================================

static void handle_toggle(const uint8_t *data, uint8_t len)
{
    if (len < 1) return;
    uint8_t ch = data[0];

    if (ch < NUM_OUTPUTS) {
        // Toggle single channel
        pwm_set(ch, s_light_values[ch] > 0 ? 0 : 255);
    } else if (ch == 8) {
        // All off or all on based on data[1]
        uint8_t val = (len >= 2 && data[1] == 0) ? 0 : 255;
        pwm_set_all(val);
    } else if (ch == 9) {
        // All on if data[1] == 1
        if (len >= 2 && data[1] == 1) {
            pwm_set_all(255);
        }
        // Apply current values to outputs (refresh)
        for (int i = 0; i < NUM_OUTPUTS; i++) {
            pwm_set(i, s_light_values[i]);
        }
    }
}

static void handle_brightness(const uint8_t *data, uint8_t len)
{
    if (len < 2) return;
    uint8_t ch = data[0];
    uint8_t duty = data[1];

    if (ch < NUM_OUTPUTS) {
        pwm_set(ch, duty);
    }
}

static void handle_sequence(const uint8_t *data, uint8_t len)
{
    if (len < 1) return;

    if (data[0] == 0) {
        light_sequence_interior();
    } else if (data[0] == 1) {
        light_sequence_exterior();
    }
}

// =============================================================================
// TWAI (CAN) task — runs independently
// =============================================================================

static void twai_task(void *arg)
{
    twai_general_config_t g_config = TWAI_GENERAL_CONFIG_DEFAULT(
        (gpio_num_t)CAN_TX_PIN, (gpio_num_t)CAN_RX_PIN, TWAI_MODE_NORMAL);
    twai_timing_config_t t_config = TWAI_TIMING_CONFIG_500KBITS();
    twai_filter_config_t f_config = TWAI_FILTER_CONFIG_ACCEPT_ALL();

    if (twai_driver_install(&g_config, &t_config, &f_config) != ESP_OK) {
        ESP_LOGE(TAG, "Failed to install TWAI driver");
        vTaskDelete(NULL);
        return;
    }
    if (twai_start() != ESP_OK) {
        ESP_LOGE(TAG, "Failed to start TWAI driver");
        vTaskDelete(NULL);
        return;
    }

    // Broadcast firmware version on CAN 0x04 at startup
    {
        uint8_t mac[6];
        esp_read_mac(mac, ESP_MAC_WIFI_STA);
        const esp_app_desc_t *app = esp_app_get_description();
        unsigned maj = 0, min = 0, pat = 0;
        sscanf(app->version, "%u.%u.%u", &maj, &min, &pat);
        twai_message_t ver_msg = {
            .identifier = 0x04,
            .data_length_code = 6,
            .data = { mac[3], mac[4], mac[5], maj, min, pat }
        };
        twai_transmit(&ver_msg, pdMS_TO_TICKS(50));
        ESP_LOGI(TAG, "Version broadcast: %s (CAN 0x04)", app->version);
    }

    uint32_t alerts = TWAI_ALERT_RX_DATA | TWAI_ALERT_ERR_PASS |
                      TWAI_ALERT_BUS_ERROR | TWAI_ALERT_RX_QUEUE_FULL |
                      TWAI_ALERT_BUS_OFF | TWAI_ALERT_BUS_RECOVERED |
                      TWAI_ALERT_ERR_ACTIVE | TWAI_ALERT_TX_FAILED |
                      TWAI_ALERT_TX_SUCCESS;
    twai_reconfigure_alerts(alerts, NULL);
    ESP_LOGI(TAG, "TWAI driver started (NORMAL mode, 500 kbps)");

    typedef enum { TX_ACTIVE, TX_PROBING } tx_state_t;
    bool bus_off = false;
    tx_state_t tx_state = TX_ACTIVE;
    int tx_fail_count = 0;
    const int TX_FAIL_THRESHOLD = 3;
    int64_t last_tx_us = 0;
    const int64_t tx_period_us = STATUS_TX_INTERVAL_MS * 1000LL;
    const int64_t tx_probe_period_us = TX_PROBE_INTERVAL_MS * 1000LL;

    while (1) {
        uint32_t triggered;
        twai_read_alerts(&triggered, pdMS_TO_TICKS(10));

        // --- Bus error handling ---
        if (triggered & TWAI_ALERT_BUS_OFF) {
            ESP_LOGE(TAG, "TWAI bus-off, initiating recovery");
            bus_off = true;
            twai_initiate_recovery();
            continue;
        }
        if (triggered & TWAI_ALERT_BUS_RECOVERED) {
            ESP_LOGI(TAG, "TWAI bus recovered, restarting");
            twai_start();
            bus_off = false;
            tx_fail_count = 0;
            tx_state = TX_PROBING;
        }
        if (triggered & TWAI_ALERT_ERR_PASS) {
            ESP_LOGW(TAG, "TWAI error passive (no peers ACKing?)");
        }
        if (triggered & TWAI_ALERT_TX_FAILED) {
            if (tx_state == TX_ACTIVE) {
                tx_fail_count++;
                if (tx_fail_count >= TX_FAIL_THRESHOLD) {
                    tx_state = TX_PROBING;
                    ESP_LOGW(TAG, "TWAI no peers detected, entering slow probe");
                }
            }
        }
        if (triggered & TWAI_ALERT_TX_SUCCESS) {
            if (tx_state == TX_PROBING) {
                tx_state = TX_ACTIVE;
                tx_fail_count = 0;
                ESP_LOGI(TAG, "TWAI probe ACK'd, peer detected, resuming normal TX");
            }
            tx_fail_count = 0;
        }

        // --- Drain received messages ---
        if (triggered & TWAI_ALERT_RX_DATA) {
            if (tx_state == TX_PROBING) {
                tx_state = TX_ACTIVE;
                tx_fail_count = 0;
                ESP_LOGI(TAG, "TWAI peer detected via RX, resuming normal TX");
            }
            twai_message_t msg;
            while (twai_receive(&msg, 0) == ESP_OK) {
                if (msg.rtr) continue;

                if (msg.identifier == CAN_ID_OTA) {
                    ota_handle_trigger(msg.data, msg.data_length_code);
                } else if (msg.identifier == CAN_ID_WIFI_CONFIG) {
                    wifi_config_handle_can(msg.data, msg.data_length_code);
                } else if (msg.identifier == CAN_ID_DISCOVERY_TRIGGER) {
                    discovery_handle_trigger();
                } else if (msg.identifier == CAN_ID_TOGGLE) {
                    handle_toggle(msg.data, msg.data_length_code);
                } else if (msg.identifier == CAN_ID_BRIGHTNESS) {
                    handle_brightness(msg.data, msg.data_length_code);
                } else if (msg.identifier == CAN_ID_SEQUENCE) {
                    handle_sequence(msg.data, msg.data_length_code);
                }
            }
        }

        // Check wifi config timeout
        wifi_config_check_timeout();

        // --- Periodic status transmit ---
        int64_t now_us = esp_timer_get_time();
        int64_t effective_period = (tx_state == TX_PROBING) ? tx_probe_period_us : tx_period_us;
        if (!bus_off && (now_us - last_tx_us >= effective_period)) {
            last_tx_us = now_us;

            twai_message_t tx_msg = {
                .identifier = CAN_STATUS_ID,
                .data_length_code = 8,
                .data = {
                    s_light_values[0], s_light_values[1],
                    s_light_values[2], s_light_values[3],
                    s_light_values[4], s_light_values[5],
                    s_light_values[6], s_light_values[7],
                }
            };
            twai_transmit(&tx_msg, 0);
        }
    }
}

// =============================================================================
// Main application
// =============================================================================

void app_main(void)
{
    wifi_config_init();

    // Load WiFi credentials from NVS
    char ssid[33], password[64];
    if (wifi_config_load(ssid, sizeof(ssid), password, sizeof(password))) {
        ESP_LOGI(TAG, "WiFi credentials loaded (SSID: %s)", ssid);
    } else {
        ESP_LOGW(TAG, "No WiFi credentials — provision via CAN ID 0x01");
    }

    ota_init();
    discovery_init();

    ESP_LOGI(TAG, "=== TrailCurrent Torrent (addr %d) ===", TORRENT_ADDRESS);
    ESP_LOGI(TAG, "8-Channel PWM Lighting Control Module");
    ESP_LOGI(TAG, "Hostname: %s", wifi_config_get_hostname());

    // Initialize LEDC PWM for all 8 output channels
    pwm_init();

    ESP_LOGI(TAG, "CAN IDs: brightness=0x%02X toggle=0x%02X status=0x%02X sequence=0x%02X",
             CAN_ID_BRIGHTNESS, CAN_ID_TOGGLE, CAN_STATUS_ID, CAN_ID_SEQUENCE);

    // CAN runs in its own task so bus errors never block app_main
    xTaskCreatePinnedToCore(twai_task, "twai", 4096, NULL, 5, NULL, 1);

    ESP_LOGI(TAG, "Setup complete");

    // Main task has nothing else to do — park it
    while (1) {
        vTaskDelay(pdMS_TO_TICKS(1000));
    }
}
