// wifi
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
#include "nvs_flash.h"
#include "nvs.h"


#define CONNECT_TIMEOUT_MS 10000
#define MAX_RETRY 10

static int retry_count = 0;
static const char *TAG = "wifi softAP";
static TaskHandle_t wifi_timeout_task_handle = NULL;
static bool wifi_connected = false;
static bool softap_initialized = false;

static void wifi_event_handler(void *arg, esp_event_base_t event_base,
                               int32_t event_id, void *event_data)
{
    if (event_base == WIFI_EVENT && event_id == WIFI_EVENT_STA_START)
    {
        esp_wifi_connect();
        if (led_blink_task_handle == NULL)
        {
            xTaskCreate(led_blink_red, "led_blink_task", 2048, NULL, 5, &led_blink_task_handle);
        }
        wifi_connected = false;
    }
    else if (event_base == WIFI_EVENT && event_id == WIFI_EVENT_STA_DISCONNECTED)
    {
        if (retry_count < MAX_RETRY)
        {
            esp_wifi_connect();
            ESP_LOGI(TAG, "Retrying to connect to the AP, attempt %d", ++retry_count);
        }
        else if (!softap_initialized)
        {
            ESP_LOGI(TAG, "Max retries reached, switching to SoftAP mode");
            vTaskDelete(led_blink_task_handle);
            wifi_init_softap();
            softap_initialized = true;
        }
        if (led_blink_task_handle == NULL)
        {
            xTaskCreate(led_blink_red, "led_blink_task", 2048, NULL, 5, &led_blink_task_handle);
        }
        wifi_connected = false;
    }
    else if (event_base == IP_EVENT && event_id == IP_EVENT_STA_GOT_IP)
    {
        ip_event_got_ip_t *event = (ip_event_got_ip_t *)event_data;
        ESP_LOGI("wifi", "got ip:" IPSTR, IP2STR(&event->ip_info.ip));

        if (led_blink_task_handle != NULL)
        {
            vTaskDelete(led_blink_task_handle);
            led_blink_task_handle = NULL;
        }

        led_builtin_color(green, 64);
        vTaskDelay(pdMS_TO_TICKS(3000));
        led_builtin_color(green, 1);
        wifi_connected = true;
        retry_count = 0;
        if (wifi_timeout_task_handle != NULL)
        {
            vTaskDelete(wifi_timeout_task_handle);
            wifi_timeout_task_handle = NULL;
        }
    }
}

static void wifi_timeout_task(void *pvParameter)
{
    vTaskDelay(pdMS_TO_TICKS(CONNECT_TIMEOUT_MS));

    if (!wifi_connected)
    {
        ESP_LOGI("wifi", "Connection timeout, switching to SoftAP mode");
        if (led_blink_task_handle != NULL) {
            vTaskDelete(led_blink_task_handle);
            led_blink_task_handle = NULL;
        }
        wifi_init_softap();
    }

    wifi_timeout_task_handle = NULL;
    vTaskDelete(NULL);   // task deletes itself, not via handle
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

    
    wifi_config_t wifi_config = {0};
    nvs_handle_t nvs;
    esp_err_t err = nvs_open("wifi_creds", NVS_READONLY, &nvs);
    if (err == ESP_OK) {
        size_t ssid_len = sizeof(wifi_config.sta.ssid);
        size_t pass_len = sizeof(wifi_config.sta.password);
        if (nvs_get_str(nvs, "ssid", (char *)wifi_config.sta.ssid, &ssid_len) != ESP_OK ||
            nvs_get_str(nvs, "pass", (char *)wifi_config.sta.password, &pass_len) != ESP_OK) {
            ESP_LOGW("WiFi", "Failed to get SSID or password, using defaults");
            strcpy((char *)wifi_config.sta.ssid, DEFAULT_WIFI_SSID);
            strcpy((char *)wifi_config.sta.password, DEFAULT_WIFI_PASS);
        }
        nvs_close(nvs);
    } else {
        ESP_LOGW("WiFi", "No stored credentials, using defaults");
        strcpy((char *)wifi_config.sta.ssid, DEFAULT_WIFI_SSID);
        strcpy((char *)wifi_config.sta.password, DEFAULT_WIFI_PASS);
    }

    wifi_config.sta.threshold.authmode = WIFI_AUTH_WPA2_PSK;

    ESP_ERROR_CHECK(esp_wifi_set_mode(WIFI_MODE_STA));
    ESP_ERROR_CHECK(esp_wifi_set_config(ESP_IF_WIFI_STA, &wifi_config));
    ESP_ERROR_CHECK(esp_wifi_start());

    xTaskCreate(wifi_timeout_task, "wifi_timeout_task", 4096, NULL, 5, &wifi_timeout_task_handle);
}


void wifi_init_softap(void)
{
    char ssid[32] = {0};
    char password[64] = {0};
    

    esp_netif_create_default_wifi_ap();

    wifi_init_config_t cfg = WIFI_INIT_CONFIG_DEFAULT();
    ESP_ERROR_CHECK(esp_wifi_init(&cfg));

    ESP_ERROR_CHECK(esp_event_handler_instance_register(WIFI_EVENT,
                                                        ESP_EVENT_ANY_ID,
                                                        &wifi_event_handler,
                                                        NULL,
                                                        NULL));

    int authmode = DEFAULT_SOFTAP_AUTHMODE;
    strcpy(ssid, DEFAULT_SOFTAP_SSID);
    strcpy(password, DEFAULT_SOFTAP_PASS);

    // Try reading from NVS and override if present
    nvs_handle_t nvs_handle;
    esp_err_t err = nvs_open("softap_creds", NVS_READONLY, &nvs_handle);
    if (err == ESP_OK)
    {
        size_t ssid_len = sizeof(ssid);
        size_t pass_len = sizeof(password);
        if (nvs_get_str(nvs_handle, "ssid", ssid, &ssid_len) != ESP_OK)
        {
            ESP_LOGW(TAG, "Using default SoftAP SSID");
            strcpy(ssid, DEFAULT_SOFTAP_SSID);
        }
        if (nvs_get_str(nvs_handle, "pass", password, &pass_len) != ESP_OK)
        {
            ESP_LOGW(TAG, "Using default SoftAP password");
            strcpy(password, DEFAULT_SOFTAP_PASS);
        }
        if (nvs_get_i32(nvs_handle, "authmode", &authmode) != ESP_OK)
        {
            ESP_LOGW(TAG, "Using default SoftAP authmode");
            authmode = DEFAULT_SOFTAP_AUTHMODE;
        }
        nvs_close(nvs_handle);
    }

    wifi_config_t wifi_config = {
        .ap = {
            .ssid_len = strlen(ssid),
            .max_connection = 4,
            .pmf_cfg = {
                .required = true,
            },
        },
    };

    strncpy((char *)wifi_config.ap.ssid, ssid, sizeof(wifi_config.ap.ssid) - 1);
    wifi_config.ap.ssid[sizeof(wifi_config.ap.ssid) - 1] = '\0';  // Ensure null-terminated
    wifi_config.ap.ssid_len = strlen((char *)wifi_config.ap.ssid);


    // Handle open auth or too short passwords
    if (strlen(password) == 0 || authmode == WIFI_AUTH_OPEN || strlen(password) < 8) {
        ESP_LOGW(TAG, "Password too short (%d), using open auth", strlen(password));
        wifi_config.ap.authmode = WIFI_AUTH_OPEN;
        memset(wifi_config.ap.password, 0, sizeof(wifi_config.ap.password));  // Clear password
    } else {
        wifi_config.ap.authmode = authmode;
        strncpy((char *)wifi_config.ap.password, password, sizeof(wifi_config.ap.password));
    }

    strncpy((char *)wifi_config.ap.ssid, ssid, sizeof(wifi_config.ap.ssid));


#ifdef CONFIG_ESP_WIFI_SOFTAP_SAE_SUPPORT
    if (wifi_config.ap.authmode == WIFI_AUTH_WPA3_PSK || wifi_config.ap.authmode == WIFI_AUTH_WPA2_WPA3_PSK) {
        wifi_config.ap.sae_pwe_h2e = WPA3_SAE_PWE_BOTH;
    }
#endif

    ESP_ERROR_CHECK(esp_wifi_set_mode(WIFI_MODE_AP));
    ESP_ERROR_CHECK(esp_wifi_set_config(WIFI_IF_AP, &wifi_config));
    ESP_ERROR_CHECK(esp_wifi_start());

    ESP_LOGI(TAG, "SoftAP started. SSID:%s Password:%s Authmode:%d",
             ssid, password, authmode);

    led_builtin_color(blue, 128);
    vTaskDelay(pdMS_TO_TICKS(3000));
    led_builtin_color(blue, 1);
}

bool wifi_is_connected(void)
{
    return wifi_connected;
}

bool wifi_is_softap_mode(void)
{
    return softap_initialized;
}


void initialize_mdns()
{
    ESP_ERROR_CHECK(mdns_init());
    ESP_ERROR_CHECK(mdns_hostname_set("esp32"));
    ESP_ERROR_CHECK(mdns_instance_name_set("ESP32 Web Server"));
    ESP_ERROR_CHECK(mdns_service_add("namedns", "_http", "_tcp", 80, NULL, 0));
}
