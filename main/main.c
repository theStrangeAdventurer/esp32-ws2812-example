/*
 * SPDX-FileCopyrightText: 2021-2022 Espressif Systems (Shanghai) CO LTD
 *
 * SPDX-License-Identifier: Unlicense OR CC0-1.0
 */
#include "driver/rmt_tx.h"
#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "led_effects.h"
#include "led_strip_encoder.h"

#define RMT_LED_STRIP_RESOLUTION_HZ                                            \
  10000000 // 10MHz resolution, 1 tick = 0.1us (led strip needs a high
           // resolution)
#define RMT_LED_STRIP_GPIO_NUM 5

static const char *TAG = "led_strip";

static uint8_t led_strip_pixels[EXAMPLE_LED_NUMBERS * 3];

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

  ESP_LOGI(TAG, "Start LED effects");
  rmt_transmit_config_t tx_config = {
      .loop_count = 0, // no transfer loop
  };

  led_effect_params_t *params = malloc(sizeof(led_effect_params_t));
  *params =
      (led_effect_params_t){.led_chan = led_chan,
                            .led_encoder = led_encoder,
                            .tx_config = tx_config,
                            .running = true,
                            .task_handle = NULL,
                            .led_strip_pixels = led_strip_pixels,
                            .pixel_buffer_size = sizeof(led_strip_pixels)};

  xTaskCreate(led_strip_diagonal_flow_task, "led_effect", 4096, params, 5,
              &params->task_handle);

  // Выберите нужный эффект, раскомментировав одну из строк:
  // xTaskCreate(led_strip_fire_task, "led_effect", 4096, params, 5,
  //             &params->task_handle);

  // xTaskCreate(led_strip_soft_candle_task, "led_effect", 4096, params, 5,
  //             &params->task_handle);

  // xTaskCreate(led_strip_candle_task, "led_effect", 4096, params, 5,
  //             &params->task_handle);

  // xTaskCreate(led_strip_rainbow_task, "led_effect", 4096, params, 5,
  //             &params->task_handle);
}
