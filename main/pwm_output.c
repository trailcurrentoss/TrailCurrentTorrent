#include "pwm_output.h"

#include "driver/ledc.h"
#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

static const char *TAG = "pwm";

// =============================================================================
// Pin and LEDC configuration
// =============================================================================

static const int OUTPUT_PINS[NUM_OUTPUTS] = {
    32,  // OUTPUT01
    33,  // OUTPUT02
    26,  // OUTPUT03
    14,  // OUTPUT04
     4,  // OUTPUT05
    17,  // OUTPUT06
    19,  // OUTPUT07
    23,  // OUTPUT08
};

#define LEDC_TIMER      LEDC_TIMER_0
#define LEDC_MODE       LEDC_LOW_SPEED_MODE
#define LEDC_DUTY_RES   LEDC_TIMER_8_BIT   // 0-255
#define LEDC_FREQUENCY  5000               // 5 kHz

// =============================================================================
// Internal state
// =============================================================================

static uint8_t s_values[NUM_OUTPUTS] = {0};

// =============================================================================
// Public API
// =============================================================================

void pwm_output_init(void)
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

    ESP_LOGI(TAG, "LEDC PWM initialized: %d channels at %d Hz", NUM_OUTPUTS, LEDC_FREQUENCY);
}

void pwm_set(int channel, uint8_t duty)
{
    if (channel < 0 || channel >= NUM_OUTPUTS) return;
    s_values[channel] = duty;
    ledc_set_duty(LEDC_MODE, (ledc_channel_t)channel, duty);
    ledc_update_duty(LEDC_MODE, (ledc_channel_t)channel);
}

void pwm_set_all(uint8_t duty)
{
    for (int i = 0; i < NUM_OUTPUTS; i++) {
        pwm_set(i, duty);
    }
}

uint8_t pwm_get(int channel)
{
    if (channel < 0 || channel >= NUM_OUTPUTS) return 0;
    return s_values[channel];
}

void pwm_get_all(uint8_t *out)
{
    for (int i = 0; i < NUM_OUTPUTS; i++) {
        out[i] = s_values[i];
    }
}

void pwm_refresh_all(void)
{
    for (int i = 0; i < NUM_OUTPUTS; i++) {
        ledc_set_duty(LEDC_MODE, (ledc_channel_t)i, s_values[i]);
        ledc_update_duty(LEDC_MODE, (ledc_channel_t)i);
    }
}

// =============================================================================
// Light sequences (blocking)
// =============================================================================

void light_sequence_startup(void)
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

void light_sequence_interior(void)
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

void light_sequence_exterior(void)
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
