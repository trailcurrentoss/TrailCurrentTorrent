#pragma once

#include <stdint.h>

// Discovery window duration (30 seconds)
#define DISCOVERY_TIMEOUT_MS 30000

/**
 * Initialize discovery subsystem — load configured flag from NVS.
 * Must be called after ota_init().
 */
void discovery_init(void);

/**
 * Handle a CAN discovery trigger (ID 0x02, broadcast).
 * If this module is unconfigured and has WiFi credentials,
 * joins WiFi, advertises via mDNS with module metadata,
 * and waits for Headwaters to confirm registration.
 */
void discovery_handle_trigger(void);

/**
 * Handle a CAN discovery reset (ID 0x03, targeted by MAC).
 * Clears the configured flag so this module responds to
 * the next discovery trigger.
 */
void discovery_handle_reset(const uint8_t *data, uint8_t len);
