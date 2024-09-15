//wifi
#include "esp_wifi.h"
#include "esp_event.h"
#include "esp_http_server.h"
#include "esp_mac.h"
#include "wifi.h"
#include <freertos/FreeRTOS.h>
#include <freertos/task.h>
#include "driver/gpio.h"
#include <nvs_flash.h>
#include <esp_log.h>
#include <esp_system.h>
#include <i2cdev.h>
#include "led_builtin.h"
#include "mdns.h"


#define CONNECT_TIMEOUT_MS 10000

static const char *TAG = "wifi softAP";
static TaskHandle_t wifi_timeout_task_handle = NULL;
static bool wifi_connected = false;

static void wifi_event_handler(void *arg, esp_event_base_t event_base,
                               int32_t event_id, void *event_data)
{
    if (event_base == WIFI_EVENT && event_id == WIFI_EVENT_STA_START)
    {   
        esp_wifi_connect();
        if (led_blink_task_handle == NULL) {
            xTaskCreate(led_blink_red, "led_blink_task", 2048, NULL, 5, &led_blink_task_handle);
        }
    }
    else if (event_base == WIFI_EVENT && event_id == WIFI_EVENT_STA_DISCONNECTED)
    {
        esp_wifi_connect();
        ESP_LOGI("wifi", "Retry to connect to the AP");
        if (led_blink_task_handle == NULL) {
        xTaskCreate(led_blink_red, "led_blink_task", 2048, NULL, 5, &led_blink_task_handle);
        }
    }
    else if (event_base == IP_EVENT && event_id == IP_EVENT_STA_GOT_IP)
    {
        ip_event_got_ip_t *event = (ip_event_got_ip_t *)event_data;
        ESP_LOGI("wifi", "got ip:" IPSTR, IP2STR(&event->ip_info.ip));

        if (led_blink_task_handle != NULL) {
            vTaskDelete(led_blink_task_handle);
            led_blink_task_handle = NULL;
        }

        led_builtin_color(green, 128);
        vTaskDelay(pdMS_TO_TICKS(3000));
        led_builtin_color(green, 10);
        wifi_connected = true;
        if (wifi_timeout_task_handle != NULL) {
            vTaskDelete(wifi_timeout_task_handle);
            wifi_timeout_task_handle = NULL;
        }
    }
}

static void wifi_timeout_task(void *pvParameter)
{
    vTaskDelay(pdMS_TO_TICKS(CONNECT_TIMEOUT_MS));

    if (!wifi_connected) {
        ESP_LOGI("wifi", "Connection timeout, switching to SoftAP mode");
        vTaskDelete(led_blink_task_handle);
        wifi_init_softap();
    }

    vTaskDelete(NULL);
}

void wifi_init_sta()
{
    ESP_ERROR_CHECK(esp_netif_init());
    ESP_ERROR_CHECK(esp_event_loop_create_default());
    esp_netif_create_default_wifi_sta();

    wifi_init_config_t cfg = WIFI_INIT_CONFIG_DEFAULT();
    ESP_ERROR_CHECK(esp_wifi_init(&cfg));

    esp_event_handler_instance_t instance_any_id;
    esp_event_handler_instance_t instance_got_ip;

    ESP_ERROR_CHECK(esp_event_handler_instance_register(WIFI_EVENT,
                                                        ESP_EVENT_ANY_ID,
                                                        &wifi_event_handler,
                                                        NULL,
                                                        &instance_any_id));
    ESP_ERROR_CHECK(esp_event_handler_instance_register(IP_EVENT,
                                                        IP_EVENT_STA_GOT_IP,
                                                        &wifi_event_handler,
                                                        NULL,
                                                        &instance_got_ip));

    wifi_config_t wifi_config = {
        .sta = {
            .ssid = WIFI_SSID,
            .password = WIFI_PASS,
            .threshold.authmode = WIFI_AUTH_WPA2_PSK,
        },
    };

    ESP_ERROR_CHECK(esp_wifi_set_mode(WIFI_MODE_STA));
    ESP_ERROR_CHECK(esp_wifi_set_config(ESP_IF_WIFI_STA, &wifi_config));
    ESP_ERROR_CHECK(esp_wifi_start());
    xTaskCreate(wifi_timeout_task, "wifi_timeout_task", 4096, NULL, 5, &wifi_timeout_task_handle);
}


void wifi_init_softap(void)
{
    // ESP_ERROR_CHECK(esp_netif_init());
    // ESP_ERROR_CHECK(esp_event_loop_create_default());
    esp_netif_create_default_wifi_ap();

    wifi_init_config_t cfg = WIFI_INIT_CONFIG_DEFAULT();
    ESP_ERROR_CHECK(esp_wifi_init(&cfg));

    ESP_ERROR_CHECK(esp_event_handler_instance_register(WIFI_EVENT,
                                                        ESP_EVENT_ANY_ID,
                                                        &wifi_event_handler,
                                                        NULL,
                                                        NULL));

    wifi_config_t wifi_config = {
        .ap = {
            .ssid = AP_SSID,
            .ssid_len = strlen(AP_SSID),
            .password = AP_PASS,
            .max_connection = 4,
#ifdef CONFIG_ESP_WIFI_SOFTAP_SAE_SUPPORT
            .authmode = WIFI_AUTH_WPA3_PSK,
            .sae_pwe_h2e = WPA3_SAE_PWE_BOTH,
#else /* CONFIG_ESP_WIFI_SOFTAP_SAE_SUPPORT */
            .authmode = WIFI_AUTH_WPA2_PSK,
#endif
            .pmf_cfg = {
                    .required = true,
            },
        },
    };
    if (strlen(AP_PASS) == 0) {
        wifi_config.ap.authmode = WIFI_AUTH_OPEN;
    }

    ESP_ERROR_CHECK(esp_wifi_set_mode(WIFI_MODE_AP));
    ESP_ERROR_CHECK(esp_wifi_set_config(WIFI_IF_AP, &wifi_config));
    ESP_ERROR_CHECK(esp_wifi_start());

    ESP_LOGI(TAG, "wifi_init_softap finished. SSID:%s password:%s channel:%d",
             wifi_config.ap.ssid, wifi_config.ap.password, wifi_config.ap.channel);

    led_builtin_color (blue, 128);
    vTaskDelay(pdMS_TO_TICKS(3000));
    led_builtin_color (blue, 10);  
}

void initialize_mdns() {
    ESP_ERROR_CHECK(mdns_init());
    ESP_ERROR_CHECK(mdns_hostname_set("esp32"));
    ESP_ERROR_CHECK(mdns_instance_name_set("ESP32 Web Server"));
    ESP_ERROR_CHECK(mdns_service_add("namedns", "_http", "_tcp", 80, NULL, 0));
}

