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

// Utility function to clear LED matrix
static void clear_led_matrix(led_effect_params_t *params) {
  // Clear all pixels to black
  for (int i = 0; i < LED_NUMBERS; i++) {
    params->led_strip_pixels[i * 3 + 0] = 0; // Green
    params->led_strip_pixels[i * 3 + 1] = 0; // Red
    params->led_strip_pixels[i * 3 + 2] = 0; // Blue
  }

  // Send cleared data to LED strip
  ESP_ERROR_CHECK(rmt_transmit(params->led_chan, params->led_encoder,
                               params->led_strip_pixels,
                               params->pixel_buffer_size, &params->tx_config));
  ESP_ERROR_CHECK(rmt_tx_wait_all_done(params->led_chan, pdMS_TO_TICKS(100)));
}

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

void led_strip_firefly_task(void *pvParameters) {
  led_effect_params_t *params = (led_effect_params_t *)pvParameters;
  uint32_t red, green, blue;

  // Цвета: черный фон, желтый светлячек
  const uint16_t yellow_hue = 20;
  const uint8_t saturation = 100;
  const uint8_t firefly_max_brightness = 100;

  // 2D position for firefly
  float firefly_x = 0.0f;
  float firefly_y = LED_NUMBERS_ROW / 2.0f;
  float firefly_size = 3.0f;           // базовый размер светлячка в LED
  const float firefly_size_min = 1.5f; // минимальный размер
  const float firefly_size_max = 3.5f; // максимальный размер
  float size_phase = 0.0f;
  const float size_change_speed = 0.03f; // скорость изменения размера
  float movement_phase = 0.0f;
  const float movement_speed = 0.05f;
  const float center_x = (LED_NUMBERS_COL - 1) / 2.0f;
  const float center_y = (LED_NUMBERS_ROW - 1) / 2.0f;

  const float figure8_width = LED_NUMBERS_COL * 0.8f;
  const float figure8_height = LED_NUMBERS_ROW * 0.8f;
  // Параметры для более естественного мерцания
  float random_flicker_timer = 0.0f;
  const float random_flicker_interval_min = 0.5f;
  const float random_flicker_interval_max = 3.0f;
  float next_random_flicker = random_flicker_interval_min;
  bool is_random_dim = false;

  // Эффект мерцания светлячка
  float flicker_phase = 0.0f;
  float flicker_speed = 0.2f;
  // Параметры для вариации скорости мерцания
  float flicker_variation = 0.0f;
  float flicker_variation_phase = 0.0f;
  const float flicker_variation_speed =
      0.014f; // Медленнее чем основное мерцание
  // Микро-мерцания
  float micro_flicker = 0.0f;
  float micro_flicker_phase = 0.0f;
  const float micro_flicker_speed = 0.8f;   // Быстрые микро-мерцания
  const float micro_flicker_amount = 0.15f; // Интенсивность микро-мерцаний

  while (params->running) {
    // Обновляем фазу движения
    movement_phase += movement_speed;
    if (movement_phase > 2 * M_PI) {
      movement_phase -= 2 * M_PI;
    }

    // Обновляем фазу изменения размера
    size_phase += size_change_speed;
    if (size_phase > 2 * M_PI) {
      size_phase -= 2 * M_PI;
    }

    firefly_size =
        ((firefly_size_max - firefly_size_min) / 2) * (sin(size_phase) + 1.0f) +
        firefly_size_min;

    // Обновляем случайное мерцание
    random_flicker_timer += 0.02f;
    if (random_flicker_timer >= next_random_flicker) {
      random_flicker_timer = 0.0f;
      is_random_dim = !is_random_dim;

      // Следующий интервал мерцания случайный
      float random_factor = (float)esp_random() / UINT32_MAX;
      next_random_flicker = random_flicker_interval_min +
                            random_factor * (random_flicker_interval_max -
                                             random_flicker_interval_min);
    }

    flicker_variation_phase += flicker_variation_speed;
    if (flicker_variation_phase > 2 * M_PI) {
      flicker_variation_phase -= 2 * M_PI;
    }
    flicker_variation =
        (sin(flicker_variation_phase) + 1.0f) / 2.0f; // 0.0 - 1.0
    flicker_speed = 0.1f + flicker_variation * 0.2f;  // 0.1 - 0.3

    micro_flicker_phase += micro_flicker_speed;
    if (micro_flicker_phase > 2 * M_PI) {
      micro_flicker_phase -= 2 * M_PI;
    }
    micro_flicker = sin(micro_flicker_phase) * micro_flicker_amount;

    // u0412u043eu0441u044cu043cu0435u0440u043au0430 -
    // u0435u0434u0438u043du0441u0442u0432u0435u043du043du044bu0439
    // u0440u0435u0436u0438u043c u0434u0432u0438u0436u0435u043du0438u044f
    firefly_x = center_x + (figure8_width / 2) * sin(movement_phase);
    firefly_y = center_y + (figure8_height / 2) * sin(movement_phase) *
                               cos(movement_phase);

    // Обновляем мерцание
    flicker_phase += flicker_speed;
    if (flicker_phase >= M_PI * 2) {
      flicker_phase = 0;
    }

    // Яркость светлячка с мерцанием
    float flicker = (sin(flicker_phase) + 1.0f) / 2.0f;

    // Добавляем микро-мерцания для большей естественности
    flicker += micro_flicker;
    if (flicker < 0.3f)
      flicker = 0.3f; // Минимальная яркость
    if (flicker > 1.0f)
      flicker = 1.0f; // Максимальная яркость

    if (is_random_dim) {
      flicker *= 0.5f; // Dim by 50% during random flickering
    }

    uint8_t firefly_brightness = (uint8_t)(firefly_max_brightness * flicker);

    for (int j = 0; j < LED_NUMBERS; j++) {
#if LED_SHOULD_ROUND == 1
      if (is_corner_led(j, 0.95f)) {
        // Отключаем угловые светодиоды
        params->led_strip_pixels[j * 3 + 0] = 0;
        params->led_strip_pixels[j * 3 + 1] = 0;
        params->led_strip_pixels[j * 3 + 2] = 0;
        continue;
      }
#endif

      // Определяем расстояние до светлячка
      // Переводим 1D индекс в 2D координаты
      int row = j / LED_NUMBERS_COL;
      int col = j % LED_NUMBERS_COL;

      // Рассчитываем расстояние в 2D
      float distance =
          sqrtf(powf(col - firefly_x, 2) + powf(row - firefly_y, 2));

      if (distance <= firefly_size) {
        // Светлячек - плавное затухание от центра
        float intensity = 1.0f - (distance / firefly_size);
        intensity = intensity * intensity; // квадратичное затухание

        uint8_t brightness = (uint8_t)(firefly_brightness * intensity);
        led_strip_hsv2rgb(yellow_hue, saturation, brightness, &red, &green,
                          &blue);
      } else {
        // Фон - черный
        red = 0;
        green = 0;
        blue = 0;
      }

      // Применяем общую яркость
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

    // Уменьшаем задержку для увеличения FPS
    vTaskDelay(pdMS_TO_TICKS(45));
  }

  // Clear LED matrix before task termination
  clear_led_matrix(params);

  params->last_task_handle = params->task_handle;
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
  const float_t threshold_step = (target_threshold - threshold) / (10 / 5);
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
      if (is_corner_led(i, threshold)) {
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

    vTaskDelay(pdMS_TO_TICKS(40));
  }

  // Clear LED matrix before task termination
  clear_led_matrix(params);

  params->last_task_handle = params->task_handle;
  params->task_handle = NULL;
  vTaskDelete(NULL);
}

void led_strip_stars_task(void *pvParameters) {
  led_effect_params_t *params = (led_effect_params_t *)pvParameters;
  uint32_t red, green, blue;

  // Star structure
  typedef struct {
    int position;            // LED index
    float brightness;        // Current brightness (0.0 - 1.0)
    float target_brightness; // Target brightness
    float fade_speed;        // How fast it fades
    bool active;             // Is this star active
    uint8_t color_type;      // 0=cool white, 1=warm white, 2=blue-white
    float timer;             // For timing control
    float next_change;       // When to change state
  } star_t;

  // Array of stars
  const int MAX_STARS = LED_NUMBERS / 4; // Up to 25% of LEDs can be stars
  static star_t stars[LED_NUMBERS / 4];

  // Initialize stars
  for (int i = 0; i < MAX_STARS; i++) {
    stars[i].position = esp_random() % LED_NUMBERS;
    stars[i].brightness = 0.0f;
    stars[i].target_brightness = 0.0f;
    stars[i].fade_speed =
        0.01f + (float)(esp_random() % 30) / 1000.0f; // 0.01-0.04
    stars[i].active = false;
    stars[i].color_type = esp_random() % 3;
    stars[i].timer = 0.0f;
    stars[i].next_change =
        (float)(esp_random() % 3000) / 1000.0f; // 0-3 seconds
  }

  float_t threshold = 0.1f;
  const float_t target_threshold = 0.95f;
  const float_t threshold_step = (target_threshold - threshold) / (100 / 5);

  while (params->running) {
    // Gradually increase corner rounding threshold
    if (threshold < target_threshold) {
      threshold += threshold_step;
      if (threshold > target_threshold) {
        threshold = target_threshold;
      }
    }

    // Update stars
    for (int i = 0; i < MAX_STARS; i++) {
      stars[i].timer += 0.05f;

      // Check if it's time to change star state
      if (stars[i].timer >= stars[i].next_change) {
        stars[i].timer = 0.0f;

        if (stars[i].active && stars[i].target_brightness > 0.1f) {
          // Start fading out
          stars[i].target_brightness = 0.0f;
          stars[i].next_change =
              1.0f + (float)(esp_random() % 2000) / 1000.0f; // 1-3s
        } else if (!stars[i].active || stars[i].target_brightness <= 0.1f) {
          // Randomly activate star or keep it inactive
          if (esp_random() % 100 < 15) { // 15% chance to activate
            stars[i].active = true;
            stars[i].position = esp_random() % LED_NUMBERS;
            stars[i].target_brightness =
                0.3f + (float)(esp_random() % 70) / 100.0f; // 0.3-1.0
            stars[i].color_type = esp_random() % 3;
            stars[i].fade_speed =
                0.008f + (float)(esp_random() % 25) / 1000.0f; // 0.008-0.033
            stars[i].next_change =
                2.0f + (float)(esp_random() % 4000) / 1000.0f; // 2-6s
          } else {
            stars[i].next_change =
                0.5f + (float)(esp_random() % 1500) / 1000.0f; // 0.5-2s
          }
        }
      }

      // Update brightness towards target
      if (stars[i].brightness < stars[i].target_brightness) {
        stars[i].brightness += stars[i].fade_speed;
        if (stars[i].brightness > stars[i].target_brightness) {
          stars[i].brightness = stars[i].target_brightness;
        }
      } else if (stars[i].brightness > stars[i].target_brightness) {
        stars[i].brightness -= stars[i].fade_speed;
        if (stars[i].brightness < stars[i].target_brightness) {
          stars[i].brightness = stars[i].target_brightness;
        }
      }

      // Deactivate completely faded stars
      if (stars[i].brightness <= 0.01f) {
        stars[i].active = false;
        stars[i].brightness = 0.0f;
      }
    }

    // Clear all LEDs to black background
    for (int i = 0; i < LED_NUMBERS; i++) {
      params->led_strip_pixels[i * 3 + 0] = 0; // Green
      params->led_strip_pixels[i * 3 + 1] = 0; // Red
      params->led_strip_pixels[i * 3 + 2] = 0; // Blue
    }

    // Render active stars
    for (int i = 0; i < MAX_STARS; i++) {
      if (!stars[i].active || stars[i].brightness <= 0.01f) {
        continue;
      }

      int pos = stars[i].position;

#if LED_SHOULD_ROUND == 1
      if (is_corner_led(pos, threshold)) {
        continue; // Skip corner LEDs
      }
#endif

      // Set star color based on type
      uint8_t base_brightness = (uint8_t)(255 * stars[i].brightness);

      switch (stars[i].color_type) {
      case 0: // Cool white
        red = base_brightness;
        green = base_brightness;
        blue = (uint8_t)(base_brightness * 1.2f);
        if (blue > 255)
          blue = 255;
        break;
      case 1: // Warm white
        red = base_brightness;
        green = (uint8_t)(base_brightness * 0.8f);
        blue = (uint8_t)(base_brightness * 0.4f);
        break;
      case 2: // Blue-white
        red = (uint8_t)(base_brightness * 0.8f);
        green = (uint8_t)(base_brightness * 0.9f);
        blue = base_brightness;
        break;
      default:
        red = green = blue = base_brightness;
        break;
      }

      // Apply global brightness
      red = (red * params->brightness) / 255;
      green = (green * params->brightness) / 255;
      blue = (blue * params->brightness) / 255;

      // Set pixel (GRB format)
      params->led_strip_pixels[pos * 3 + 0] = green;
      params->led_strip_pixels[pos * 3 + 1] = red;
      params->led_strip_pixels[pos * 3 + 2] = blue;
    }

    // Transmit to LED strip
    ESP_ERROR_CHECK(rmt_transmit(
        params->led_chan, params->led_encoder, params->led_strip_pixels,
        params->pixel_buffer_size, &params->tx_config));
    ESP_ERROR_CHECK(rmt_tx_wait_all_done(params->led_chan, pdMS_TO_TICKS(100)));

    vTaskDelay(pdMS_TO_TICKS(50)); // 20 FPS for smooth twinkling
  }

  // Clear LED matrix before task termination
  clear_led_matrix(params);

  params->last_task_handle = params->task_handle;
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
    vTaskDelay(pdMS_TO_TICKS(30));
  }

  clear_led_matrix(params);

  params->last_task_handle = params->task_handle;
  params->task_handle = NULL;
  vTaskDelete(NULL);
}
