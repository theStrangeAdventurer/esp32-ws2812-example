/*
 * LED Strip Effects Implementation
 */

#include "led_effects.h"
#include "esp_random.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include <math.h>
#include <string.h>

void led_strip_hsv2rgb(uint32_t h, uint32_t s, uint32_t v, uint32_t *r,
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

void led_strip_rainbow_task(void *pvParameters) {
  led_effect_params_t *params = (led_effect_params_t *)pvParameters;
  uint32_t red, green, blue;
  uint16_t hue = 0;
  uint16_t start_rgb = 0;

  while (params->running) {
    for (int j = 0; j < EXAMPLE_LED_NUMBERS; j++) {
      // Modified calculation to include moving offset
      hue = (j * 360 / EXAMPLE_LED_NUMBERS + start_rgb) % 360;
      led_strip_hsv2rgb(hue, 100, 100, &red, &green, &blue);

      // Применяем яркость из параметров
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
    vTaskDelay(pdMS_TO_TICKS(EXAMPLE_CHASE_SPEED_MS));
    start_rgb = (start_rgb + 5) % 360; // Slower, smoother movement
  }
  params->task_handle = NULL;
  vTaskDelete(NULL);
}

void led_strip_candle_task(void *pvParameters) {
  led_effect_params_t *params = (led_effect_params_t *)pvParameters;
  uint32_t red, green, blue;

  // Индивидуальные значения и счетчики для каждого светодиода
  uint8_t led_brightness[EXAMPLE_LED_NUMBERS];
  uint8_t led_target[EXAMPLE_LED_NUMBERS];
  uint16_t led_hue[EXAMPLE_LED_NUMBERS];
  uint8_t led_saturation[EXAMPLE_LED_NUMBERS];
  uint16_t hue_change_timer[EXAMPLE_LED_NUMBERS];
  uint8_t brightness_step[EXAMPLE_LED_NUMBERS];

  // Инициализация начальных значений с более широким диапазоном
  for (int i = 0; i < EXAMPLE_LED_NUMBERS; i++) {
    led_brightness[i] = 30 + (esp_random() % 50); // 30-79%
    led_target[i] = 30 + (esp_random() % 50);
    led_hue[i] = 5 + (esp_random() % 20); // 5-24° (оранжево-красный)
    led_saturation[i] =
        85 + (esp_random() % 15);                // 85-99% (больше насыщенности)
    hue_change_timer[i] = esp_random() % 100;    // Случайные интервалы
    brightness_step[i] = 1 + (esp_random() % 2); // 1-2 шага изменения
  }

  while (params->running) {
    for (int j = 0; j < EXAMPLE_LED_NUMBERS; j++) {
      // Плавное изменение яркости с переменным шагом
      if (led_brightness[j] < led_target[j]) {
        led_brightness[j] =
            (led_brightness[j] + brightness_step[j] > led_target[j])
                ? led_target[j]
                : led_brightness[j] + brightness_step[j];
      } else if (led_brightness[j] > led_target[j]) {
        led_brightness[j] =
            (led_brightness[j] - brightness_step[j] < led_target[j])
                ? led_target[j]
                : led_brightness[j] - brightness_step[j];
      }

      // Новая цель при достижении текущей
      if (led_brightness[j] == led_target[j]) {
        // Иногда делаем более драматичные изменения (как у настоящей свечи)
        if (esp_random() % 10 == 0) {
          led_target[j] = 15 + (esp_random() % 75); // 15-89% (широкий диапазон)
        } else {
          led_target[j] = 30 + (esp_random() % 50); // 30-79% (обычный диапазон)
        }
        brightness_step[j] = 1 + (esp_random() % 3); // Новый шаг 1-3
      }

      // Редкие изменения оттенка (каждые 30-120 кадров)
      if (--hue_change_timer[j] == 0) {
        // Основной диапазон: теплые оранжевые тона
        if (esp_random() % 20 == 0) {
          // Редкие красноватые вспышки для реализма
          led_hue[j] = esp_random() % 10; // 0-9° (красно-оранжевый)
          led_saturation[j] = 90 + (esp_random() % 10); // 90-99%
        } else {
          led_hue[j] = 8 + (esp_random() % 17);         // 8-24° (оранжевый)
          led_saturation[j] = 80 + (esp_random() % 20); // 80-99%
        }
        hue_change_timer[j] = 30 + (esp_random() % 90);
      }

      led_strip_hsv2rgb(led_hue[j], led_saturation[j], led_brightness[j], &red,
                        &green, &blue);

      // Применяем общую яркость из параметров
      red = (red * params->brightness) / 255;
      green = (green * params->brightness) / 255;
      blue = (blue * params->brightness) / 255;

      // Сохраняем оригинальный порядок цветов GRB
      params->led_strip_pixels[j * 3 + 0] = green;
      params->led_strip_pixels[j * 3 + 1] = red;
      params->led_strip_pixels[j * 3 + 2] = blue;
    }

    ESP_ERROR_CHECK(rmt_transmit(
        params->led_chan, params->led_encoder, params->led_strip_pixels,
        params->pixel_buffer_size, &params->tx_config));
    ESP_ERROR_CHECK(rmt_tx_wait_all_done(params->led_chan, pdMS_TO_TICKS(100)));
    vTaskDelay(pdMS_TO_TICKS(60)); // ~16 FPS для плавного эффекта
  }
  params->task_handle = NULL;
  vTaskDelete(NULL);
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
    for (int j = 0; j < EXAMPLE_LED_NUMBERS; j++) {
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
  uint32_t red, green, blue;

  // Каждый светодиод - отдельный "язык пламени"
  float flame_phase[4];
  float flame_speed[4];
  uint32_t next_flicker[4];

  // Инициализация каждого "языка"
  for (int i = 0; i < 4; i++) {
    flame_phase[i] = (esp_random() % 628) / 100.0f;
    flame_speed[i] = 0.1f + (esp_random() % 20) / 100.0f;
    next_flicker[i] = esp_random() % 50;
  }

  while (params->running) {
    for (int j = 0; j < 4; j++) {
      // Базовое "дыхание" костра
      float base_flame = (sin(flame_phase[j]) + 1.0f) / 2.0f; // 0-1

      // Случайные резкие вспышки (более ограниченные)
      float intensity = base_flame * 0.5f + 0.3f; // 0.3-0.8 базовый диапазон

      if (next_flicker[j] == 0) {
        // Более умеренные вспышки
        intensity += (esp_random() % 20) / 100.0f; // +0-0.2 вместо 0.3
        next_flicker[j] = 10 + esp_random() % 40;
      } else {
        next_flicker[j]--;
      }

      // Иногда почти гаснем
      if (esp_random() % 200 == 0) {
        intensity *= 0.2f;
      }

      // Строго ограничиваем интенсивность
      if (intensity > 0.9f)
        intensity = 0.9f; // Не даем достигать 1.0
      if (intensity < 0.1f)
        intensity = 0.1f;

      // Улучшенная огненная палитра без белого и зеленого
      if (intensity < 0.4f) {
        // Темно-красный
        red = 80 + 120 * (intensity / 0.4f); // 80-200
        green = 0;
        blue = 0;
      } else if (intensity < 0.7f) {
        // Красный → оранжевый (ограниченный green)
        red = 200 + 55 * ((intensity - 0.4f) / 0.3f); // 200-255
        green = 60 * ((intensity - 0.4f) / 0.3f);     // 0-60 (не 165!)
        blue = 0;
      } else {
        // Оранжевый → желтоватый (без белого)
        red = 255;
        green = 60 + 40 * ((intensity - 0.7f) / 0.3f); // 60-100 (не 255!)
        blue = 0; // Никакого синего компонента
      }

      // Применяем общую яркость из параметров
      red = (red * params->brightness) / 255;
      green = (green * params->brightness) / 255;
      blue = (blue * params->brightness) / 255;

      params->led_strip_pixels[j * 3 + 0] = green;
      params->led_strip_pixels[j * 3 + 1] = red;
      params->led_strip_pixels[j * 3 + 2] = blue;

      // Обновляем фазу дыхания
      flame_phase[j] += flame_speed[j];
      if (flame_phase[j] > M_PI * 2)
        flame_phase[j] -= M_PI * 2;
    }

    ESP_ERROR_CHECK(rmt_transmit(
        params->led_chan, params->led_encoder, params->led_strip_pixels,
        params->pixel_buffer_size, &params->tx_config));
    ESP_ERROR_CHECK(rmt_tx_wait_all_done(params->led_chan, pdMS_TO_TICKS(100)));
    vTaskDelay(pdMS_TO_TICKS(60)); // ~16 FPS
  }
  params->task_handle = NULL;
  vTaskDelete(NULL);
}

void led_strip_soft_candle_task(void *pvParameters) {
  led_effect_params_t *params = (led_effect_params_t *)pvParameters;
  uint32_t red, green, blue;

  // Мягкое дыхание свечей
  float breathing_phase[EXAMPLE_LED_NUMBERS];
  float breathing_speed[EXAMPLE_LED_NUMBERS];
  uint16_t base_hue[EXAMPLE_LED_NUMBERS];
  uint8_t base_saturation[EXAMPLE_LED_NUMBERS];

  // Инициализация каждой "свечи"
  for (int i = 0; i < EXAMPLE_LED_NUMBERS; i++) {
    breathing_phase[i] = (esp_random() % 628) / 100.0f;         // 0-2π
    breathing_speed[i] = 0.02f + (esp_random() % 15) / 1000.0f; // 0.02-0.035
    base_hue[i] = 15 + (esp_random() % 20);        // 15-34° (оранжево-красный)
    base_saturation[i] = 80 + (esp_random() % 20); // 80-99%
  }

  while (params->running) {
    for (int j = 0; j < EXAMPLE_LED_NUMBERS; j++) {
      // Мягкое дыхание вместо резких мерцаний
      float breathing = (sin(breathing_phase[j]) + 1.0f) / 2.0f; // 0-1

      // Добавляем небольшие случайные вариации для реализма
      float flicker = 0.95f + (esp_random() % 10) / 100.0f; // 0.95-1.05
      breathing *= flicker;
      if (breathing > 1.0f)
        breathing = 1.0f;

      // Плавно меняющаяся яркость в диапазоне свечи
      uint8_t brightness = 40 + (uint8_t)(50 * breathing); // 40-90%

      // Очень медленные изменения оттенка
      uint16_t current_hue =
          base_hue[j] + (uint8_t)(5 * sin(breathing_phase[j] * 0.1f));

      led_strip_hsv2rgb(current_hue, base_saturation[j], brightness, &red,
                        &green, &blue);

      // Применяем общую яркость из параметров
      red = (red * params->brightness) / 255;
      green = (green * params->brightness) / 255;
      blue = (blue * params->brightness) / 255;

      params->led_strip_pixels[j * 3 + 0] = green;
      params->led_strip_pixels[j * 3 + 1] = red;
      params->led_strip_pixels[j * 3 + 2] = blue;

      // Обновляем фазу дыхания
      breathing_phase[j] += breathing_speed[j];
      if (breathing_phase[j] > M_PI * 2)
        breathing_phase[j] -= M_PI * 2;
    }
    ESP_ERROR_CHECK(rmt_transmit(
        params->led_chan, params->led_encoder, params->led_strip_pixels,
        params->pixel_buffer_size, &params->tx_config));
    ESP_ERROR_CHECK(rmt_tx_wait_all_done(params->led_chan, pdMS_TO_TICKS(100)));
    vTaskDelay(pdMS_TO_TICKS(80)); // ~12 FPS для спокойного эффекта
  }
  params->task_handle = NULL;
  vTaskDelete(NULL);
}
