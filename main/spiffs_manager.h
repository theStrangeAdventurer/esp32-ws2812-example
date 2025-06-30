#ifndef SPIFFS_MANAGER_H
#define SPIFFS_MANAGER_H

#include "esp_err.h"
#include <stddef.h>

/**
 * @brief Initialize SPIFFS filesystem
 *
 * Mounts SPIFFS partition with default configuration.
 * If mount fails, attempts to format the partition and retry.
 *
 * @return esp_err_t ESP_OK on success, error code otherwise
 */
esp_err_t spiffs_manager_init(void);

/**
 * @brief Get SPIFFS partition information
 *
 * @param total Pointer to store total partition size in bytes
 * @param used Pointer to store used space in bytes
 * @return esp_err_t ESP_OK on success, error code otherwise
 */
esp_err_t spiffs_manager_get_info(size_t *total, size_t *used);

/**
 * @brief Deinitialize SPIFFS filesystem
 *
 * Unmounts SPIFFS partition and frees resources
 */
void spiffs_manager_deinit(void);

#endif // SPIFFS_MANAGER_H
