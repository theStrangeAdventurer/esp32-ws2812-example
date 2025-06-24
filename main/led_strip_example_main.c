/*
 * SPDX-FileCopyrightText: 2021-2022 Espressif Systems (Shanghai) CO LTD
 *
 * SPDX-License-Identifier: Unlicense OR CC0-1.0
 */
#include "driver/rmt_tx.h"
#include "esp_log.h"
#include "esp_random.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "led_strip_encoder.h"
#include <string.h>

#define RMT_LED_STRIP_RESOLUTION_HZ                                            \
  10000000 // 10MHz resolution, 1 tick = 0.1us (led strip needs a high
           // resolution)
#define RMT_LED_STRIP_GPIO_NUM 5

#define EXAMPLE_LED_NUMBERS 4
#define EXAMPLE_CHASE_SPEED_MS 10

static const char *TAG = "example";

static uint8_t led_strip_pixels[EXAMPLE_LED_NUMBERS * 3];

/**
 * @brief Simple helper function, converting HSV color space to RGB color space
 *
 * Wiki: https://en.wikipedia.org/wiki/HSL_and_HSV
 *
 */
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

typedef struct {
  rmt_channel_handle_t led_chan;
  rmt_encoder_handle_t led_encoder;
  rmt_transmit_config_t tx_config;
  bool running;
  TaskHandle_t task_handle;
} led_effect_params_t;

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
      led_strip_pixels[j * 3 + 0] = green;
      led_strip_pixels[j * 3 + 1] = blue;
      led_strip_pixels[j * 3 + 2] = red;
    }
    ESP_ERROR_CHECK(rmt_transmit(params->led_chan, params->led_encoder,
                                 led_strip_pixels, sizeof(led_strip_pixels),
                                 &params->tx_config));
    ESP_ERROR_CHECK(rmt_tx_wait_all_done(params->led_chan, pdMS_TO_TICKS(100)));
    vTaskDelay(pdMS_TO_TICKS(EXAMPLE_CHASE_SPEED_MS));
    start_rgb = (start_rgb + 5) % 360; // Slower, smoother movement
  }
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

      // Сохраняем оригинальный порядок цветов GRB
      led_strip_pixels[j * 3 + 0] = green;
      led_strip_pixels[j * 3 + 1] = red;
      led_strip_pixels[j * 3 + 2] = blue;
    }

    ESP_ERROR_CHECK(rmt_transmit(params->led_chan, params->led_encoder,
                                 led_strip_pixels, sizeof(led_strip_pixels),
                                 &params->tx_config));
    ESP_ERROR_CHECK(rmt_tx_wait_all_done(params->led_chan, pdMS_TO_TICKS(100)));
    vTaskDelay(pdMS_TO_TICKS(60)); // ~16 FPS для плавного эффекта
  }
  vTaskDelete(NULL);
}

void app_main(void) {
  ESP_LOGI(TAG, "Create RMT TX channel");
  rmt_channel_handle_t led_chan = NULL;
  rmt_tx_channel_config_t tx_chan_config = {
      .clk_src = RMT_CLK_SRC_DEFAULT, // select source clock
      .gpio_num = RMT_LED_STRIP_GPIO_NUM,
      .mem_block_symbols =
          64, // increase the block size can make the LED less flickering
      .resolution_hz = RMT_LED_STRIP_RESOLUTION_HZ,
      .trans_queue_depth = 4, // set the number of transactions that can be
                              // pending in the background
  };
  ESP_ERROR_CHECK(rmt_new_tx_channel(&tx_chan_config, &led_chan));

  ESP_LOGI(TAG, "Install led strip encoder");
  rmt_encoder_handle_t led_encoder = NULL;
  led_strip_encoder_config_t encoder_config = {
      .resolution = RMT_LED_STRIP_RESOLUTION_HZ,
  };
  ESP_ERROR_CHECK(rmt_new_led_strip_encoder(&encoder_config, &led_encoder));

  ESP_LOGI(TAG, "Enable RMT TX channel");
  ESP_ERROR_CHECK(rmt_enable(led_chan));

  ESP_LOGI(TAG, "Start LED rainbow chase");
  rmt_transmit_config_t tx_config = {
      .loop_count = 0, // no transfer loop
  };

  led_effect_params_t *params = malloc(sizeof(led_effect_params_t));
  *params = (led_effect_params_t){.led_chan = led_chan,
                                  .led_encoder = led_encoder,
                                  .tx_config = tx_config,
                                  .running = true,
                                  .task_handle = NULL};

  xTaskCreate(led_strip_candle_task, "led_effect", 4096, params, 5,
              &params->task_handle);

  // xTaskCreate(led_strip_rainbow_task, "led_effect", 4096, params, 5,
  //             &params->task_handle);
}
