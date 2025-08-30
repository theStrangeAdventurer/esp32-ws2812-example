/*
 * LED Strip Effects Module
 *
 * This module contains various LED strip effects for ESP32
 */

#ifndef LED_EFFECTS_H
#define LED_EFFECTS_H

#include "driver/rmt_tx.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

#ifdef __cplusplus
extern "C" {
#endif

// Configuration constants
#define LED_NUMBERS_COL 8
#define LED_NUMBERS_ROW 8
#define LED_NUMBERS (LED_NUMBERS_COL * LED_NUMBERS_ROW)
#define LED_SHOULD_ROUND 1 // Will round active leds to pretend a circle

#define EXAMPLE_CHASE_SPEED_MS 10

// Effect parameters structure
typedef struct {
  rmt_channel_handle_t led_chan;
  rmt_encoder_handle_t led_encoder;
  rmt_transmit_config_t tx_config;
  bool running;
  TaskHandle_t task_handle;
  uint8_t *led_strip_pixels; // Pointer to LED pixel buffer
  size_t pixel_buffer_size;  // Size of pixel buffer
  uint8_t brightness;        // Brightness level (1-255)
} led_effect_params_t;

void led_strip_firefly_task(void *pvParameters);
void led_strip_fire_task(void *pvParameters);
void led_strip_soft_light_task(void *pvParameters);
void led_strip_stars_task(void *pvParameters);

#ifdef __cplusplus
}
#endif

#endif // LED_EFFECTS_H
