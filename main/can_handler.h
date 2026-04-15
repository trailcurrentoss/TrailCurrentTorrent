#pragma once

#include "esp_err.h"

// CAN bus pin assignments — ESP32-WROOM-32 8-Channel PWM Module
#define CAN_TX_PIN  15
#define CAN_RX_PIN  13

// Shared protocol IDs (all modules on the bus)
#define CAN_ID_OTA               0x00
#define CAN_ID_WIFI_CONFIG       0x01
#define CAN_ID_DISCOVERY_TRIGGER 0x02

// Per-instance ID bases (add TORRENT_ADDRESS to get the addressed ID)
#define CAN_ID_BRIGHTNESS_BASE   0x15   // 0x15-0x17
#define CAN_ID_TOGGLE_BASE       0x18   // 0x18-0x1A
#define CAN_ID_STATUS_BASE       0x1B   // 0x1B-0x1D
#define CAN_ID_SEQUENCE_BASE     0x33   // 0x33-0x35

/**
 * Install and start the TWAI driver. Must be called from app_main()
 * before can_handler_task() is created. Does not transmit anything.
 */
esp_err_t can_handler_init(void);

/**
 * CAN bus task entry point. Configures alerts, then loops handling
 * received messages and sending periodic status frames.
 * Pin to core 1: xTaskCreatePinnedToCore(can_handler_task, ..., 1)
 */
void can_handler_task(void *arg);
