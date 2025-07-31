//v0 cod afisare bme680 online in mod STA cu conectare la wifi-ul de acasa, cu timp si refresh odata la 10s al paginii
//v0.1 adaugat functie de log cu scriere in SPIFFS
//v0.2 scriere log in ordine de la cel mai nou, transmitere pagina web in chunk-uri, pagina web independenta in spiffs stilizata, cu functii de confirmare a actiunilor, functie de reinitializare
//v0.3 adaugat functii pentru led-ul incorporat (rosu - conectare la wifi, verde - conectat la wifi, albastru - mod AP (Access Point)), 
//     adaugat mod AccessPoint daca statia wifi nu este gasita in timpul configurat, adaugat hostname (esp32.local) prin mDNS pentru a evita utilizarea adresei IP
//v0.3.1 reparat bug-uri softAP (resetare la 10 secunde sistem si neinitializare softAP la deconectarea de la statie)
//v0.3.2 reorganizare cod
//v0.4 migrare transmitere date prin websockets

//C
#include <stdio.h>
#include <stdbool.h>
#include <unistd.h>
#include <string.h>

//functionalitati hardware-soft ware si drivere
#include <freertos/FreeRTOS.h>
#include <freertos/task.h>
#include "driver/gpio.h"
#include <nvs_flash.h>
#include <esp_log.h>
#include <esp_system.h>
#include "esp_spiffs.h"
#include <i2cdev.h>
#include "esp_heap_caps.h"
#include "led_builtin.h"
#include "led_strip.h"

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

//webserver
#include "web_server.h"

//filemanagement
#include "file_manager.h"


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
 


void bme680_test(void *pvParameters)
{
    bme680_t sensor;
    memset(&sensor, 0, sizeof(bme680_t));

    ESP_ERROR_CHECK(bme680_init_desc(&sensor, ADDR, PORT, SDA_GPIO, SCL_GPIO));
    ESP_ERROR_CHECK(bme680_init_sensor(&sensor));
    bme680_set_oversampling_rates(&sensor, BME680_OSR_1X, BME680_OSR_1X, BME680_OSR_1X);
    bme680_set_filter_size(&sensor, BME680_IIR_SIZE_3);
//  bme680_set_heater_profile(&sensor, 0, 0, 0);
    bme680_use_heater_profile(&sensor, -1);
//  bme680_set_ambient_temperature(&sensor, 25);

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

                if (current_tick - last_data_write >= pdMS_TO_TICKS(300002))
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

                if (current_tick - last_console_report >= pdMS_TO_TICKS(1003))
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
        vTaskDelay(pdMS_TO_TICKS(15));
    }
}

void app_main(void)
{
    
    esp_log_level_set("bme680", ESP_LOG_ERROR);
    ESP_ERROR_CHECK(i2cdev_init());
    ESP_ERROR_CHECK(nvs_flash_init());


    spiffs_init();
    wifi_init_sta();
    initialize_mdns();
    start_webserver();
    initialize_sntp();
    set_timezone();
    obtain_time();
    xTaskCreate(bme680_test, "bme680_test", configMINIMAL_STACK_SIZE * 8, NULL, 5, NULL);
}
