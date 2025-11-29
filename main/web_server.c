/*
 * Web Server Implementation for LED Strip Control
 */

#include "web_server.h"
#include "cJSON.h"
#include "esp_http_server.h"
#include "esp_log.h"
#include "mdns.h"
#include "nvs.h"
#include "wifi_manager.h"
#include <fcntl.h> // For open() and O_* constants
#include <stdint.h>
#include <string.h>
#include <unistd.h> // For close() and write()

#define UPLOAD_BUFFER_SIZE 4096 // Уменьшаем буфер до 4KB
#define MDNS_HOSTNAME "lamp-01"
#define MIN(a, b) ((a) < (b) ? (a) : (b)) // Добавляем макрос MIN
#define SCALE_TO_255(x)                                                        \
  ((uint8_t)((x) * 2.55)) // Макрос для перевода 0-100 в 0-255
#define SCALE_TO_100(x)                                                        \
  ((uint8_t)((x) / 2.55)) // Макрос для перевода 0-255 в 0-100

static char *cached_index_html = NULL;
static size_t cached_index_len = 0;
static const char *TAG = "web_server";
static httpd_handle_t server = NULL;
static effect_manager_t *g_effect_manager = NULL;
static uint16_t server_port = 80;

const char *default_html_response =
    "<!doctype html>\n"
    "<html lang=\"en\">\n"
    "<head>\n"
    "    <meta charset=\"utf-8\">\n"
    "    <meta name=\"viewport\" content=\"width=device-width, "
    "initial-scale=1.0\">\n"
    "    <title>error</title>\n"
    "</head>\n"
    "<body>\n"
    "    <h1>web interface unavailable</h1>\n"
    "    <p>please upload the required files first.</p>\n"
    "</body>\n"
    "</html>";

// HTTP обработчик для страницы настройки WiFi
static esp_err_t wifi_config_page_handler(httpd_req_t *req) {
  ESP_LOGI(TAG, "WiFi config page handler called");
  const char *html_response =
      "<!DOCTYPE html>\n"
      "<html lang=\"ru\">\n"
      "<head>\n"
      "    <meta charset=\"UTF-8\">\n"
      "    <meta name=\"viewport\" content=\"width=device-width, initial-scale=1.0\">\n"
      "    <title>Настройка WiFi - LED Лампа</title>\n"
      "    <style>\n"
      "        * { margin: 0; padding: 0; box-sizing: border-box; }\n"
      "        body { font-family: 'Segoe UI', sans-serif; background: linear-gradient(135deg, #667eea 0%, #764ba2 100%); min-height: 100vh; display: flex; align-items: center; justify-content: center; padding: 20px; }\n"
      "        .container { background: white; border-radius: 15px; box-shadow: 0 20px 40px rgba(0,0,0,0.1); padding: 40px; max-width: 500px; width: 100%; }\n"
      "        .header { text-align: center; margin-bottom: 30px; }\n"
      "        .header h1 { color: #333; margin-bottom: 10px; font-size: 28px; }\n"
      "        .header p { color: #666; font-size: 16px; line-height: 1.5; }\n"
      "        .form-group { margin-bottom: 20px; }\n"
      "        label { display: block; margin-bottom: 8px; color: #333; font-weight: 600; }\n"
      "        input[type=\"text\"], input[type=\"password\"] { width: 100%; padding: 12px 15px; border: 2px solid #e1e5e9; border-radius: 8px; font-size: 16px; transition: border-color 0.3s; }\n"
      "        input:focus { outline: none; border-color: #667eea; }\n"
      "        .btn { width: 100%; padding: 15px; background: linear-gradient(135deg, #667eea 0%, #764ba2 100%); color: white; border: none; border-radius: 8px; font-size: 16px; font-weight: 600; cursor: pointer; transition: transform 0.2s; }\n"
      "        .btn:hover { transform: translateY(-2px); }\n"
      "        .status { margin-top: 20px; padding: 15px; border-radius: 8px; text-align: center; font-weight: 600; display: none; }\n"
      "        .status.success { background: #d4edda; color: #155724; border: 1px solid #c3e6cb; }\n"
      "        .status.error { background: #f8d7da; color: #721c24; border: 1px solid #f5c6cb; }\n"
      "        .info-box { background: #f8f9fa; border: 1px solid #e9ecef; border-radius: 8px; padding: 15px; margin-bottom: 20px; }\n"
      "        .device-info { background: #e7f3ff; border: 1px solid #b3d9ff; border-radius: 8px; padding: 15px; margin-bottom: 20px; }\n"
      "    </style>\n"
      "</head>\n"
      "<body>\n"
      "    <div class=\"container\">\n"
      "        <div class=\"header\">\n"
      "            <h1>⚡ Настройка WiFi</h1>\n"
      "            <p>Подключите вашу LED лампу к WiFi сети</p>\n"
      "        </div>\n"
      "        \n"
      "        <div class=\"device-info\">\n"
      "            <h3>📱 Информация об устройстве</h3>\n"
      "            <p><strong>Текущий режим:</strong> Точка доступа</p>\n"
      "            <p><strong>IP адрес:</strong> 192.168.4.1</p>\n"
      "            <p><strong>Доступ по:</strong> http://lamp-01.local или http://192.168.4.1</p>\n"
      "        </div>\n"
      "        \n"
      "        <div class=\"info-box\">\n"
      "            <h3>ℹ️ Инструкция</h3>\n"
      "            <p>Введите данные вашей WiFi сети. После сохранения устройство перезагрузится и попытается подключиться к указанной сети.</p>\n"
      "        </div>\n"
      "        \n"
      "        <form id=\"wifi-config-form\">\n"
      "            <div class=\"form-group\">\n"
      "                <label for=\"ssid\">Имя WiFi сети (SSID):</label>\n"
      "                <input type=\"text\" id=\"ssid\" name=\"ssid\" required placeholder=\"Введите имя вашей WiFi сети\">\n"
      "            </div>\n"
      "            \n"
      "            <div class=\"form-group\">\n"
      "                <label for=\"password\">Пароль WiFi:</label>\n"
      "                <input type=\"password\" id=\"password\" name=\"password\" placeholder=\"Введите пароль (если есть)\">\n"
      "            </div>\n"
      "            \n"
      "            <button type=\"submit\" class=\"btn\">💾 Сохранить настройки</button>\n"
      "        </form>\n"
      "        \n"
      "        <div id=\"status\" class=\"status\"></div>\n"
      "    </div>\n"
      "\n"
      "    <script>\n"
      "        document.getElementById('wifi-config-form').addEventListener('submit', function(e) {\n"
      "            e.preventDefault();\n"
      "            \n"
      "            const ssid = document.getElementById('ssid').value.trim();\n"
      "            const password = document.getElementById('password').value;\n"
      "            \n"
      "            if (!ssid) {\n"
      "                showStatus('Пожалуйста, введите имя WiFi сети', 'error');\n"
      "                return;\n"
      "            }\n"
      "            \n"
      "            const status = document.getElementById('status');\n"
      "            status.style.display = 'block';\n"
      "            status.className = 'status';\n"
      "            status.textContent = 'Сохраняем настройки...';\n"
      "            \n"
      "            fetch('/api/wifi/config', {\n"
      "                method: 'POST',\n"
      "                headers: {\n"
      "                    'Content-Type': 'application/json',\n"
      "                },\n"
      "                body: JSON.stringify({\n"
      "                    ssid: ssid,\n"
      "                    password: password\n"
      "                })\n"
      "            })\n"
      "            .then(response => response.json())\n"
      "            .then(data => {\n"
      "                if (data.status === 'success') {\n"
      "                    showStatus(data.message + ' Устройство перезагрузится через 2 секунды...', 'success');\n"
      "                    setTimeout(() => {\n"
      "                        showStatus('Перезагрузка...', 'success');\n"
      "                    }, 2000);\n"
      "                } else {\n"
      "                    showStatus(data.message || 'Ошибка при сохранении настроек', 'error');\n"
      "                }\n"
      "            })\n"
      "            .catch(error => {\n"
      "                console.error('Error:', error);\n"
      "                showStatus('Ошибка сети. Проверьте подключение.', 'error');\n"
      "            });\n"
      "        });\n"
      "        \n"
      "        function showStatus(message, type) {\n"
      "            const status = document.getElementById('status');\n"
      "            status.style.display = 'block';\n"
      "            status.className = `status ${type}`;\n"
      "            status.textContent = message;\n"
      "        }\n"
      "    </script>\n"
      "</body>\n"
      "</html>";

  httpd_resp_set_type(req, "text/html");
  httpd_resp_set_hdr(req, "Access-Control-Allow-Origin", "*");
  return httpd_resp_send(req, html_response, strlen(html_response));
}

static esp_err_t wifi_config_post_handler(httpd_req_t *req) {
  char buf[512];
  int ret = httpd_req_recv(req, buf, sizeof(buf) - 1);
  if (ret <= 0) {
    httpd_resp_send_500(req);
    return ESP_FAIL;
  }
  buf[ret] = '\0';

  cJSON *json = cJSON_Parse(buf);
  if (json == NULL) {
    httpd_resp_set_hdr(req, "Access-Control-Allow-Origin", "*");
    httpd_resp_send_err(req, HTTPD_400_BAD_REQUEST, "Invalid JSON");
    return ESP_FAIL;
  }

  cJSON *ssid = cJSON_GetObjectItem(json, "ssid");
  cJSON *password = cJSON_GetObjectItem(json, "password");

  if (!cJSON_IsString(ssid) || ssid->valuestring[0] == '\0') {
    httpd_resp_set_hdr(req, "Access-Control-Allow-Origin", "*");
    httpd_resp_send_err(req, HTTPD_400_BAD_REQUEST, "Invalid SSID");
    cJSON_Delete(json);
    return ESP_FAIL;
  }

  // Сохраняем настройки WiFi в NVS
  nvs_handle_t nvs_handle;
  esp_err_t err = nvs_open("wifi_config", NVS_READWRITE, &nvs_handle);
  if (err == ESP_OK) {
    nvs_set_str(nvs_handle, "ssid", ssid->valuestring);
    if (cJSON_IsString(password)) {
      nvs_set_str(nvs_handle, "password", password->valuestring);
    } else {
      nvs_set_str(nvs_handle, "password", "");
    }
    nvs_commit(nvs_handle);
    nvs_close(nvs_handle);
  }

  httpd_resp_set_hdr(req, "Access-Control-Allow-Origin", "*");
  cJSON *response = cJSON_CreateObject();

  if (err == ESP_OK) {
    cJSON_AddStringToObject(response, "status", "success");
    cJSON_AddStringToObject(response, "message",
                            "WiFi settings saved. Device will restart.");

    char *response_string = cJSON_Print(response);
    httpd_resp_set_type(req, "application/json");
    httpd_resp_send(req, response_string, strlen(response_string));
    free(response_string);

    // Перезагружаем устройство через 2 секунды
    vTaskDelay(pdMS_TO_TICKS(2000));
    esp_restart();
  } else {
    cJSON_AddStringToObject(response, "status", "error");
    cJSON_AddStringToObject(response, "message",
                            "Failed to save WiFi settings");

    char *response_string = cJSON_Print(response);
    httpd_resp_set_type(req, "application/json");
    httpd_resp_send(req, response_string, strlen(response_string));
    free(response_string);
  }

  cJSON_Delete(json);
  return ESP_OK;
}

static esp_err_t status_get_handler(httpd_req_t *req) {
  if (g_effect_manager == NULL) {
    httpd_resp_send_500(req);
    return ESP_FAIL;
  }

  cJSON *json = cJSON_CreateObject();

  effect_status_t status;
  esp_err_t ret = effect_manager_get_status(g_effect_manager, &status);

  if (ret == ESP_OK) {
    cJSON_AddStringToObject(json, "current_effect", status.current_name);
    cJSON_AddNumberToObject(json, "current_effect_index",
                            status.current_effect);
    cJSON_AddNumberToObject(json, "total_effects", status.total_effects);
    cJSON_AddNumberToObject(
        json, "brightness",
        SCALE_TO_100(effect_manager_get_brightness(g_effect_manager)));
    cJSON_AddBoolToObject(json, "is_running",
                          g_effect_manager->params->running);

    // Добавляем список доступных эффектов
    cJSON *effects_array = cJSON_CreateArray();
    // Создаем массив эффектов из статуса
    char *effects_copy = strdup(status.effects_list);
    char *token = strtok(effects_copy, ",");
    while (token != NULL) {
      cJSON_AddItemToArray(effects_array, cJSON_CreateString(token));
      token = strtok(NULL, ",");
    }
    free(effects_copy);
    cJSON_AddItemToObject(json, "available_effects", effects_array);
  } else {
    cJSON_AddStringToObject(json, "error", "Failed to get status");
  }

  char *json_string = cJSON_Print(json);
  httpd_resp_set_type(req, "application/json");
  httpd_resp_set_hdr(req, "Access-Control-Allow-Origin", "*");
  httpd_resp_send(req, json_string, strlen(json_string));

  free(json_string);
  cJSON_Delete(json);
  return ESP_OK;
}

// HTTP обработчик для получения списка эффектов
static esp_err_t effects_list_handler(httpd_req_t *req) {
  if (g_effect_manager == NULL) {
    httpd_resp_send_500(req);
    return ESP_FAIL;
  }

  effect_status_t status;
  esp_err_t ret = effect_manager_get_status(g_effect_manager, &status);

  if (ret != ESP_OK) {
    httpd_resp_send_500(req);
    return ESP_FAIL;
  }

  cJSON *json = cJSON_CreateObject();
  cJSON *effects_array = cJSON_CreateArray();

  // Парсим список эффектов
  char *effects_copy = strdup(status.effects_list);
  char *token = strtok(effects_copy, ",");
  while (token != NULL) {
    cJSON_AddItemToArray(effects_array, cJSON_CreateString(token));
    token = strtok(NULL, ",");
  }
  free(effects_copy);

  cJSON_AddItemToObject(json, "effects", effects_array);
  cJSON_AddNumberToObject(json, "total", status.total_effects);
  cJSON_AddNumberToObject(json, "current_index", status.current_effect);

  char *json_string = cJSON_Print(json);
  httpd_resp_set_type(req, "application/json");
  httpd_resp_set_hdr(req, "Access-Control-Allow-Origin", "*");
  httpd_resp_send(req, json_string, strlen(json_string));

  free(json_string);
  cJSON_Delete(json);
  return ESP_OK;
}

// HTTP обработчик для переключения эффекта
static esp_err_t effect_post_handler(httpd_req_t *req) {
  if (g_effect_manager == NULL) {
    httpd_resp_send_500(req);
    return ESP_FAIL;
  }

  char buf[200];
  int ret = httpd_req_recv(req, buf, sizeof(buf) - 1);
  if (ret <= 0) {
    httpd_resp_send_500(req);
    return ESP_FAIL;
  }
  buf[ret] = '\0';

  cJSON *json = cJSON_Parse(buf);
  if (json == NULL) {
    httpd_resp_set_hdr(req, "Access-Control-Allow-Origin", "*");
    httpd_resp_send_err(req, HTTPD_400_BAD_REQUEST, "Invalid JSON");
    return ESP_FAIL;
  }

  cJSON *effect = cJSON_GetObjectItem(json, "effect");
  cJSON *effect_index = cJSON_GetObjectItem(json, "index");

  esp_err_t err = ESP_FAIL;

  if (cJSON_IsString(effect)) {
    // Устанавливаем эффект по имени
    err = effect_manager_set_effect_by_name(g_effect_manager,
                                            effect->valuestring);
  } else if (cJSON_IsNumber(effect_index)) {
    // Устанавливаем эффект по индексу
    err = effect_manager_switch_to(g_effect_manager, effect_index->valueint);
  }

  httpd_resp_set_hdr(req, "Access-Control-Allow-Origin", "*");
  if (err == ESP_OK) {
    cJSON *response = cJSON_CreateObject();
    cJSON_AddStringToObject(response, "status", "success");
    cJSON_AddStringToObject(response, "current_effect",
                            effect_manager_get_current_name(g_effect_manager));

    char *response_string = cJSON_Print(response);
    httpd_resp_set_type(req, "application/json");
    httpd_resp_send(req, response_string, strlen(response_string));
    free(response_string);
    cJSON_Delete(response);
  } else {
    httpd_resp_send_err(req, HTTPD_400_BAD_REQUEST,
                        "Invalid effect name or index");
  }

  cJSON_Delete(json);
  return ESP_OK;
}

// HTTP обработчик для переключения на следующий эффект
static esp_err_t next_effect_handler(httpd_req_t *req) {
  if (g_effect_manager == NULL) {
    httpd_resp_send_500(req);
    return ESP_FAIL;
  }

  esp_err_t err = effect_manager_switch_next(g_effect_manager);

  httpd_resp_set_hdr(req, "Access-Control-Allow-Origin", "*");
  if (err == ESP_OK) {
    cJSON *response = cJSON_CreateObject();
    cJSON_AddStringToObject(response, "status", "success");
    cJSON_AddStringToObject(response, "current_effect",
                            effect_manager_get_current_name(g_effect_manager));
    cJSON_AddNumberToObject(response, "current_index",
                            effect_manager_get_current_index(g_effect_manager));

    char *response_string = cJSON_Print(response);
    httpd_resp_set_type(req, "application/json");
    httpd_resp_send(req, response_string, strlen(response_string));
    free(response_string);
    cJSON_Delete(response);
  } else {
    httpd_resp_send_err(req, HTTPD_500_INTERNAL_SERVER_ERROR,
                        "Failed to switch effect");
  }

  return ESP_OK;
}

// HTTP обработчик для изменения яркости
static esp_err_t brightness_post_handler(httpd_req_t *req) {
  if (g_effect_manager == NULL) {
    httpd_resp_send_500(req);
    return ESP_FAIL;
  }

  char buf[200];
  int ret = httpd_req_recv(req, buf, sizeof(buf) - 1);
  if (ret <= 0) {
    httpd_resp_send_500(req);
    return ESP_FAIL;
  }
  buf[ret] = '\0';

  cJSON *json = cJSON_Parse(buf);
  if (json == NULL) {
    httpd_resp_set_hdr(req, "Access-Control-Allow-Origin", "*");
    httpd_resp_send_err(req, HTTPD_400_BAD_REQUEST, "Invalid JSON");
    return ESP_FAIL;
  }

  cJSON *brightness = cJSON_GetObjectItem(json, "brightness");
  cJSON *delta = cJSON_GetObjectItem(json, "delta");

  esp_err_t err = ESP_FAIL;
  uint8_t new_brightness = 0;

  if (cJSON_IsNumber(brightness)) {
    // Устанавливаем абсолютное значение яркости
    uint8_t brightness_value = SCALE_TO_255((uint8_t)brightness->valueint);
    if (brightness_value > 255)
      brightness_value = 255;
    if (brightness_value < 1)
      brightness_value = 1;

    err = effect_manager_set_brightness(g_effect_manager, brightness_value);
    new_brightness = brightness_value;
  } else if (cJSON_IsNumber(delta)) {
    // Изменяем яркость на delta
    int8_t delta_value = (int8_t)delta->valueint;
    err = effect_manager_adjust_brightness(g_effect_manager, delta_value);
    new_brightness = effect_manager_get_brightness(g_effect_manager);
  }

  httpd_resp_set_hdr(req, "Access-Control-Allow-Origin", "*");
  if (err == ESP_OK) {
    cJSON *response = cJSON_CreateObject();
    cJSON_AddStringToObject(response, "status", "success");
    cJSON_AddNumberToObject(response, "brightness", new_brightness);

    char *response_string = cJSON_Print(response);
    httpd_resp_set_type(req, "application/json");
    httpd_resp_send(req, response_string, strlen(response_string));
    free(response_string);
    cJSON_Delete(response);
  } else {
    httpd_resp_send_err(req, HTTPD_400_BAD_REQUEST,
                        "Invalid brightness value or delta");
  }

  cJSON_Delete(json);
  return ESP_OK;
}

// HTTP обработчик для управления питанием (запуск/остановка эффектов)
static esp_err_t power_post_handler(httpd_req_t *req) {
  if (g_effect_manager == NULL) {
    httpd_resp_send_500(req);
    return ESP_FAIL;
  }

  char buf[200];
  int ret = httpd_req_recv(req, buf, sizeof(buf) - 1);
  if (ret <= 0) {
    httpd_resp_send_500(req);
    return ESP_FAIL;
  }
  buf[ret] = '\0';

  cJSON *json = cJSON_Parse(buf);
  if (json == NULL) {
    httpd_resp_set_hdr(req, "Access-Control-Allow-Origin", "*");
    httpd_resp_send_err(req, HTTPD_400_BAD_REQUEST, "Invalid JSON");
    return ESP_FAIL;
  }

  cJSON *power = cJSON_GetObjectItem(json, "power");
  if (cJSON_IsBool(power)) {
    if (cJSON_IsTrue(power)) {
      // Включаем эффекты - устанавливаем флаг running в true
      g_effect_manager->params->running = true;
      effect_manager_start_current(g_effect_manager);
      ESP_LOGI(TAG, "Effects enabled via web API");
    } else {
      // Выключаем эффекты - останавливаем текущий эффект
      g_effect_manager->params->running = false;
      effect_manager_stop_current(g_effect_manager);
      ESP_LOGI(TAG, "Effects disabled via web API");
    }

    httpd_resp_set_hdr(req, "Access-Control-Allow-Origin", "*");
    cJSON *response = cJSON_CreateObject();
    cJSON_AddStringToObject(response, "status", "success");
    cJSON_AddBoolToObject(response, "power", g_effect_manager->params->running);

    char *response_string = cJSON_Print(response);
    httpd_resp_set_type(req, "application/json");
    httpd_resp_send(req, response_string, strlen(response_string));
    free(response_string);
    cJSON_Delete(response);
  } else {
    httpd_resp_set_hdr(req, "Access-Control-Allow-Origin", "*");
    httpd_resp_send_err(req, HTTPD_400_BAD_REQUEST, "Missing power parameter");
  }

  cJSON_Delete(json);
  return ESP_OK;
}

// Обработчик для CORS preflight запросов
static esp_err_t options_handler(httpd_req_t *req) {
  httpd_resp_set_hdr(req, "Access-Control-Allow-Origin", "*");
  httpd_resp_set_hdr(req, "Access-Control-Allow-Methods", "GET, POST, OPTIONS");
  httpd_resp_set_hdr(req, "Access-Control-Allow-Headers", "Content-Type");
  httpd_resp_send(req, NULL, 0);
  return ESP_OK;
}

esp_err_t cache_index_html() {
  FILE *f = fopen("/spiffs/index.html", "rb");
  if (!f) {
    ESP_LOGE(TAG, "Failed to open index.html");
    return ESP_FAIL;
  }

  // Получаем размер файла
  fseek(f, 0, SEEK_END);
  cached_index_len = ftell(f);
  fseek(f, 0, SEEK_SET);

  // Выделяем память (обычная куча, не DMA)
  cached_index_html = malloc(cached_index_len + 1);
  if (!cached_index_html) {
    fclose(f);
    ESP_LOGE(TAG, "Failed to allocate cache buffer");
    return ESP_FAIL;
  }

  size_t read_bytes = fread(cached_index_html, 1, cached_index_len, f);
  fclose(f);

  if (read_bytes != cached_index_len) {
    free(cached_index_html);
    cached_index_html = NULL;
    ESP_LOGE(TAG, "Failed to read index.html");
    return ESP_FAIL;
  }

  cached_index_html[cached_index_len] = '\0';
  ESP_LOGI(TAG, "Cached index.html (%d bytes)", (int)cached_index_len);
  return ESP_OK;
}

int check_webapp_uploaded() {
  FILE *f = NULL;
  // Open renamed file for reading
  ESP_LOGI(TAG, "Check index.html exists");
  f = fopen("/spiffs/index.html", "r");
  if (f == NULL) {
    return 0;
  }
  fclose(f);
  return 1;
}

esp_err_t root_handler(httpd_req_t *req) {
  ESP_LOGI(TAG, "Root handler called, URI: %s", req->uri);
  
  // В AP режиме всегда показываем страницу настройки WiFi
  if (wifi_manager_is_ap_mode()) {
    ESP_LOGI(TAG, "AP mode detected, showing WiFi config page");
    return wifi_config_page_handler(req);
  } else {
    ESP_LOGI(TAG, "STA mode detected, checking for web application");
  }

  const int is_webapp_uploaded = check_webapp_uploaded();
  
  if (!is_webapp_uploaded) {
    ESP_LOGW(TAG, "Web application not yet uploaded");
    return httpd_resp_send(req, default_html_response,
                           strlen(default_html_response));
  }
  if (!cached_index_html) {
    cache_index_html(); // Trying to cache existing file
  }
  if (!cached_index_html) {
    ESP_LOGE(TAG, "Cache not initialized");
    return httpd_resp_send_500(req);
  }

  return httpd_resp_send(req, cached_index_html, cached_index_len);
}

esp_err_t upload_handler(httpd_req_t *req) {
  char *buf = malloc(UPLOAD_BUFFER_SIZE);
  if (!buf) {
    httpd_resp_send_500(req);
    return ESP_FAIL;
  }

  char filename[128] = {0};
  int fd = -1; // Use file descriptor instead of FILE*
  const char *fail_resp = "{\"result\": false}";
  const char *success_resp = "{\"result\": true}";
  char boundary[70] = {0};
  bool file_complete = false;
  size_t total_written = 0;

  // Получаем boundary из Content-Type
  char content_type[128];
  if (httpd_req_get_hdr_value_str(req, "Content-Type", content_type,
                                  sizeof(content_type)) != ESP_OK) {
    ESP_LOGE(TAG, "Failed to get Content-Type header");
    free(buf);
    httpd_resp_send(req, fail_resp, strlen(fail_resp));
    return ESP_FAIL;
  }

  char *boundary_start = strstr(content_type, "boundary=");
  if (!boundary_start) {
    ESP_LOGE(TAG, "Boundary not found in Content-Type");
    free(buf);
    httpd_resp_send(req, fail_resp, strlen(fail_resp));
    return ESP_FAIL;
  }
  strncpy(boundary, boundary_start + 9, sizeof(boundary) - 1);
  ESP_LOGI(TAG, "Boundary: %s", boundary);

  // Обработка данных
  int remaining = req->content_len;
  bool is_first_chunk = true;
  char *data_start = NULL;
  size_t data_len = 0;

  while (remaining > 0 && !file_complete) {
    int received = httpd_req_recv(req, buf, MIN(remaining, UPLOAD_BUFFER_SIZE));
    if (received <= 0) {
      if (received == HTTPD_SOCK_ERR_TIMEOUT) {
        ESP_LOGE(TAG, "Receive timeout");
      } else {
        ESP_LOGE(TAG, "Receive failed or connection closed");
      }
      if (fd != -1)
        close(fd);
      free(buf);
      httpd_resp_send(req, fail_resp, strlen(fail_resp));
      return ESP_FAIL;
    }

    if (is_first_chunk) {
      // Парсинг имени файла из заголовков
      char *filename_start = strstr(buf, "filename=\"");
      if (!filename_start) {
        ESP_LOGE(TAG, "Filename not found in headers");
        free(buf);
        httpd_resp_send(req, fail_resp, strlen(fail_resp));
        return ESP_FAIL;
      }
      filename_start += 10;
      char *filename_end = strchr(filename_start, '"');
      if (!filename_end) {
        ESP_LOGE(TAG, "Malformed filename header");
        free(buf);
        httpd_resp_send(req, fail_resp, strlen(fail_resp));
        return ESP_FAIL;
      }
      strncpy(filename, filename_start, filename_end - filename_start);

      // Открываем файл в SPIFFS
      char filepath[256];
      snprintf(filepath, sizeof(filepath), "/spiffs/%s", filename);
      fd = open(filepath, O_WRONLY | O_CREAT | O_TRUNC, 0666);
      if (fd == -1) {
        ESP_LOGE(TAG, "Failed to open file %s", filepath);
        free(buf);
        httpd_resp_send(req, fail_resp, strlen(fail_resp));
        return ESP_FAIL;
      }

      // Находим начало данных файла (после \r\n\r\n)
      data_start = strstr(buf, "\r\n\r\n");
      if (data_start) {
        data_start += 4;
        data_len = buf + received - data_start;
      } else {
        data_start = buf;
        data_len = received;
      }
      is_first_chunk = false;
    } else {
      data_start = buf;
      data_len = received;
    }

    // Ищем boundary в текущем чанке
    char *boundary_pos =
        memmem(data_start, data_len, boundary, strlen(boundary));
    if (boundary_pos) {
      // Нашли конец файла
      size_t to_write = boundary_pos - data_start - 2; // Учитываем \r\n
      write(fd, data_start, to_write);
      total_written += to_write;
      file_complete = true;
      ESP_LOGI(TAG, "File end detected at %d bytes", (int)to_write);
    } else {
      // Записываем весь чанк (если boundary не найден)
      write(fd, data_start, data_len);
      total_written += data_len;
    }

    remaining -= received;
  }

  if (fd != -1)
    close(fd);
  free(buf);

  if (file_complete) {
    ESP_LOGI(TAG, "File %s uploaded successfully, size: %d bytes", filename,
             (int)total_written);
    cache_index_html();
    httpd_resp_send(req, success_resp, strlen(success_resp));
    return ESP_OK;
  } else {
    ESP_LOGE(TAG, "File upload incomplete");
    httpd_resp_send(req, fail_resp, strlen(fail_resp));
    return ESP_FAIL;
  }
}

esp_err_t init_mdns() {
  esp_err_t err = mdns_init();
  if (err != ESP_OK) {
    ESP_LOGE(TAG, "mDNS Init failed: %s", esp_err_to_name(err));
    return err;
  }

  // Установить имя хоста
  err = mdns_hostname_set(MDNS_HOSTNAME);
  if (err != ESP_OK) {
    ESP_LOGE(TAG, "mDNS Set hostname failed: %s", esp_err_to_name(err));
    return err;
  }
  err = mdns_service_add(NULL, "_http", "_tcp", 80, NULL, 0);
  if (err != ESP_OK) {
    ESP_LOGE(TAG, "mDNS Add service failed: %s", esp_err_to_name(err));
    return err;
  }

  ESP_LOGI(TAG, "mDNS started: http://%s.local", MDNS_HOSTNAME);
  return ESP_OK;
}

esp_err_t web_server_init(effect_manager_t *effect_mgr) {

  if (effect_mgr == NULL) {
    ESP_LOGE(TAG, "Effect manager is NULL");
    return ESP_ERR_INVALID_ARG;
  }

  if (server != NULL) {
    ESP_LOGW(TAG, "Web server already running");
    return ESP_OK;
  }

  g_effect_manager = effect_mgr;

  // Initialize mDNS after WiFi is ready
  ESP_LOGI(TAG, "Initializing mDNS...");
  esp_err_t mdns_ret = init_mdns();
  if (mdns_ret != ESP_OK) {
    ESP_LOGW(TAG, "mDNS initialization failed, continuing without mDNS");
  } else {
    ESP_LOGI(TAG, "mDNS initialized successfully");
  }

  httpd_config_t config = HTTPD_DEFAULT_CONFIG();
  config.server_port = server_port;
  config.max_uri_handlers = 20;
  config.stack_size = 8192;

  esp_err_t ret = httpd_start(&server, &config);
  if (ret != ESP_OK) {
    ESP_LOGE(TAG, "Error starting HTTP server: %s", esp_err_to_name(ret));
    return ret;
  }
  
  ESP_LOGI(TAG, "HTTP server successfully started on port %d", server_port);

  // Log network information
  esp_netif_t *netif = esp_netif_get_handle_from_ifkey("WIFI_AP_DEF");
  if (netif) {
    esp_netif_ip_info_t ip_info;
    if (esp_netif_get_ip_info(netif, &ip_info) == ESP_OK) {
      ESP_LOGI(TAG, "AP IP Address: " IPSTR, IP2STR(&ip_info.ip));
      ESP_LOGI(TAG, "AP Gateway: " IPSTR, IP2STR(&ip_info.gw));
      ESP_LOGI(TAG, "AP Netmask: " IPSTR, IP2STR(&ip_info.netmask));
    }
  } else {
    ESP_LOGW(TAG, "Could not get AP network interface");
  }

  // Регистрация обработчиков API
  httpd_uri_t wifi_config_page_uri = {.uri = "/wifi-config",
                                      .method = HTTP_GET,
                                      .handler = wifi_config_page_handler,
                                      .user_ctx = NULL};
  httpd_register_uri_handler(server, &wifi_config_page_uri);

  httpd_uri_t wifi_config_uri = {.uri = "/api/wifi/config",
                                 .method = HTTP_POST,
                                 .handler = wifi_config_post_handler,
                                 .user_ctx = NULL};
  httpd_register_uri_handler(server, &wifi_config_uri);

  httpd_uri_t status_uri = {.uri = "/api/status",
                            .method = HTTP_GET,
                            .handler = status_get_handler,
                            .user_ctx = NULL};
  httpd_register_uri_handler(server, &status_uri);

  httpd_uri_t effects_uri = {.uri = "/api/effects",
                             .method = HTTP_GET,
                             .handler = effects_list_handler,
                             .user_ctx = NULL};
  httpd_register_uri_handler(server, &effects_uri);

  httpd_uri_t effect_uri = {.uri = "/api/effect",
                            .method = HTTP_POST,
                            .handler = effect_post_handler,
                            .user_ctx = NULL};
  httpd_register_uri_handler(server, &effect_uri);

  httpd_uri_t next_effect_uri = {.uri = "/api/effect/next",
                                 .method = HTTP_POST,
                                 .handler = next_effect_handler,
                                 .user_ctx = NULL};
  httpd_register_uri_handler(server, &next_effect_uri);

  httpd_uri_t brightness_uri = {.uri = "/api/brightness",
                                .method = HTTP_POST,
                                .handler = brightness_post_handler,
                                .user_ctx = NULL};
  httpd_register_uri_handler(server, &brightness_uri);

  httpd_uri_t power_uri = {.uri = "/api/power",
                           .method = HTTP_POST,
                           .handler = power_post_handler,
                           .user_ctx = NULL};

  httpd_register_uri_handler(server, &power_uri);
  httpd_uri_t uri_post_upload = {.uri = "/upload",
                                 .method = HTTP_POST,
                                 .handler = upload_handler,
                                 .user_ctx = NULL};

  httpd_register_uri_handler(server, &uri_post_upload);

  // CORS обработчики
  httpd_uri_t options_uri = {.uri = "/api/*",
                             .method = HTTP_OPTIONS,
                             .handler = options_handler,
                             .user_ctx = NULL};
  httpd_register_uri_handler(server, &options_uri);

  // Главная страница
  httpd_uri_t root_uri = {.uri = "/",
                          .method = HTTP_GET,
                          .handler = root_handler,
                          .user_ctx = NULL};
  httpd_register_uri_handler(server, &root_uri);



  ESP_LOGI(TAG, "HTTP server started on port %d", server_port);
  ESP_LOGI(TAG, "Web interface available at: http://[IP_ADDRESS]/");
  ESP_LOGI(TAG, "API endpoints available:");
  ESP_LOGI(TAG, "  GET  /api/status");
  ESP_LOGI(TAG, "  GET  /api/effects");
  ESP_LOGI(TAG, "  POST /api/effect");
  ESP_LOGI(TAG, "  POST /api/effect/next");
  ESP_LOGI(TAG, "  POST /api/brightness");
  ESP_LOGI(TAG, "  POST /api/power");

  return ESP_OK;
}

esp_err_t web_server_stop(void) {
  if (server == NULL) {
    ESP_LOGW(TAG, "Web server not running");
    return ESP_OK;
  }

  esp_err_t ret = httpd_stop(server);
  if (ret == ESP_OK) {
    server = NULL;
    g_effect_manager = NULL;
    ESP_LOGI(TAG, "Web server stopped");
  } else {
    ESP_LOGE(TAG, "Error stopping web server: %s", esp_err_to_name(ret));
  }

  return ret;
}

bool web_server_is_running(void) { return server != NULL; }

uint16_t web_server_get_port(void) { return server_port; }
