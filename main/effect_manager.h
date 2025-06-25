/*
 * LED Effect Manager
 *
 * Centralized management for LED strip effects with button control
 * and future web API support
 */

#ifndef EFFECT_MANAGER_H
#define EFFECT_MANAGER_H

#include "esp_err.h"
#include "led_effects.h"

#ifdef __cplusplus
extern "C" {
#endif

// Тип функции эффекта
typedef void (*led_effect_func_t)(void *params);

// Описание эффекта
typedef struct {
  const char *name;
  led_effect_func_t func;
  const char *description;
} led_effect_info_t;

// Параметры для задачи обработки кнопки
typedef struct {
  struct effect_manager_s *manager; // Forward declaration
  int button_gpio;
} button_params_t;

// Менеджер эффектов
typedef struct effect_manager_s {
  led_effect_params_t *params;
  const led_effect_info_t *effects;
  int effect_count;
  int current_effect;
  TaskHandle_t button_task_handle;
  button_params_t *button_params;
} effect_manager_t;

// Статус эффектов для API
typedef struct {
  int current_effect;
  int total_effects;
  char current_name[32];
  char effects_list[512]; // JSON-like string
} effect_status_t;

/**
 * @brief Initialize effect manager
 * @param manager Pointer to effect manager structure
 * @param params LED effect parameters
 * @return ESP_OK on success
 */
esp_err_t effect_manager_init(effect_manager_t *manager,
                              led_effect_params_t *params);

/**
 * @brief Switch to next effect
 * @param manager Pointer to effect manager
 * @return ESP_OK on success
 */
esp_err_t effect_manager_switch_next(effect_manager_t *manager);

/**
 * @brief Switch to specific effect by index
 * @param manager Pointer to effect manager
 * @param effect_index Index of effect to switch to
 * @return ESP_OK on success
 */
esp_err_t effect_manager_switch_to(effect_manager_t *manager, int effect_index);

/**
 * @brief Start button handler task
 * @param manager Pointer to effect manager
 * @param button_gpio GPIO number for button
 * @return ESP_OK on success
 */
esp_err_t effect_manager_start_button_handler(effect_manager_t *manager,
                                              int button_gpio);

/**
 * @brief Stop current effect
 * @param manager Pointer to effect manager
 */
void effect_manager_stop_current(effect_manager_t *manager);

/**
 * @brief Get current effect name
 * @param manager Pointer to effect manager
 * @return Current effect name string
 */
const char *effect_manager_get_current_name(effect_manager_t *manager);

/**
 * @brief Get current effect index
 * @param manager Pointer to effect manager
 * @return Current effect index
 */
int effect_manager_get_current_index(effect_manager_t *manager);

/**
 * @brief Get effect status for API
 * @param manager Pointer to effect manager
 * @param status Pointer to status structure to fill
 * @return ESP_OK on success
 */
esp_err_t effect_manager_get_status(effect_manager_t *manager,
                                    effect_status_t *status);

/**
 * @brief Set effect by name (for API)
 * @param manager Pointer to effect manager
 * @param name Name of effect to activate
 * @return ESP_OK on success
 */
esp_err_t effect_manager_set_effect_by_name(effect_manager_t *manager,
                                            const char *name);

/**
 * @brief Clean up effect manager and free all resources
 * @param manager Pointer to effect manager
 */
void effect_manager_cleanup(effect_manager_t *manager);

#ifdef __cplusplus
}
#endif

#endif // EFFECT_MANAGER_H
