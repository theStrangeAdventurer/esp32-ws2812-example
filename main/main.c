/*
 * SPDX-FileCopyrightText: 2021-2022 Espressif Systems (Shanghai) CO LTD
 *
 * SPDX-License-Identifier: Unlicense OR CC0-1.0
 */
#include "driver/gpio.h"
#include "driver/rmt_tx.h"
#include "effect_manager.h"
#include "esp_log.h"
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
#define CONTROL_BUTTON_GPIO_NUM 2           // SW on Rotate endecoder
#define CONTROL_BUTTON_SECONDARY_GPIO_NUM 6 // SW on Rotate endecoder
#define CONTROL_CLK_GPIO_NUM 0
#define CONTROL_DT_GPIO_NUM 1
#define LED_BUILTIN_GPIO_NUM 8

#define WIFI_SESSID "MGTS_GPON_2950"
#define WIFI_PSWD "4W5VNRHH"

static const char *TAG = "led_strip";

static uint8_t led_strip_pixels[LED_NUMBERS * 3];
static effect_manager_t effect_manager;

static TaskHandle_t builtin_led_task_handle = NULL;
void builtin_led_task(void *pvParameters) {
  while (1) {
    if (!wifi_manager_is_connected()) {
      // WiFi не подключен - моргаем светодиодом
      gpio_set_level(LED_BUILTIN_GPIO_NUM, 1);
      vTaskDelay(pdMS_TO_TICKS(500));
      gpio_set_level(LED_BUILTIN_GPIO_NUM, 0);
      vTaskDelay(pdMS_TO_TICKS(500));
    } else {
      // WiFi подключен - гасим светодиод
      gpio_set_level(LED_BUILTIN_GPIO_NUM, 0);
      vTaskDelay(pdMS_TO_TICKS(1000));
    }
  }
}
esp_err_t led_builtin_start_handler() {
  gpio_config_t io_conf = {.pin_bit_mask = (1ULL << LED_BUILTIN_GPIO_NUM),
                           .mode = GPIO_MODE_OUTPUT,
                           .pull_up_en = GPIO_PULLUP_DISABLE,
                           .pull_down_en = GPIO_PULLDOWN_DISABLE,
                           .intr_type = GPIO_INTR_DISABLE};
  ESP_ERROR_CHECK(gpio_config(&io_conf));

  BaseType_t result = xTaskCreate(builtin_led_task, "builtin_led_task_handler",
                                  2048, NULL, 4, &builtin_led_task_handle);

  if (result == pdPASS) {
    ESP_LOGI(TAG, "Builtin led handler started on GPIO %d",
             LED_BUILTIN_GPIO_NUM);
    return ESP_OK;
  } else {
    ESP_LOGE(TAG, "Failed to create builtin led handler task");
    return ESP_FAIL;
  }
}

void led_builtin_stop_handler() {
  if (builtin_led_task_handle != NULL) {
    vTaskDelete(builtin_led_task_handle);
    builtin_led_task_handle = NULL;
    gpio_set_level(LED_BUILTIN_GPIO_NUM, 0); // Ensure LED is off
    ESP_LOGI(TAG, "Builtin LED task stopped");
  }
}

void app_main(void) {

  // Запуск встроенного светодиода
  ESP_LOGI(TAG, "Starting builtin LED handler");
  ESP_ERROR_CHECK(led_builtin_start_handler());

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
  esp_err_t wifi_ret = wifi_manager_init_sta(WIFI_SESSID, WIFI_PSWD);
  if (wifi_ret != ESP_OK) {
    ESP_LOGE(TAG, "Failed to initialize WiFi: %s", esp_err_to_name(wifi_ret));
    // Continue without WiFi - device can still work with physical controls
    ESP_LOGW(TAG, "Continuing without WiFi connection");
    led_builtin_stop_handler();

    gpio_set_level(LED_BUILTIN_GPIO_NUM,
                   1); // disable builtin led blinkikn

    vTaskDelay(pdMS_TO_TICKS(200));
    gpio_set_level(LED_BUILTIN_GPIO_NUM,
                   0); // disable builtin led blinkikn

    vTaskDelay(pdMS_TO_TICKS(200));
    gpio_set_level(LED_BUILTIN_GPIO_NUM,
                   1); // disable builtin led blinkikn
    vTaskDelay(pdMS_TO_TICKS(200));
    gpio_set_level(LED_BUILTIN_GPIO_NUM,
                   0); // disable builtin led blinkikn

  } else {
    // WiFi подключен успешно - запускаем веб-сервер
    ESP_LOGI(TAG, "WiFi connected successfully");
    ESP_LOGI(TAG, "Starting web server...");
    ESP_ERROR_CHECK(web_server_init(&effect_manager));
  }

  // Initialize SPIFFS
  ESP_LOGI(TAG, "Initializing SPIFFS...");
  ESP_ERROR_CHECK(spiffs_manager_init());
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
                            .running = false,
                            .task_handle = NULL,
                            .last_task_handle = NULL,
                            .led_strip_pixels = led_strip_pixels,
                            .pixel_buffer_size = sizeof(led_strip_pixels)};
  // Инициализация менеджера эффектов
  ESP_LOGI(TAG, "Initialize effect manager");
  ESP_ERROR_CHECK(effect_manager_init(&effect_manager, params));

  ESP_LOGI(TAG, "Current effect: %s",
           effect_manager_get_current_name(&effect_manager));
  // Запуск обработчиков физических элементов управления
  ESP_LOGI(TAG, "Start physical controls handlers");
  ESP_ERROR_CHECK(effect_manager_start_physical_controls_handler(
      &effect_manager, CONTROL_BUTTON_GPIO_NUM,
      CONTROL_BUTTON_SECONDARY_GPIO_NUM, CONTROL_CLK_GPIO_NUM,
      CONTROL_DT_GPIO_NUM));
}
