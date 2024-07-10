//cod afisare bme680 online in mod STA cu conectare la wifi-ul de acasa, cu timp si refresh odata la 10s al paginii

//C
#include <stdio.h>
#include <stdbool.h>
#include <unistd.h>
#include <string.h>

//functionare hardware
#include <freertos/FreeRTOS.h>
#include <freertos/task.h>
#include "driver/gpio.h"
#include <nvs_flash.h>
#include <esp_log.h>
#include <esp_system.h>
#include <i2cdev.h>

//wifi
#include "esp_wifi.h"
#include "esp_event.h"
#include "esp_http_server.h"
#include "esp_mac.h"

//senzor
#include "bme680.h"

//timp
#include <time.h>
#include <sys/time.h>
#include "lwip/apps/sntp.h"

#define TAG "MAIN" 


#define I2C_PORT I2C_NUM_0
#define SDA_GPIO GPIO_NUM_5
#define SCL_GPIO GPIO_NUM_6

#ifndef APP_CPU_NUM
#define APP_CPU_NUM PRO_CPU_NUM
#endif

#define PORT 0
#if defined(BME680_I2C_ADDR_0)
#define ADDR BME680_I2C_ADDR_0
#endif
#if defined(BME680_I2C_ADDR_1)
#define ADDR BME680_I2C_ADDR_1
#endif

#define WIFI_SSID "your_ssid"
#define WIFI_PASS "****"

typedef struct {
    float temperature;
    float humidity;
    float pressure;
    float gas_resistance;
} sensor_data_t;

sensor_data_t sensor_data;



const char *HTML_TEMPLATE = "<html><head><meta http-equiv=\"refresh\" content=\"10\"></head><body><h1>BME680 Sensor Data <br> %d/%d/%d HOUR: %d MIN: %d SEC: %d </h1>"
                            "<p>Temperature: %.2f C</p>"
                            "<p>Humidity: %.2f %%</p>"
                            "<p>Pressure: %.2f hPa</p>"
                            "<p>Gas Resistance: %.2f Ohm</p>"
                            "</body></html>";


void initialize_sntp() {
    sntp_setoperatingmode(SNTP_OPMODE_POLL);
    sntp_setservername(0, "time.google.com");
    sntp_init();
}

void obtain_time() {
    time_t now = 0;
    struct tm timeinfo = { 0 };
    int retry = 0;
    const int retry_count = 10;

    while (++retry < retry_count) {
        ESP_LOGI(TAG, "Waiting for system time to be set... (%d/%d)", retry, retry_count);
        vTaskDelay(2000 / portTICK_PERIOD_MS);
        time(&now);
        localtime_r(&now, &timeinfo);
    }
}

void set_timezone() {
    setenv("TZ", "EET-2EEST,M3.5.0/3,M10.5.0/4", 1);
    tzset();
}



void bme680_test(void *pvParameters)
{
    bme680_t sensor;
    memset(&sensor, 0, sizeof(bme680_t));

    ESP_ERROR_CHECK(bme680_init_desc(&sensor, ADDR, PORT, SDA_GPIO, SCL_GPIO));
    ESP_ERROR_CHECK(bme680_init_sensor(&sensor));
    bme680_set_oversampling_rates(&sensor, BME680_OSR_4X, BME680_OSR_4X, BME680_OSR_4X);
    bme680_set_filter_size(&sensor, BME680_IIR_SIZE_7);
    bme680_set_heater_profile(&sensor, 0, 0, 0);
    bme680_use_heater_profile(&sensor, 0);
    bme680_set_ambient_temperature(&sensor, 10);

    uint32_t duration;
    bme680_get_measurement_duration(&sensor, &duration);

    TickType_t last_wakeup = xTaskGetTickCount();

    bme680_values_float_t values;
    while (1)
    {
        if (bme680_force_measurement(&sensor) == ESP_OK)
        {
            vTaskDelay(duration);
            if (bme680_get_results_float(&sensor, &values) == ESP_OK)
            {
                sensor_data.temperature = values.temperature;
                sensor_data.humidity = values.humidity;
                sensor_data.pressure = values.pressure;
                sensor_data.gas_resistance = values.gas_resistance;
                time_t now;
                time(&now);
                struct tm timeinfo;
                localtime_r(&now, &timeinfo);
                printf("%d/%d/%d HOUR: %d MIN: %d SEC: %d BME680 Sensor: %.2f C, %.2f %%, %.2f hPa, %.2f Ohm\n",
                       timeinfo.tm_mday, timeinfo.tm_mon + 1, timeinfo.tm_year + 1900, timeinfo.tm_hour, timeinfo.tm_min, timeinfo.tm_sec, values.temperature, values.humidity, values.pressure, values.gas_resistance );


            }
        }
        vTaskDelayUntil(&last_wakeup, pdMS_TO_TICKS(1000));
    }
}

esp_err_t sensor_get_handler(httpd_req_t *req)
{
    char resp_str[100];
    snprintf(resp_str, sizeof(resp_str), "Temperature: %.2f C, Humidity: %.2f %%", sensor_data.temperature, sensor_data.humidity);
    httpd_resp_send(req, resp_str, strlen(resp_str));
    return ESP_OK;
}

httpd_uri_t sensor_uri = {
    .uri      = "/sensor",
    .method   = HTTP_GET,
    .handler  = sensor_get_handler,
    .user_ctx = NULL
};

esp_err_t root_get_handler(httpd_req_t *req)
{
                time_t now;
                time(&now);
                struct tm timeinfo;
                localtime_r(&now, &timeinfo);
    char resp_str[500];
    snprintf(resp_str, sizeof(resp_str), HTML_TEMPLATE, 
             timeinfo.tm_mday, timeinfo.tm_mon + 1, timeinfo.tm_year + 1900, timeinfo.tm_hour, timeinfo.tm_min, timeinfo.tm_sec,
             sensor_data.temperature, sensor_data.humidity,
             sensor_data.pressure, sensor_data.gas_resistance);
    httpd_resp_send(req, resp_str, strlen(resp_str));
    return ESP_OK;
}

httpd_uri_t root_uri = {
    .uri      = "/",
    .method   = HTTP_GET,
    .handler  = root_get_handler,
    .user_ctx = NULL
};

void start_webserver(void)
{
    httpd_config_t config = HTTPD_DEFAULT_CONFIG();
    httpd_handle_t server = NULL;

    if (httpd_start(&server, &config) == ESP_OK) {
        httpd_register_uri_handler(server, &sensor_uri);
        httpd_register_uri_handler(server, &root_uri);
    }
}

static void wifi_event_handler(void* arg, esp_event_base_t event_base,
                               int32_t event_id, void* event_data)
{
    if (event_base == WIFI_EVENT && event_id == WIFI_EVENT_STA_START) {
        esp_wifi_connect();
    } else if (event_base == WIFI_EVENT && event_id == WIFI_EVENT_STA_DISCONNECTED) {
        esp_wifi_connect();
        ESP_LOGI("wifi", "Retry to connect to the AP");
    } else if (event_base == IP_EVENT && event_id == IP_EVENT_STA_GOT_IP) {
        ip_event_got_ip_t* event = (ip_event_got_ip_t*) event_data;
        ESP_LOGI("wifi", "got ip:" IPSTR, IP2STR(&event->ip_info.ip));
    }
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
}

void app_main(void)
{
    esp_log_level_set("bme680", ESP_LOG_ERROR);
    ESP_ERROR_CHECK(i2cdev_init());
    xTaskCreatePinnedToCore(bme680_test, "bme680_test", configMINIMAL_STACK_SIZE * 8, NULL, 5, NULL, APP_CPU_NUM);

    ESP_ERROR_CHECK(nvs_flash_init());
    wifi_init_sta();
    start_webserver();
    initialize_sntp();
    obtain_time();
    set_timezone();

}
