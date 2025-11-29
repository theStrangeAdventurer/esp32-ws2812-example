#include "wifi_manager.h"
#include "esp_event.h"
#include "esp_log.h"
#include "esp_netif.h"
#include "esp_wifi.h"
#include "freertos/FreeRTOS.h"
#include "freertos/event_groups.h"
#include "freertos/task.h"
#include <string.h>

// For MAC2STR macro
#include "esp_mac.h"
// For IP4_ADDR macro
#include "lwip/ip4_addr.h"

#define WIFI_CONNECTED_BIT BIT0
#define WIFI_FAIL_BIT BIT1
#define WIFI_MAXIMUM_RETRY 3

static const char *TAG = "wifi_manager";
static EventGroupHandle_t s_wifi_event_group;
static int s_retry_num = 0;
static bool s_is_connected = false;
static bool s_is_ap_mode = false;

static void event_handler(void *arg, esp_event_base_t event_base,
                          int32_t event_id, void *event_data) {
  if (event_base == WIFI_EVENT && event_id == WIFI_EVENT_STA_START) {
    esp_wifi_connect();
  } else if (event_base == WIFI_EVENT &&
             event_id == WIFI_EVENT_STA_DISCONNECTED) {
    if (s_retry_num < WIFI_MAXIMUM_RETRY) {
      esp_wifi_connect();
      s_retry_num++;
      ESP_LOGI(TAG, "Retry to connect to the AP (%d/%d)", s_retry_num,
               WIFI_MAXIMUM_RETRY);
    } else {
      xEventGroupSetBits(s_wifi_event_group, WIFI_FAIL_BIT);
    }
    s_is_connected = false;
    ESP_LOGI(TAG, "Connect to the AP failed");
  } else if (event_base == IP_EVENT && event_id == IP_EVENT_STA_GOT_IP) {
    ip_event_got_ip_t *event = (ip_event_got_ip_t *)event_data;
    ESP_LOGI(TAG, "Got IP: " IPSTR, IP2STR(&event->ip_info.ip));
    s_retry_num = 0;
    s_is_connected = true;
    xEventGroupSetBits(s_wifi_event_group, WIFI_CONNECTED_BIT);
  } else if (event_base == WIFI_EVENT &&
             event_id == WIFI_EVENT_AP_STACONNECTED) {
    wifi_event_ap_staconnected_t *event =
        (wifi_event_ap_staconnected_t *)event_data;
    ESP_LOGI(TAG, "Station " MACSTR " connected, AID=%d", MAC2STR(event->mac),
             event->aid);
  } else if (event_base == WIFI_EVENT &&
             event_id == WIFI_EVENT_AP_STADISCONNECTED) {
    wifi_event_ap_stadisconnected_t *event =
        (wifi_event_ap_stadisconnected_t *)event_data;
    ESP_LOGI(TAG, "Station " MACSTR " disconnected, AID=%d",
             MAC2STR(event->mac), event->aid);
  }
}

esp_err_t wifi_manager_init_ap(const char *ssid, const char *password,
                               uint8_t channel, uint8_t max_conn) {
  // Initialize network stack
  ESP_ERROR_CHECK(esp_netif_init());
  ESP_ERROR_CHECK(esp_event_loop_create_default());
  
  // Create default AP network interface
  esp_netif_t *ap_netif = esp_netif_create_default_wifi_ap();
  if (!ap_netif) {
    ESP_LOGE(TAG, "Failed to create AP network interface");
    return ESP_FAIL;
  }

  wifi_init_config_t cfg = WIFI_INIT_CONFIG_DEFAULT();
  ESP_ERROR_CHECK(esp_wifi_init(&cfg));

  // Register AP event handlers
  esp_event_handler_instance_t instance_any_id;
  ESP_ERROR_CHECK(esp_event_handler_instance_register(
      WIFI_EVENT, ESP_EVENT_ANY_ID, &event_handler, NULL, &instance_any_id));

  wifi_config_t wifi_config = {
      .ap =
          {
              .ssid = "",
              .ssid_len = strlen(ssid),
              .channel = channel,
              .password = "",
              .max_connection = max_conn,
              .authmode = WIFI_AUTH_WPA2_PSK,
              .pmf_cfg =
                  {
                      .required = false,
                  },
          },
  };

  strncpy((char *)wifi_config.ap.ssid, ssid, sizeof(wifi_config.ap.ssid));
  if (password && strlen(password) >= 8) {
    strncpy((char *)wifi_config.ap.password, password,
            sizeof(wifi_config.ap.password));
  } else {
    wifi_config.ap.authmode = WIFI_AUTH_OPEN;
  }

  ESP_ERROR_CHECK(esp_wifi_set_mode(WIFI_MODE_AP));
  ESP_ERROR_CHECK(esp_wifi_set_config(WIFI_IF_AP, &wifi_config));
  ESP_ERROR_CHECK(esp_wifi_start());

  // Configure AP IP settings (optional but recommended)
  esp_netif_ip_info_t ip_info;
  IP4_ADDR(&ip_info.ip, 192, 168, 4, 1);
  IP4_ADDR(&ip_info.gw, 192, 168, 4, 1);
  IP4_ADDR(&ip_info.netmask, 255, 255, 255, 0);
  esp_netif_dhcps_stop(ap_netif);
  esp_netif_set_ip_info(ap_netif, &ip_info);
  esp_netif_dhcps_start(ap_netif);

  // Устанавливаем флаг AP режима
  s_is_ap_mode = true;
  s_is_connected = false; // Сбрасываем флаг STA подключения

  ESP_LOGI(TAG, "AP started: SSID: %s, Channel: %d, Max connections: %d", 
           ssid, channel, max_conn);
  return ESP_OK;
}

esp_err_t wifi_manager_init_sta(const char *ssid, const char *password) {
  if (!ssid || !password) {
    ESP_LOGE(TAG, "SSID or password is NULL");
    return ESP_ERR_INVALID_ARG;
  }

  s_wifi_event_group = xEventGroupCreate();
  if (s_wifi_event_group == NULL) {
    ESP_LOGE(TAG, "Failed to create event group");
    return ESP_ERR_NO_MEM;
  }

  ESP_ERROR_CHECK(esp_netif_init());
  ESP_ERROR_CHECK(esp_event_loop_create_default());
  esp_netif_create_default_wifi_sta();

  wifi_init_config_t cfg = WIFI_INIT_CONFIG_DEFAULT();
  ESP_ERROR_CHECK(esp_wifi_init(&cfg));

  esp_event_handler_instance_t instance_any_id;
  esp_event_handler_instance_t instance_got_ip;
  ESP_ERROR_CHECK(esp_event_handler_instance_register(
      WIFI_EVENT, ESP_EVENT_ANY_ID, &event_handler, NULL, &instance_any_id));
  ESP_ERROR_CHECK(esp_event_handler_instance_register(
      IP_EVENT, IP_EVENT_STA_GOT_IP, &event_handler, NULL, &instance_got_ip));

  wifi_config_t wifi_config = {0};
  strncpy((char *)wifi_config.sta.ssid, ssid, sizeof(wifi_config.sta.ssid) - 1);
  strncpy((char *)wifi_config.sta.password, password,
          sizeof(wifi_config.sta.password) - 1);
  wifi_config.sta.threshold.authmode = WIFI_AUTH_WPA2_PSK;

  // Сбрасываем флаг AP режима при инициализации STA
  s_is_ap_mode = false;

  ESP_ERROR_CHECK(esp_wifi_set_mode(WIFI_MODE_STA));
  ESP_ERROR_CHECK(esp_wifi_set_config(WIFI_IF_STA, &wifi_config));
  ESP_ERROR_CHECK(esp_wifi_start());

  ESP_LOGI(TAG, "WiFi station initialized. Connecting to %s...", ssid);

  // Ждем подключения с таймаутом 15 секунд
  EventBits_t bits = xEventGroupWaitBits(s_wifi_event_group,
                                         WIFI_CONNECTED_BIT | WIFI_FAIL_BIT,
                                         pdFALSE, pdFALSE, pdMS_TO_TICKS(15000));

  if (bits & WIFI_CONNECTED_BIT) {
    ESP_LOGI(TAG, "Connected to WiFi network: %s", ssid);
    return ESP_OK;
  } else if (bits & WIFI_FAIL_BIT) {
    ESP_LOGE(TAG, "Failed to connect to WiFi network: %s", ssid);
    return ESP_FAIL;
  } else {
    ESP_LOGW(TAG, "WiFi connection timeout for network: %s", ssid);
    return ESP_FAIL;
  }
}

bool wifi_manager_is_connected(void) { 
  // В AP режиме считаем устройство "подключенным" для целей веб-сервера
  return s_is_connected || s_is_ap_mode; 
}

bool wifi_manager_is_ap_mode(void) {
  return s_is_ap_mode;
}

void wifi_manager_deinit(void) {
  if (s_wifi_event_group) {
    vEventGroupDelete(s_wifi_event_group);
    s_wifi_event_group = NULL;
  }
  esp_wifi_stop();
  esp_wifi_deinit();
  esp_event_loop_delete_default();
  esp_netif_deinit();
  s_is_connected = false;
  s_is_ap_mode = false;
}
