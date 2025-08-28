/*
 * LED Strip Effects Implementation
 */

#include "led_effects.h"
#include "esp_random.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include <math.h>
#include <string.h>

#if LED_SHOULD_ROUND == 1
// Function to check if LED should be disabled for circular rounding
static bool is_corner_led(int led_index, float threshold) {
  // Calculate row and column position in the matrix
  int row = led_index / LED_NUMBERS_COL;
  int col = led_index % LED_NUMBERS_COL;

  // Calculate distance from center for each LED
  float center_x = (LED_NUMBERS_COL - 1) / 2.0f;
  float center_y = (LED_NUMBERS_ROW - 1) / 2.0f;

  float distance = sqrtf(powf(col - center_x, 2) + powf(row - center_y, 2));
  float max_radius = sqrtf(powf(center_x, 2) + powf(center_y, 2));

  // Disable LEDs that are outside the circular area
  // Adjust the 0.9 factor to control how "round" the corners are
  return distance > max_radius * threshold;
}
#endif

static void led_strip_hsv2rgb(uint32_t h, uint32_t s, uint32_t v, uint32_t *r,
                              uint32_t *g, uint32_t *b) {
  h %= 360; // h -> [0,360]
  uint32_t rgb_max = (v * 255) / 100;
  uint32_t rgb_min = (rgb_max * (100 - s)) / 100;

  uint32_t i = h / 60;
  uint32_t diff = h % 60;

  // RGB adjustment amount by hue
  uint32_t rgb_adj = (rgb_max - rgb_min) * diff / 60;

  switch (i) {
  case 0:
    *r = rgb_max;
    *g = rgb_min + rgb_adj;
    *b = rgb_min;
    break;
  case 1:
    *r = rgb_max - rgb_adj;
    *g = rgb_max;
    *b = rgb_min;
    break;
  case 2:
    *r = rgb_min;
    *g = rgb_max;
    *b = rgb_min + rgb_adj;
    break;
  case 3:
    *r = rgb_min;
    *g = rgb_max - rgb_adj;
    *b = rgb_max;
    break;
  case 4:
    *r = rgb_min + rgb_adj;
    *g = rgb_min;
    *b = rgb_max;
    break;
  default:
    *r = rgb_max;
    *g = rgb_min;
    *b = rgb_max - rgb_adj;
    break;
  }
}

void led_strip_diagonal_flow_task(void *pvParameters) {
  led_effect_params_t *params = (led_effect_params_t *)pvParameters;
  uint32_t red, green, blue;

  // Цветовая палитра: фиолетовый, оранжевый (без синего)
  const uint16_t colors[2] = {280, 20}; // фиолетовый, более оранжевый желтый
  const uint8_t saturation = 100;
  const uint8_t max_brightness = 80;

  float phase = 0;
  const float speed = 0.05f;

  // Состояние для отслеживания смены цветов
  static uint8_t current_color_group[2] = {0, 1}; // цвета для каждой группы
  static bool fade_detected[2] = {false, false};  // флаги обнаружения угасания

  while (params->running) {
    for (int j = 0; j < LED_NUMBERS; j++) {

#if LED_SHOULD_ROUND == 1
      if (is_corner_led(j, 0.95f)) {
        // Disable corner LEDs
        params->led_strip_pixels[j * 3 + 0] = 0;
        params->led_strip_pixels[j * 3 + 1] = 0;
        params->led_strip_pixels[j * 3 + 2] = 0;
        continue;
      }
#endif

      int group = (j == 0 || j == 2) ? 0 : 1;
      float led_phase = phase + (group * M_PI);

      // Плавная волна дыхания от 0 до 1 с более выраженным угасанием
      float breath_wave = (sin(led_phase) + 1.0f) / 2.0f;
      breath_wave = breath_wave * breath_wave;

      uint8_t brightness = (uint8_t)(max_brightness * breath_wave);
      if (brightness < 5)
        brightness = 0;

      // Детекция полного угасания для смены цвета
      if (brightness == 0 && !fade_detected[group]) {
        // Момент полного угасания - меняем цвет группы
        current_color_group[group] = 1 - current_color_group[group];
        fade_detected[group] = true;
      } else if (brightness > 10) {
        // Сброс флага когда яркость поднимается
        fade_detected[group] = false;
      }

      uint16_t current_hue = colors[current_color_group[group]];

      led_strip_hsv2rgb(current_hue, saturation, brightness, &red, &green,
                        &blue);

      // Применяем общую яркость из параметров
      red = (red * params->brightness) / 255;
      green = (green * params->brightness) / 255;
      blue = (blue * params->brightness) / 255;

      params->led_strip_pixels[j * 3 + 0] = green;
      params->led_strip_pixels[j * 3 + 1] = red;
      params->led_strip_pixels[j * 3 + 2] = blue;
    }

    ESP_ERROR_CHECK(rmt_transmit(
        params->led_chan, params->led_encoder, params->led_strip_pixels,
        params->pixel_buffer_size, &params->tx_config));
    ESP_ERROR_CHECK(rmt_tx_wait_all_done(params->led_chan, pdMS_TO_TICKS(100)));

    phase += speed;
    if (phase >= M_PI * 4) {
      phase = 0;
    }

    vTaskDelay(pdMS_TO_TICKS(40));
  }
  params->task_handle = NULL;
  vTaskDelete(NULL);
}

void led_strip_fire_task(void *pvParameters) {
  led_effect_params_t *params = (led_effect_params_t *)pvParameters;

  // Heat array for fire simulation (2D matrix)
  static uint8_t heat[LED_NUMBERS_ROW][LED_NUMBERS_COL];
  uint32_t red, green, blue;

  // Initialize heat array
  for (int row = 0; row < LED_NUMBERS_ROW; row++) {
    for (int col = 0; col < LED_NUMBERS_COL; col++) {
      heat[row][col] = 0;
    }
  }

  float_t threshold = 0.1f; // Начальное значение
  const float_t target_threshold = 0.8f;
  const float_t threshold_step = (target_threshold - threshold) / (100 / 5);
  while (params->running) {

    if (threshold < target_threshold) {
      threshold += threshold_step;
      if (threshold > target_threshold) {
        threshold = target_threshold;
      }
    }
    // Step 1: Cool down every cell
    for (int row = 0; row < LED_NUMBERS_ROW; row++) {
      for (int col = 0; col < LED_NUMBERS_COL; col++) {
        uint8_t cooling = (esp_random() % 10) + 5; // 5-14
        if (cooling > heat[row][col]) {
          heat[row][col] = 0;
        } else {
          heat[row][col] -= cooling;
        }
      }
    }

    // Step 2: Heat propagation (более простое и надежное)
    for (int row = LED_NUMBERS_ROW - 1; row > 0; row--) {
      for (int col = 0; col < LED_NUMBERS_COL; col++) {
        // Простое распространение: 80% от нижнего + 20% от текущего
        if (row > 0) {
          heat[row][col] = (heat[row - 1][col] * 8 + heat[row][col] * 2) / 10;
        }
      }
    }

    // Step 3: Add new sparks at the bottom row
    for (int col = 0; col < LED_NUMBERS_COL; col++) {
      if (esp_random() % 10 < 5) {                 // 50% chance per bottom cell
        uint8_t spark = 180 + (esp_random() % 76); // 180-255
        if (spark > heat[0][col]) {
          heat[0][col] = spark;
        }
      }
    }

    // Step 4: ЧИСТАЯ ОГНЕННАЯ ПАЛИТРА БЕЗ СИНЕГО
    for (int i = 0; i < LED_NUMBERS; i++) {
      int row = i / LED_NUMBERS_COL;
      int col = i % LED_NUMBERS_COL;

#if LED_SHOULD_ROUND == 1
      if (is_corner_led(i, 0.95f)) {
        // Disable corner LEDs
        params->led_strip_pixels[i * 3 + 0] = 0;
        params->led_strip_pixels[i * 3 + 1] = 0;
        params->led_strip_pixels[i * 3 + 2] = 0;
        continue;
      }
#endif

      // Чистая огненная палитра: черный → красный → оранжевый
      uint8_t heat_val = heat[row][col];

      if (heat_val < 85) {    // Черный → темно-красный
        red = heat_val * 3;   // 0-255
        green = heat_val / 4; // 0-21 (очень мало зеленого)
        blue = 0;
      } else if (heat_val < 170) { // Темно-красный → ярко-красный
        red = 255;
        green = (heat_val - 85) * 1; // 0-170 (умеренный зеленый)
        blue = 0;
      } else { // Красный → оранжевый → желтый
        red = 255;
        green = 140 + (heat_val - 170) / 2; // 170-255
        blue = 0;
      }

      // Применяем общую яркость
      red = (red * params->brightness) / 255;
      green = (green * params->brightness) / 255;
      blue = (blue * params->brightness) / 255;

      // Правильный порядок GRB
      params->led_strip_pixels[i * 3 + 0] = green;
      params->led_strip_pixels[i * 3 + 1] = red;
      params->led_strip_pixels[i * 3 + 2] = blue;
    }

    // Send to LEDs
    ESP_ERROR_CHECK(rmt_transmit(
        params->led_chan, params->led_encoder, params->led_strip_pixels,
        params->pixel_buffer_size, &params->tx_config));
    ESP_ERROR_CHECK(rmt_tx_wait_all_done(params->led_chan, pdMS_TO_TICKS(100)));

    vTaskDelay(pdMS_TO_TICKS(70)); // ~14 FPS
  }

  params->task_handle = NULL;
  vTaskDelete(NULL);
}

void led_strip_soft_light_task(void *pvParameters) {
  led_effect_params_t *params = (led_effect_params_t *)pvParameters;
  uint32_t red, green, blue;

  float_t threshold = 0.1f; // Начальное значение
  const float_t target_threshold = 0.8f;
  const float_t threshold_step = (target_threshold - threshold) / (100 / 5);
  while (params->running) {
    if (threshold < target_threshold) {
      threshold += threshold_step;
      if (threshold > target_threshold) {
        threshold = target_threshold;
      }
    }

    for (int i = 0; i < LED_NUMBERS; i++) {
      // Теплый белый ~2300K
      red = 255;
      green = 115;
      blue = 23;

#if LED_SHOULD_ROUND == 1
      if (is_corner_led(i, threshold)) {
        // Disable corner LEDs
        params->led_strip_pixels[i * 3 + 0] = 0;
        params->led_strip_pixels[i * 3 + 1] = 0;
        params->led_strip_pixels[i * 3 + 2] = 0;
        continue;
      }
#endif

      if (params->brightness <= 1) {
        red = green = blue = 0;
      } else {
        red = (red * params->brightness) / 255;
        green = (green * params->brightness) / 255;
        blue = (blue * params->brightness) / 255;
      }
      params->led_strip_pixels[i * 3 + 0] = green;
      params->led_strip_pixels[i * 3 + 1] = red;
      params->led_strip_pixels[i * 3 + 2] = blue;
    }
    ESP_ERROR_CHECK(rmt_transmit(
        params->led_chan, params->led_encoder, params->led_strip_pixels,
        params->pixel_buffer_size, &params->tx_config));
    ESP_ERROR_CHECK(rmt_tx_wait_all_done(params->led_chan, pdMS_TO_TICKS(100)));
    vTaskDelay(pdMS_TO_TICKS(16)); // 60 FPS для стабильного мягкого света
  }
  params->task_handle = NULL;
  vTaskDelete(NULL);
}
