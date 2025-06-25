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
#define EXAMPLE_LED_NUMBERS 4
#define EXAMPLE_CHASE_SPEED_MS 10

// Effect parameters structure
typedef struct {
    rmt_channel_handle_t led_chan;
    rmt_encoder_handle_t led_encoder;
    rmt_transmit_config_t tx_config;
    bool running;
    TaskHandle_t task_handle;
    uint8_t *led_strip_pixels;  // Pointer to LED pixel buffer
    size_t pixel_buffer_size;   // Size of pixel buffer
} led_effect_params_t;

/**
 * @brief Convert HSV color space to RGB color space
 * 
 * @param h Hue (0-360)
 * @param s Saturation (0-100)
 * @param v Value/Brightness (0-100)
 * @param r Red output (0-255)
 * @param g Green output (0-255)
 * @param b Blue output (0-255)
 */
void led_strip_hsv2rgb(uint32_t h, uint32_t s, uint32_t v, uint32_t *r, uint32_t *g, uint32_t *b);

/**
 * @brief Rainbow effect task - cycles through colors in rainbow pattern
 * @param pvParameters Pointer to led_effect_params_t structure
 */
void led_strip_rainbow_task(void *pvParameters);

/**
 * @brief Candle effect task - simulates flickering candle flame
 * @param pvParameters Pointer to led_effect_params_t structure
 */
void led_strip_candle_task(void *pvParameters);

/**
 * @brief Diagonal flow effect task - alternating color breathing effect
 * @param pvParameters Pointer to led_effect_params_t structure
 */
void led_strip_diagonal_flow_task(void *pvParameters);

/**
 * @brief Fire effect task - simulates fire/flame with red-orange colors
 * @param pvParameters Pointer to led_effect_params_t structure
 */
void led_strip_fire_task(void *pvParameters);

/**
 * @brief Soft candle effect task - gentle candle-like breathing effect
 * @param pvParameters Pointer to led_effect_params_t structure
 */
void led_strip_soft_candle_task(void *pvParameters);

#ifdef __cplusplus
}
#endif

#endif // LED_EFFECTS_H