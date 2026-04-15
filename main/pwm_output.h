#pragma once

#include <stdint.h>

#define NUM_OUTPUTS 8

/**
 * Configure LEDC timer and all 8 output channels. All channels start at 0.
 */
void pwm_output_init(void);

/** Set a single channel (0-7) to the given duty cycle (0-255). */
void pwm_set(int channel, uint8_t duty);

/** Set all 8 channels to the same duty cycle. */
void pwm_set_all(uint8_t duty);

/** Read back the last written duty for a channel (0-7). */
uint8_t pwm_get(int channel);

/**
 * Copy the last-written values for all channels into out[0..NUM_OUTPUTS-1].
 * Used by the CAN status transmit to snapshot current state.
 */
void pwm_get_all(uint8_t *out);

/**
 * Re-apply the internally stored duty values to the LEDC hardware.
 * Useful after a reset to restore the last-known output state.
 */
void pwm_refresh_all(void);

// --- Light sequences (blocking — call from CAN task context) ---
void light_sequence_startup(void);
void light_sequence_interior(void);
void light_sequence_exterior(void);
