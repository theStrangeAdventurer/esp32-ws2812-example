#include "spiffs_manager.h"
#include "esp_log.h"
#include "esp_spiffs.h"

static const char *TAG = "spiffs_manager";

esp_err_t spiffs_manager_init(void) {
  ESP_LOGI(TAG, "Initializing SPIFFS");

  esp_vfs_spiffs_conf_t conf = {.base_path = "/spiffs",
                                .partition_label = NULL,
                                .max_files = 5,
                                .format_if_mount_failed = true};

  esp_err_t ret = esp_vfs_spiffs_register(&conf);

  if (ret != ESP_OK) {
    ESP_LOGE(TAG, "SPIFFS mount failed! (err=0x%x)", ret);
    if (ret == ESP_FAIL) {
      ESP_LOGE(TAG, "Formatting SPIFFS...");
      esp_spiffs_format(NULL); // Форматирование при ошибке
      ret = esp_vfs_spiffs_register(&conf);
      if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Failed to mount SPIFFS after formatting (%s)",
                 esp_err_to_name(ret));
        return ret;
      }
    } else if (ret == ESP_ERR_NOT_FOUND) {
      ESP_LOGE(TAG, "Failed to find SPIFFS partition");
      return ret;
    } else {
      ESP_LOGE(TAG, "Failed to initialize SPIFFS (%s)", esp_err_to_name(ret));
      return ret;
    }
  }

  size_t total = 0, used = 0;
  ret = esp_spiffs_info(NULL, &total, &used);
  if (ret != ESP_OK) {
    ESP_LOGE(TAG, "Failed to get SPIFFS partition information (%s)",
             esp_err_to_name(ret));
  } else {
    ESP_LOGI(TAG, "Partition size: total: %d, used: %d", (int)total, (int)used);
  }

  ESP_LOGI(TAG, "SPIFFS initialized successfully");
  return ESP_OK;
}

esp_err_t spiffs_manager_get_info(size_t *total, size_t *used) {
  if (!total || !used) {
    ESP_LOGE(TAG, "Invalid parameters for get_info");
    return ESP_ERR_INVALID_ARG;
  }

  esp_err_t ret = esp_spiffs_info(NULL, total, used);
  if (ret != ESP_OK) {
    ESP_LOGE(TAG, "Failed to get SPIFFS partition information (%s)",
             esp_err_to_name(ret));
  }
  return ret;
}

void spiffs_manager_deinit(void) {
  ESP_LOGI(TAG, "Deinitializing SPIFFS");
  esp_vfs_spiffs_unregister(NULL);
}
