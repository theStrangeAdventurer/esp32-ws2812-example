idf_component_register(SRCS "main.c" "led_strip_encoder.c" "led_effects.c" "effect_manager.c" "wifi_manager.c" "web_server.c" "spiffs_manager.c"
                       INCLUDE_DIRS "."
                       REQUIRES esp_http_server json esp_driver_rmt esp_driver_gpio esp_wifi esp_event nvs_flash freertos esp_netif spiffs)
