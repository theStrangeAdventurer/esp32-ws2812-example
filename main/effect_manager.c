/*
 * LED Effect Manager Implementation
 */

#include "effect_manager.h"
#include "driver/gpio.h"
#include "esp_log.h"
#include "freertos/queue.h"
#include "freertos/task.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static const char *TAG = "effect_manager";
// Определение всех доступных эффектов
static const led_effect_info_t available_effects[] = {
    {"Diagonal Flow", led_strip_diagonal_flow_task,
     "Diagonal flowing light effect"},
    {"Fire", led_strip_fire_task, "Fire simulation effect"},
    {"Soft Candle", led_strip_soft_candle_task, "Soft candle flicker"},
    {"Candle", led_strip_candle_task, "Candle flicker effect"},
    {"Rainbow", led_strip_rainbow_task, "Rainbow color cycle"}};

static const int EFFECT_COUNT =
    sizeof(available_effects) / sizeof(available_effects[0]);

// Обработка кнопки с debouncing
static void button_task(void *arg) {
  button_params_t *params = (button_params_t *)arg;
  effect_manager_t *manager = params->manager;
  int button_gpio = params->button_gpio;

  bool last_state = true; // Pull-up, поэтому HIGH = не нажата
  TickType_t last_change = 0;
  const TickType_t debounce_time = pdMS_TO_TICKS(50);

  ESP_LOGI(TAG, "Button handler started on GPIO %d", button_gpio);

  while (true) {
    bool current_state = gpio_get_level(button_gpio);

    if (current_state != last_state) {
      TickType_t now = xTaskGetTickCount();
      if ((now - last_change) > debounce_time) {
        if (current_state == false) { // Кнопка нажата (LOW)
          ESP_LOGI(TAG, "Button pressed, switching effect");
          effect_manager_switch_next(manager);
        }
        last_change = now;
      }
      last_state = current_state;
    }

    vTaskDelay(pdMS_TO_TICKS(10));
  }
}

// Обработка кнопки яркости с debouncing
static void brightness_button_task(void *arg) {
  brightness_button_params_t *params = (brightness_button_params_t *)arg;
  effect_manager_t *manager = params->manager;
  int button_gpio = params->button_gpio;

  bool last_state = true; // Pull-up, поэтому HIGH = не нажата
  TickType_t last_change = 0;
  const TickType_t debounce_time = pdMS_TO_TICKS(50);
  const uint8_t brightness_step = 32; // Шаг изменения яркости (8 уровней: 32,
                                      // 64, 96, 128, 160, 192, 224, 255)
  const uint8_t min_brightness = 10;  // Минимальная яркость

  ESP_LOGI(TAG, "Brightness button handler started on GPIO %d", button_gpio);

  while (true) {
    bool current_state = gpio_get_level(button_gpio);

    if (current_state != last_state) {
      TickType_t now = xTaskGetTickCount();
      if ((now - last_change) > debounce_time) {
        if (current_state == false) { // Кнопка нажата (LOW)
          uint8_t current_brightness = effect_manager_get_brightness(manager);
          uint8_t new_brightness;

          if (current_brightness >= 255 - brightness_step) {
            // Достигли максимума, сбрасываем на минимум
            new_brightness = min_brightness;
            ESP_LOGI(TAG, "Brightness reset to minimum: %d", new_brightness);
          } else {
            // Увеличиваем на шаг
            new_brightness = current_brightness + brightness_step;
            if (new_brightness > 255)
              new_brightness = 255;
            ESP_LOGI(TAG, "Brightness increased: %d -> %d", current_brightness,
                     new_brightness);
          }

          effect_manager_set_brightness(manager, new_brightness);
        }
        last_change = now;
      }
      last_state = current_state;
    }

    vTaskDelay(pdMS_TO_TICKS(10));
  }
}

esp_err_t
effect_manager_start_brightness_button_handler(effect_manager_t *manager,
                                               int button_gpio) {
  if (!manager) {
    ESP_LOGE(TAG, "Invalid manager");
    return ESP_ERR_INVALID_ARG;
  }

  // Настройка GPIO для кнопки яркости
  gpio_config_t io_conf = {.pin_bit_mask = (1ULL << button_gpio),
                           .mode = GPIO_MODE_INPUT,
                           .pull_up_en = GPIO_PULLUP_ENABLE,
                           .pull_down_en = GPIO_PULLDOWN_DISABLE,
                           .intr_type = GPIO_INTR_DISABLE};
  ESP_ERROR_CHECK(gpio_config(&io_conf));

  // Выделить память для параметров задачи
  brightness_button_params_t *params =
      malloc(sizeof(brightness_button_params_t));
  if (!params) {
    ESP_LOGE(TAG,
             "Failed to allocate memory for brightness button task params");
    return ESP_ERR_NO_MEM;
  }

  params->manager = manager;
  params->button_gpio = button_gpio;

  // Создать задачу обработки кнопки яркости
  BaseType_t result =
      xTaskCreate(brightness_button_task, "brightness_button_handler", 2048,
                  params, 4, &manager->brightness_button_task_handle);

  if (result == pdPASS) {
    // Сохранить указатель на параметры для последующей очистки
    manager->brightness_button_params = params;
    ESP_LOGI(TAG, "Brightness button handler started on GPIO %d", button_gpio);
    return ESP_OK;
  } else {
    ESP_LOGE(TAG, "Failed to create brightness button handler task");
    free(params);
    return ESP_FAIL;
  }
}

esp_err_t effect_manager_init(effect_manager_t *manager,
                              led_effect_params_t *params) {
  if (!manager || !params) {
    ESP_LOGE(TAG, "Invalid arguments");
    return ESP_ERR_INVALID_ARG;
  }

  manager->params = params;
  manager->effects = available_effects;
  manager->effect_count = EFFECT_COUNT;
  manager->current_effect = 0;
  manager->button_task_handle = NULL;
  manager->button_params = NULL;
  manager->brightness_button_task_handle = NULL;
  manager->brightness_button_params = NULL;

  // Установить яркость по умолчанию, если не задана
  if (manager->params->brightness == 0) {
    manager->params->brightness = 128; // 50% яркости по умолчанию
  }

  ESP_LOGI(TAG, "Effect manager initialized with %d effects, brightness: %d",
           EFFECT_COUNT, manager->params->brightness);

  // Запустить первый эффект
  return effect_manager_switch_to(manager, 0);
}

void effect_manager_stop_current(effect_manager_t *manager) {
  if (!manager || !manager->params) {
    return;
  }

  if (manager->params->task_handle) {
    ESP_LOGI(TAG, "Stopping current effect: %s",
             manager->effects[manager->current_effect].name);

    // Сигнализируем о завершении
    manager->params->running = false;

    // Ждем самостоятельного завершения задачи
    for (int i = 0; i < 20; i++) { // 200ms максимум
      if (manager->params->task_handle == NULL) {
        ESP_LOGI(TAG, "Task finished gracefully");
        return; // Задача завершилась сама
      }
      vTaskDelay(pdMS_TO_TICKS(10));
    }

    // Если задача не завершилась сама, принудительно удаляем
    ESP_LOGW(TAG, "Force deleting task");
    vTaskDelete(manager->params->task_handle);
    manager->params->task_handle = NULL;
  }
}

esp_err_t effect_manager_switch_to(effect_manager_t *manager,
                                   int effect_index) {
  if (!manager || effect_index < 0 || effect_index >= manager->effect_count) {
    ESP_LOGE(TAG, "Invalid effect index: %d", effect_index);
    return ESP_ERR_INVALID_ARG;
  }

  // Остановить текущий эффект
  effect_manager_stop_current(manager);

  // Сбросить флаг
  manager->params->running = true;

  // Запустить новый эффект
  manager->current_effect = effect_index;
  const led_effect_info_t *effect = &manager->effects[effect_index];

  BaseType_t result =
      xTaskCreate(effect->func, "led_effect", 4096, manager->params, 5,
                  &manager->params->task_handle);

  if (result == pdPASS) {
    ESP_LOGI(TAG, "Switched to effect [%d]: %s", effect_index, effect->name);
    return ESP_OK;
  } else {
    ESP_LOGE(TAG, "Failed to create task for effect: %s", effect->name);
    return ESP_FAIL;
  }
}

esp_err_t effect_manager_switch_next(effect_manager_t *manager) {
  if (!manager) {
    return ESP_ERR_INVALID_ARG;
  }

  int next_effect = (manager->current_effect + 1) % manager->effect_count;
  return effect_manager_switch_to(manager, next_effect);
}

esp_err_t effect_manager_start_button_handler(effect_manager_t *manager,
                                              int button_gpio) {
  if (!manager) {
    ESP_LOGE(TAG, "Invalid manager");
    return ESP_ERR_INVALID_ARG;
  }

  // Настройка GPIO для кнопки
  gpio_config_t io_conf = {.pin_bit_mask = (1ULL << button_gpio),
                           .mode = GPIO_MODE_INPUT,
                           .pull_up_en = GPIO_PULLUP_ENABLE,
                           .pull_down_en = GPIO_PULLDOWN_DISABLE,
                           .intr_type = GPIO_INTR_DISABLE};
  ESP_ERROR_CHECK(gpio_config(&io_conf));

  // Выделить память для параметров задачи
  button_params_t *params = malloc(sizeof(button_params_t));
  if (!params) {
    ESP_LOGE(TAG, "Failed to allocate memory for button task params");
    return ESP_ERR_NO_MEM;
  }

  params->manager = manager;
  params->button_gpio = button_gpio;

  // Создать задачу обработки кнопки
  BaseType_t result = xTaskCreate(button_task, "button_handler", 2048, params,
                                  4, &manager->button_task_handle);

  if (result == pdPASS) {
    // Сохранить указатель на параметры для последующей очистки
    manager->button_params = params;
    ESP_LOGI(TAG, "Button handler started on GPIO %d", button_gpio);
    return ESP_OK;
  } else {
    ESP_LOGE(TAG, "Failed to create button handler task");
    free(params);
    return ESP_FAIL;
  }
}

const char *effect_manager_get_current_name(effect_manager_t *manager) {
  if (!manager || manager->current_effect < 0 ||
      manager->current_effect >= manager->effect_count) {
    return "Unknown";
  }
  return manager->effects[manager->current_effect].name;
}

int effect_manager_get_current_index(effect_manager_t *manager) {
  if (!manager) {
    return -1;
  }
  return manager->current_effect;
}

esp_err_t effect_manager_get_status(effect_manager_t *manager,
                                    effect_status_t *status) {
  if (!manager || !status) {
    return ESP_ERR_INVALID_ARG;
  }

  status->current_effect = manager->current_effect;
  status->total_effects = manager->effect_count;
  strncpy(status->current_name, effect_manager_get_current_name(manager),
          sizeof(status->current_name) - 1);
  status->current_name[sizeof(status->current_name) - 1] = '\0';

  // Формируем JSON-like список эффектов
  char *ptr = status->effects_list;
  int remaining = sizeof(status->effects_list) - 1;
  int written = snprintf(ptr, remaining, "[");
  ptr += written;
  remaining -= written;

  for (int i = 0; i < manager->effect_count && remaining > 0; i++) {
    written = snprintf(ptr, remaining,
                       "%s{\"id\":%d,\"name\":\"%s\",\"description\":\"%s\"}",
                       (i > 0) ? "," : "", i, manager->effects[i].name,
                       manager->effects[i].description);
    ptr += written;
    remaining -= written;
  }

  if (remaining > 0) {
    snprintf(ptr, remaining, "]");
  }

  return ESP_OK;
}

esp_err_t effect_manager_set_brightness(effect_manager_t *manager,
                                        uint8_t brightness) {
  if (!manager || !manager->params) {
    ESP_LOGE(TAG, "Invalid manager or params");
    return ESP_ERR_INVALID_ARG;
  }

  // Ограничиваем яркость от 1 до 255 (0 может вызвать проблемы)
  if (brightness == 0) {
    brightness = 1;
  }

  manager->params->brightness = brightness;
  ESP_LOGI(TAG, "Brightness set to %d", brightness);
  return ESP_OK;
}

uint8_t effect_manager_get_brightness(effect_manager_t *manager) {
  if (!manager || !manager->params) {
    return 0;
  }
  return manager->params->brightness;
}

esp_err_t effect_manager_adjust_brightness(effect_manager_t *manager,
                                           int8_t delta) {
  if (!manager || !manager->params) {
    return ESP_ERR_INVALID_ARG;
  }

  int new_brightness = (int)manager->params->brightness + delta;

  // Ограничиваем диапазон
  if (new_brightness < 1) {
    new_brightness = 1;
  } else if (new_brightness > 255) {
    new_brightness = 255;
  }

  return effect_manager_set_brightness(manager, (uint8_t)new_brightness);
}
esp_err_t effect_manager_set_effect_by_name(effect_manager_t *manager,
                                            const char *name) {
  if (!manager || !name) {
    return ESP_ERR_INVALID_ARG;
  }

  for (int i = 0; i < manager->effect_count; i++) {
    if (strcasecmp(manager->effects[i].name, name) == 0) {
      return effect_manager_switch_to(manager, i);
    }
  }

  ESP_LOGE(TAG, "Effect not found: %s", name);
  return ESP_ERR_NOT_FOUND;
}

void effect_manager_cleanup(effect_manager_t *manager) {
  if (!manager) {
    return;
  }

  ESP_LOGI(TAG, "Cleaning up effect manager");

  // Остановить текущий эффект
  effect_manager_stop_current(manager);

  // Остановить задачу обработки кнопки
  if (manager->button_task_handle) {
    ESP_LOGI(TAG, "Stopping button handler task");
    vTaskDelete(manager->button_task_handle);
    manager->button_task_handle = NULL;
  }

  // Остановить задачу обработки кнопки яркости
  if (manager->brightness_button_task_handle) {
    ESP_LOGI(TAG, "Stopping brightness button handler task");
    vTaskDelete(manager->brightness_button_task_handle);
    manager->brightness_button_task_handle = NULL;
  }

  // Освободить память параметров кнопки
  if (manager->button_params) {
    ESP_LOGI(TAG, "Freeing button task parameters");
    free(manager->button_params);
    manager->button_params = NULL;
  }

  // Освободить память параметров кнопки яркости
  if (manager->brightness_button_params) {
    ESP_LOGI(TAG, "Freeing brightness button task parameters");
    free(manager->brightness_button_params);
    manager->brightness_button_params = NULL;
  }

  // Очистить остальные поля
  manager->params = NULL;
  manager->effects = NULL;
  manager->effect_count = 0;
  manager->current_effect = 0;

  ESP_LOGI(TAG, "Effect manager cleanup completed");
}
