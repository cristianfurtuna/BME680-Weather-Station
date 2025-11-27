#include "mqtt.h"

#include <stdio.h>
#include <string.h>
#include <time.h>
#include <sys/time.h>

#include "esp_log.h"
#include "mqtt_client.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "nvs_flash.h"
#include "nvs.h"

static const char *TAG = "mqtt";

static esp_mqtt_client_handle_t mqtt_client = NULL;
static TaskHandle_t mqtt_pub_task_handle = NULL;
static bool mqtt_connected = false;
static char broker_uri[64] = {0}; // holds final "mqtt://ip:port" URI

static void mqtt_load_broker_from_nvs(void)
{
    // Start with the default
    strncpy(broker_uri, MQTT_DEFAULT_BROKER_URI, sizeof(broker_uri) - 1);
    broker_uri[sizeof(broker_uri) - 1] = '\0';

    nvs_handle_t nvs;
    esp_err_t err = nvs_open("mqtt_cfg", NVS_READONLY, &nvs);
    if (err == ESP_OK)
    {
        char ip[32];
        size_t ip_len = sizeof(ip);
        uint16_t port = 0;

        if (nvs_get_str(nvs, "ip", ip, &ip_len) == ESP_OK &&
            nvs_get_u16(nvs, "port", &port) == ESP_OK &&
            ip_len > 1 && port > 0 && port <= 65535)
        {

            snprintf(broker_uri, sizeof(broker_uri),
                     "mqtt://%s:%u", ip, port);
            ESP_LOGI(TAG, "Loaded MQTT broker from NVS: %s", broker_uri);
        }
        else
        {
            ESP_LOGI(TAG, "No valid MQTT config in NVS, using default");
        }
        nvs_close(nvs);
    }
    else
    {
        ESP_LOGI(TAG, "No mqtt_cfg namespace in NVS, using default");
    }
}

// const char *mqtt_get_broker_uri(void)
// {
//     if (broker_uri[0] == '\0')
//     {
//         mqtt_load_broker_from_nvs();
//     }
//     return broker_uri;
// }

/* Forward declarations */
static void mqtt_event_handler(void *handler_args,
                               esp_event_base_t base,
                               int32_t event_id,
                               void *event_data);
static void mqtt_publisher_task(void *arg);

bool mqtt_is_connected(void)
{
    return mqtt_connected;
}

void mqtt_publish_sensor(void)
{
    if (!mqtt_client || !mqtt_connected)
    {
        return;
    }

    /* Build JSON similar to your WebSocket broadcast format */
    char payload[256];

    time_t now;
    struct tm timeinfo;
    time(&now);
    localtime_r(&now, &timeinfo);

    snprintf(payload, sizeof(payload),
             "{\"temperature\":%.2f,"
             "\"humidity\":%.2f,"
             "\"pressure\":%.2f,"
             "\"gas_resistance\":%.2f,"
             "\"timestamp\":\"%04d-%02d-%02d %02d:%02d:%02d\","
             "\"gas_warning_on\":%s}",
             sensor_data.temperature,
             sensor_data.humidity,
             sensor_data.pressure,
             sensor_data.gas_resistance,
             timeinfo.tm_year + 1900,
             timeinfo.tm_mon + 1,
             timeinfo.tm_mday,
             timeinfo.tm_hour,
             timeinfo.tm_min,
             timeinfo.tm_sec,
             sensor_data.gas_warning_on ? "true" : "false");

    int msg_id = esp_mqtt_client_publish(mqtt_client,
                                         MQTT_TOPIC_STATE,
                                         payload,
                                         0,  // use strlen(payload)
                                         1,  // QoS 1
                                         0); // retain = 0

    if (msg_id >= 0)
    {
        ESP_LOGD(TAG, "Published sensor data, msg_id=%d", msg_id);
    }
    else
    {
        ESP_LOGW(TAG, "Failed to publish sensor data");
    }
}

static void mqtt_publisher_task(void *arg)
{
    (void)arg;
    while (1)
    {
        if (mqtt_connected)
        {
            mqtt_publish_sensor();
        }
        vTaskDelay(pdMS_TO_TICKS(10000));
    }
}

/* MQTT event handler */
static void mqtt_event_handler(void *handler_args,
                               esp_event_base_t base,
                               int32_t event_id,
                               void *event_data)
{
    (void)handler_args;
    (void)base;

    esp_mqtt_event_handle_t event = event_data;
    esp_mqtt_client_handle_t client = event->client;

    switch ((esp_mqtt_event_id_t)event_id)
    {
    case MQTT_EVENT_CONNECTED:
        ESP_LOGI(TAG, "MQTT_EVENT_CONNECTED");
        mqtt_connected = true;

        /* Publish "online" status */
        esp_mqtt_client_publish(client,
                                MQTT_TOPIC_STATUS,
                                "online",
                                0,
                                1,
                                1); // retain = 1
        break;

    case MQTT_EVENT_DISCONNECTED:
        ESP_LOGW(TAG, "MQTT_EVENT_DISCONNECTED");
        mqtt_connected = false;
        break;

    case MQTT_EVENT_ERROR:
        ESP_LOGW(TAG, "MQTT_EVENT_ERROR");
        break;

    case MQTT_EVENT_SUBSCRIBED:
    case MQTT_EVENT_UNSUBSCRIBED:
    case MQTT_EVENT_PUBLISHED:
    case MQTT_EVENT_DATA:
    case MQTT_EVENT_BEFORE_CONNECT:
    default:
        break;
    }
}

void mqtt_start(void)
{
    if (mqtt_client != NULL)
    {
        ESP_LOGW(TAG, "mqtt_start() called but client already initialized");
        return;
    }

    mqtt_load_broker_from_nvs();
    ESP_LOGI(TAG, "Starting MQTT client, broker: %s", broker_uri);

    const esp_mqtt_client_config_t mqtt_cfg = {
        .broker = {
            .address.uri = broker_uri,
        },
        .credentials = {
            .client_id = MQTT_CLIENT_ID,
        },
    };

    mqtt_client = esp_mqtt_client_init(&mqtt_cfg);
    if (mqtt_client == NULL)
    {
        ESP_LOGE(TAG, "esp_mqtt_client_init failed");
        return;
    }

    esp_err_t err = esp_mqtt_client_register_event(
        mqtt_client,
        MQTT_EVENT_ANY,
        mqtt_event_handler,
        NULL);
    if (err != ESP_OK)
    {
        ESP_LOGE(TAG, "Failed to register MQTT event handler: %s", esp_err_to_name(err));
        esp_mqtt_client_destroy(mqtt_client);
        mqtt_client = NULL;
        return;
    }

    err = esp_mqtt_client_start(mqtt_client);
    if (err != ESP_OK)
    {
        ESP_LOGE(TAG, "Failed to start MQTT client: %s", esp_err_to_name(err));
        esp_mqtt_client_destroy(mqtt_client);
        mqtt_client = NULL;
        return;
    }

    /* Start background publisher task (unchanged) */
    if (mqtt_pub_task_handle == NULL)
    {
        BaseType_t res = xTaskCreate(mqtt_publisher_task,
                                     "mqtt_pub_task",
                                     4096,
                                     NULL,
                                     5,
                                     &mqtt_pub_task_handle);
        if (res != pdPASS)
        {
            ESP_LOGE(TAG, "Failed to create MQTT publisher task");
            mqtt_pub_task_handle = NULL;
        }
    }
}

// void mqtt_stop(void)
// {
//     if (mqtt_pub_task_handle != NULL)
//     {
//         vTaskDelete(mqtt_pub_task_handle);
//         mqtt_pub_task_handle = NULL;
//     }

//     if (mqtt_client != NULL)
//     {
//         esp_mqtt_client_stop(mqtt_client);
//         esp_mqtt_client_destroy(mqtt_client);
//         mqtt_client = NULL;
//     }

//     mqtt_connected = false;
//     ESP_LOGI(TAG, "MQTT client stopped");
// }
