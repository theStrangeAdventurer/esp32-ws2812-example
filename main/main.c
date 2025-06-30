/*
 * SPDX-FileCopyrightText: 2021-2022 Espressif Systems (Shanghai) CO LTD
 *
 * SPDX-License-Identifier: Unlicense OR CC0-1.0
 */
#include "driver/rmt_tx.h"
#include "effect_manager.h"
#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "led_effects.h"
#include "led_strip_encoder.h"
#include "nvs_flash.h"
#include "spiffs_manager.h"
#include "web_server.h"
#include "wifi_manager.h"
#define RMT_LED_STRIP_RESOLUTION_HZ                                            \
  10000000 // 10MHz resolution, 1 tick = 0.1us (led strip needs a high
           // resolution)
#define RMT_LED_STRIP_GPIO_NUM 5
#define BUTTON_GPIO_NUM 9
#define BUTTON_BRIGHTNESS_GPIO_NUM 10
#define WIFI_SESSID "MGTS_GPON_2950"
#define WIFI_PSWD "4W5VNRHH"

static const char *TAG = "led_strip";

static uint8_t led_strip_pixels[EXAMPLE_LED_NUMBERS * 3];
static effect_manager_t effect_manager;

void app_main(void) {
  // Initialize NVS
  esp_err_t ret = nvs_flash_init();
  if (ret == ESP_ERR_NVS_NO_FREE_PAGES ||
      ret == ESP_ERR_NVS_NEW_VERSION_FOUND) {
    ESP_ERROR_CHECK(nvs_flash_erase());
    ret = nvs_flash_init();
  }
  ESP_ERROR_CHECK(ret);

  // Initialize WiFi
  ESP_LOGI(TAG, "Initializing WiFi...");
  ESP_ERROR_CHECK(wifi_manager_init_sta(WIFI_SESSID, WIFI_PSWD));

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

  // Подготовка параметров для эффектов
  led_effect_params_t *params = malloc(sizeof(led_effect_params_t));
  if (params == NULL) {
    ESP_LOGE(TAG, "Failed to allocate memory for LED effect parameters");
    return;
  }

  *params =
      (led_effect_params_t){.led_chan = led_chan,
                            .led_encoder = led_encoder,
                            .tx_config = tx_config,
                            .running = true,
                            .task_handle = NULL,
                            .led_strip_pixels = led_strip_pixels,
                            .pixel_buffer_size = sizeof(led_strip_pixels)};

  // Инициализация менеджера эффектов
  ESP_LOGI(TAG, "Initialize effect manager");
  ESP_ERROR_CHECK(effect_manager_init(&effect_manager, params));

  // Запуск обработчика кнопки
  ESP_LOGI(TAG, "Start button handler");
  ESP_ERROR_CHECK(
      effect_manager_start_button_handler(&effect_manager, BUTTON_GPIO_NUM));

  // Запуск обработчика кнопки яркости
  ESP_LOGI(TAG, "Start brightness button handler");
  ESP_ERROR_CHECK(effect_manager_start_brightness_button_handler(
      &effect_manager, BUTTON_BRIGHTNESS_GPIO_NUM));

  // Initialize SPIFFS
  ESP_LOGI(TAG, "Initializing SPIFFS...");
  ESP_ERROR_CHECK(spiffs_manager_init());

  // Запуск веб-сервера
  ESP_LOGI(TAG, "Starting web server...");
  ESP_ERROR_CHECK(web_server_init(&effect_manager));
  ESP_LOGI(TAG, "LED strip initialized. Press button to switch effects.");
  ESP_LOGI(TAG, "Press brightness button (GPIO %d) to adjust brightness.",
           BUTTON_BRIGHTNESS_GPIO_NUM);
  ESP_LOGI(TAG, "Current effect: %s",
           effect_manager_get_current_name(&effect_manager));
}
