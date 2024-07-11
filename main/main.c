//v0 cod afisare bme680 online in mod STA cu conectare la wifi-ul de acasa, cu timp si refresh odata la 10s al paginii
//v0.1 adaugat functie de log cu scriere in SPIFFS

//C
#include <stdio.h>
#include <stdbool.h>
#include <unistd.h>
#include <string.h>

//functionalitati hardware-software si drivere
#include <freertos/FreeRTOS.h>
#include <freertos/task.h>
#include "driver/gpio.h"
#include <nvs_flash.h>
#include <esp_log.h>
#include <esp_system.h>
#include "esp_spiffs.h"
#include <i2cdev.h>

//wifi
#include "esp_wifi.h"
#include "esp_event.h"
#include "esp_http_server.h"
#include "esp_mac.h"
#include "wifi.h"

//senzor
#include "bme680.h"

//timp
#include <time.h>
#include <sys/time.h>
#include "lwip/apps/sntp.h"
#include "timesetup.h"

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
 
typedef struct {
    float temperature;
    float humidity;
    float pressure;
    float gas_resistance;
} sensor_data_t;

sensor_data_t sensor_data;

const char *HTML_TEMPLATE = "<html><head><meta http-equiv=\"refresh\" content=\"10\"></head><body>"
                            "<h1>BME680 Sensor Data <br> %d/%.2d/%d  %.2d:%.2d:%.2d </h1>"
                            "<p>Temperature: %.2f C</p>"
                            "<p>Humidity: %.2f %%</p>"
                            "<p>Pressure: %.2f hPa</p>"
                            "<p>Gas Resistance: %.2f Ohm</p>"
                            "<a href=\"/download\" target=\"_blank\"><button type=\"button\">Show Log</button></a>"
                            "</body></html>";

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

esp_err_t download_get_handler(httpd_req_t *req)
{
    FILE* f = fopen("/spiffs/sensor_data.txt", "r");
    if (f == NULL) {
        httpd_resp_send_404(req);
        return ESP_FAIL;
    }

    char buffer[128];
    size_t bytes_read;
    httpd_resp_set_type(req, "text/plain");
    while ((bytes_read = fread(buffer, 1, sizeof(buffer), f)) > 0) {
        httpd_resp_send_chunk(req, buffer, bytes_read);
    }
    fclose(f);
    httpd_resp_send_chunk(req, NULL, 0);
    return ESP_OK;
}

httpd_uri_t download_uri = {
    .uri      = "/download",
    .method   = HTTP_GET,
    .handler  = download_get_handler,
    .user_ctx = NULL
};

void start_webserver(void)
{
    httpd_config_t config = HTTPD_DEFAULT_CONFIG();
    httpd_handle_t server = NULL;

    if (httpd_start(&server, &config) == ESP_OK) {
        httpd_register_uri_handler(server, &root_uri);
        httpd_register_uri_handler(server, &download_uri);
    }
}

void write_data_to_file(const char *data)
{
    FILE* f = fopen("/spiffs/sensor_data.txt", "a");
    if (f == NULL) {
        ESP_LOGE("SPIFFS", "Failed to open file for writing");
        return;
    }
    fprintf(f, "%s\n", data);
    fclose(f);
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
//  bme680_set_ambient_temperature(&sensor, 10);

    uint32_t duration;
    bme680_get_measurement_duration(&sensor, &duration);

    TickType_t last_data_write = xTaskGetTickCount();
    TickType_t last_console_report = xTaskGetTickCount();

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

                TickType_t current_tick = xTaskGetTickCount();

                if (current_tick - last_data_write >= pdMS_TO_TICKS(120000))
                {
                    time_t now;
                    time(&now);
                    struct tm timeinfo;
                    localtime_r(&now, &timeinfo);
                    char data_str[256];
                    snprintf(data_str, sizeof(data_str), "%.2d/%.2d/%d at: %.2d:%.2d:%.2d BME680 Sensor: %.2f C, %.2f %%, %.2f hPa, %.2f Ohm",
                           timeinfo.tm_mday, timeinfo.tm_mon + 1, timeinfo.tm_year + 1900, timeinfo.tm_hour, timeinfo.tm_min, timeinfo.tm_sec, values.temperature, values.humidity, values.pressure, values.gas_resistance);
                    write_data_to_file(data_str);
                    last_data_write = current_tick;
                }

                if (current_tick - last_console_report >= pdMS_TO_TICKS(1000))
                {
                    time_t now;
                    time(&now);
                    struct tm timeinfo;
                    localtime_r(&now, &timeinfo);
                    char data_str[256];
                    snprintf(data_str, sizeof(data_str), "%.2d/%.2d/%d at: %.2d:%.2d:%.2d BME680 Sensor: %.2f C, %.2f %%, %.2f hPa, %.2f Ohm",
                           timeinfo.tm_mday, timeinfo.tm_mon + 1, timeinfo.tm_year + 1900, timeinfo.tm_hour, timeinfo.tm_min, timeinfo.tm_sec, values.temperature, values.humidity, values.pressure, values.gas_resistance);
                    printf("%s\n", data_str);
                    last_console_report = current_tick;
                }
            }
        }
        vTaskDelay(pdMS_TO_TICKS(1000));
    }
}

void spiffs_init(void){
  esp_vfs_spiffs_conf_t conf = {
        .base_path = "/spiffs",
        .partition_label = NULL,
        .max_files = 5,
        .format_if_mount_failed = true
    };

    esp_err_t ret = esp_vfs_spiffs_register(&conf);

    if (ret != ESP_OK) {
        if (ret == ESP_FAIL) {
            ESP_LOGE("SPIFFS", "Failed to mount or format filesystem");
        } else if (ret == ESP_ERR_NOT_FOUND) {
            ESP_LOGE("SPIFFS", "Failed to find SPIFFS partition");
        } else {
            ESP_LOGE("SPIFFS", "Failed to initialize SPIFFS (%s)", esp_err_to_name(ret));
        }
        return;
    }

    size_t total = 0, used = 0;
    ret = esp_spiffs_info(NULL, &total, &used);
    if (ret != ESP_OK) {
        ESP_LOGE("SPIFFS", "Failed to get SPIFFS partition information (%s)", esp_err_to_name(ret));
    } else {
        ESP_LOGI("SPIFFS", "Partition size: total: %d, used: %d", total, used);
    }

}

void app_main(void)
{
    
    esp_log_level_set("bme680", ESP_LOG_ERROR);
    ESP_ERROR_CHECK(i2cdev_init());
    ESP_ERROR_CHECK(nvs_flash_init());

    spiffs_init();
    wifi_init_sta();
    initialize_sntp();
    set_timezone();
    obtain_time();
    start_webserver();
    xTaskCreatePinnedToCore(bme680_test, "bme680_test", configMINIMAL_STACK_SIZE * 8, NULL, 5, NULL, APP_CPU_NUM);

}
