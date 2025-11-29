#ifndef WIFI_MANAGER_H
#define WIFI_MANAGER_H

#include "esp_err.h"
#include <stdbool.h>

esp_err_t wifi_manager_init_ap(const char *ssid, const char *password,
                               uint8_t channel, uint8_t max_conn);

/**
 * @brief Initialize WiFi in station mode and connect to configured network
 *
 * @param ssid WiFi network name
 * @param password WiFi password
 * @return esp_err_t ESP_OK on success
 */
esp_err_t wifi_manager_init_sta(const char *ssid, const char *password);

/**
 * @brief Check if WiFi is connected
 *
 * @return true if connected, false otherwise
 */
bool wifi_manager_is_connected(void);

/**
 * @brief Check if device is in AP mode
 *
 * @return true if in AP mode, false otherwise
 */
bool wifi_manager_is_ap_mode(void);

/**
 * @brief Deinitialize WiFi manager
 */
void wifi_manager_deinit(void);

#endif // WIFI_MANAGER_H
