#include "can_handler.h"
#include "can_common.h"
#include "pwm_output.h"
#include "wifi_config.h"
#include "discovery.h"
#include "ota.h"
#include "esp_log.h"
#include "esp_timer.h"
#include "driver/twai.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include <stdio.h>

static const char *TAG = "can";

// Module address (0-2) — set at build time: idf.py build -DTORRENT_ADDRESS=1
#ifndef TORRENT_ADDRESS
#define TORRENT_ADDRESS 0
#endif
#if TORRENT_ADDRESS < 0 || TORRENT_ADDRESS > 2
#error "TORRENT_ADDRESS must be 0-2"
#endif

#define STATUS_TX_INTERVAL_MS  33    // ~30 Hz
#define TX_PROBE_INTERVAL_MS   2000  // slow probe when no peers detected

// Per-instance addressed CAN IDs
#define CAN_ID_BRIGHTNESS  (CAN_ID_BRIGHTNESS_BASE + TORRENT_ADDRESS)
#define CAN_ID_TOGGLE      (CAN_ID_TOGGLE_BASE      + TORRENT_ADDRESS)
#define CAN_ID_STATUS      (CAN_ID_STATUS_BASE       + TORRENT_ADDRESS)
#define CAN_ID_SEQUENCE    (CAN_ID_SEQUENCE_BASE     + TORRENT_ADDRESS)

// =============================================================================
// CAN message handlers
// =============================================================================

static void handle_toggle(const uint8_t *data, uint8_t len)
{
    if (len < 1) return;
    uint8_t ch = data[0];

    if (ch < NUM_OUTPUTS) {
        pwm_set(ch, pwm_get(ch) > 0 ? 0 : 255);
    } else if (ch == 8) {
        // All off or all on based on data[1]
        uint8_t val = (len >= 2 && data[1] == 0) ? 0 : 255;
        pwm_set_all(val);
    } else if (ch == 9) {
        // All on if data[1] == 1
        if (len >= 2 && data[1] == 1) {
            pwm_set_all(255);
        }
        // Re-apply current stored values to LEDC hardware
        pwm_refresh_all();
    }
}

static void handle_brightness(const uint8_t *data, uint8_t len)
{
    if (len < 2) return;
    uint8_t ch   = data[0];
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
// Public API
// =============================================================================

esp_err_t can_handler_init(void)
{
    esp_err_t ret = can_common_init(CAN_TX_PIN, CAN_RX_PIN);
    if (ret != ESP_OK) return ret;

    ESP_LOGI(TAG, "CAN addr=%d toggle=0x%02X status=0x%02X brightness=0x%02X sequence=0x%02X",
             TORRENT_ADDRESS, CAN_ID_TOGGLE, CAN_ID_STATUS, CAN_ID_BRIGHTNESS, CAN_ID_SEQUENCE);
    return ESP_OK;
}

void can_handler_task(void *arg)
{
    // Configure alerts BEFORE any bus activity so no error transitions are missed.
    twai_reconfigure_alerts(CAN_COMMON_ALERTS, NULL);

    // Alerts are armed above — any TX failure is caught by the state machine.
    can_common_version_broadcast();

    typedef enum { TX_ACTIVE, TX_PROBING } tx_state_t;
    bool        bus_off        = false;
    tx_state_t  tx_state       = TX_ACTIVE;
    int         tx_fail_count  = 0;
    const int   TX_FAIL_THRESHOLD = 3;
    int64_t     last_tx_us     = 0;
    const int64_t tx_period_us       = STATUS_TX_INTERVAL_MS * 1000LL;
    const int64_t tx_probe_period_us = TX_PROBE_INTERVAL_MS  * 1000LL;

    while (1) {
        uint32_t triggered;
        twai_read_alerts(&triggered, pdMS_TO_TICKS(10));

        // --- Bus error handling ---
        if (triggered & TWAI_ALERT_BUS_OFF) {
            ESP_LOGE(TAG, "TWAI bus-off, initiating recovery");
            bus_off = true;
            twai_initiate_recovery();
            // No continue — fall through so RX_DATA in the same poll is still processed.
        }
        if (triggered & TWAI_ALERT_BUS_RECOVERED) {
            ESP_LOGI(TAG, "TWAI bus recovered, restarting");
            twai_start();
            bus_off        = false;
            tx_fail_count  = 0;
            tx_state       = TX_PROBING;
            // Version broadcast deferred until first TX_SUCCESS or RX_DATA confirms a peer.
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
                tx_state      = TX_ACTIVE;
                tx_fail_count = 0;
                can_common_version_broadcast();
                ESP_LOGI(TAG, "TWAI probe ACK'd, peer detected, resuming normal TX");
            }
            tx_fail_count = 0;
        }

        // --- Drain received messages ---
        if (triggered & TWAI_ALERT_RX_DATA) {
            if (tx_state == TX_PROBING) {
                tx_state      = TX_ACTIVE;
                tx_fail_count = 0;
                can_common_version_broadcast();
                ESP_LOGI(TAG, "TWAI peer detected via RX, resuming normal TX");
            }
            twai_message_t msg;
            while (twai_receive(&msg, 0) == ESP_OK) {
                if (msg.rtr) continue;

                switch (msg.identifier) {
                case CAN_ID_OTA:
                    ota_handle_trigger(msg.data, msg.data_length_code);
                    break;
                case CAN_ID_WIFI_CONFIG:
                    wifi_config_handle_can(msg.data, msg.data_length_code);
                    break;
                case CAN_ID_DISCOVERY_TRIGGER:
                    discovery_handle_trigger();
                    break;
                case CAN_ID_TOGGLE:
                    handle_toggle(msg.data, msg.data_length_code);
                    break;
                case CAN_ID_BRIGHTNESS:
                    handle_brightness(msg.data, msg.data_length_code);
                    break;
                case CAN_ID_SEQUENCE:
                    handle_sequence(msg.data, msg.data_length_code);
                    break;
                default:
                    break;
                }
            }
        }

        // Check wifi config timeout
        wifi_config_check_timeout();

        // --- Periodic status transmit ---
        int64_t now = esp_timer_get_time();
        int64_t effective_period = (tx_state == TX_PROBING) ? tx_probe_period_us : tx_period_us;
        if (!bus_off && (now - last_tx_us >= effective_period)) {
            last_tx_us = now;

            uint8_t vals[NUM_OUTPUTS];
            pwm_get_all(vals);

            twai_message_t tx_msg = {
                .identifier       = CAN_ID_STATUS,
                .data_length_code = NUM_OUTPUTS,
                .data = {
                    vals[0], vals[1], vals[2], vals[3],
                    vals[4], vals[5], vals[6], vals[7],
                },
            };
            twai_transmit(&tx_msg, 0);
        }
    }
}
