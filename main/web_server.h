/*
 * Web Server Module for LED Strip Control
 * 
 * This module provides HTTP API endpoints for controlling LED strip effects
 * via web interface or REST API calls.
 */

#ifndef WEB_SERVER_H
#define WEB_SERVER_H

#include "esp_err.h"
#include "effect_manager.h"

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief Initialize and start the web server
 * 
 * @param effect_mgr Pointer to the effect manager instance
 * @return esp_err_t ESP_OK on success, error code otherwise
 */
esp_err_t web_server_init(effect_manager_t *effect_mgr);

/**
 * @brief Stop the web server
 * 
 * @return esp_err_t ESP_OK on success, error code otherwise
 */
esp_err_t web_server_stop(void);

/**
 * @brief Check if web server is running
 * 
 * @return true if server is running, false otherwise
 */
bool web_server_is_running(void);

/**
 * @brief Get server port number
 * 
 * @return uint16_t Port number (80 by default)
 */
uint16_t web_server_get_port(void);

#ifdef __cplusplus
}
#endif

#endif // WEB_SERVER_H