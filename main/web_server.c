#include "esp_http_server.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include "esp_log.h"
#include "web_server.h"
#include "freertos/semphr.h"
#include "freertos/task.h"
#include "timesetup.h"
#include <nvs_flash.h>
#include "nvs.h"
#include "cJSON.h"
#include "wifi.h"
#include "esp_wifi.h"

#define MAX_CLIENTS 8

static const char *TAG = "web_server";

static httpd_handle_t ws_server = NULL;
sensor_data_t sensor_data;

static SemaphoreHandle_t clients_mutex;
static int client_fds[MAX_CLIENTS] = {0};

esp_err_t handle_set_mqtt(httpd_req_t *req)
{
    char buf[128];
    int len = httpd_req_recv(req, buf, sizeof(buf) - 1);
    if (len <= 0) {
        httpd_resp_send_err(req, HTTPD_400_BAD_REQUEST, "Empty body");
        return ESP_OK;
    }
    buf[len] = 0;

    cJSON *json = cJSON_Parse(buf);
    if (!json) {
        httpd_resp_send_err(req, HTTPD_400_BAD_REQUEST, "Invalid JSON");
        return ESP_OK;
    }

    const cJSON *ip_json   = cJSON_GetObjectItem(json, "ip");
    const cJSON *port_json = cJSON_GetObjectItem(json, "port");

    if (!cJSON_IsString(ip_json) || !cJSON_IsNumber(port_json)) {
        cJSON_Delete(json);
        httpd_resp_send_err(req, HTTPD_400_BAD_REQUEST, "Missing ip or port");
        return ESP_OK;
    }

    const char *ip = ip_json->valuestring;
    int port = port_json->valueint;

    if (port <= 0 || port > 65535) {
        cJSON_Delete(json);
        httpd_resp_send_err(req, HTTPD_400_BAD_REQUEST, "Invalid port");
        return ESP_OK;
    }

    nvs_handle_t nvs;
    esp_err_t err = nvs_open("mqtt_cfg", NVS_READWRITE, &nvs);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "Failed to open NVS for mqtt_cfg: %s", esp_err_to_name(err));
        cJSON_Delete(json);
        httpd_resp_send_err(req, HTTPD_500_INTERNAL_SERVER_ERROR, "NVS open failed");
        return ESP_OK;
    }

    ESP_ERROR_CHECK(nvs_set_str(nvs, "ip", ip));
    ESP_ERROR_CHECK(nvs_set_u16(nvs, "port", (uint16_t)port));
    ESP_ERROR_CHECK(nvs_commit(nvs));
    nvs_close(nvs);

    ESP_LOGI(TAG, "MQTT config saved: %s:%d", ip, port);

    cJSON_Delete(json);
    httpd_resp_sendstr(req, "OK");

    vTaskDelay(pdMS_TO_TICKS(500));
    esp_restart();   // same pattern as your WiFi config
    return ESP_OK;
}


static esp_err_t websocket_handler(httpd_req_t *req)
{
    if (!req)
        return ESP_FAIL;

    if (req->method == HTTP_GET)
    {
        int sock_fd = httpd_req_to_sockfd(req);
        xSemaphoreTake(clients_mutex, portMAX_DELAY);
        for (int i = 0; i < MAX_CLIENTS; ++i)
        {
            if (client_fds[i] == 0)
            {
                client_fds[i] = sock_fd;
                ESP_LOGI(TAG, "Client socket %d connected at index %d", sock_fd, i);
                break;
            }
        }
        xSemaphoreGive(clients_mutex);
        return ESP_OK;
    }

//     httpd_ws_frame_t ws_pkt = {
//         .final = true,
//         .fragmented = false,
//         .type = HTTPD_WS_TYPE_TEXT,
//         .payload = NULL,
//         .len = 0};

//     esp_err_t ret = httpd_ws_recv_frame(req, &ws_pkt, 0);
//     if (ret != ESP_OK)
//         return ret;

//     ws_pkt.payload = malloc(ws_pkt.len + 1);
//     if (!ws_pkt.payload)
//         return ESP_ERR_NO_MEM;

//     ret = httpd_ws_recv_frame(req, &ws_pkt, ws_pkt.len);
//     if (ret == ESP_OK)
//     {
//         ws_pkt.payload[ws_pkt.len] = '\0';
//         ESP_LOGI(TAG, "Received: %s", (char *)ws_pkt.payload);
//     }
//     free(ws_pkt.payload);
//     return ESP_OK;
// 
    return ESP_OK;
}

static httpd_uri_t ws_uri = {
    .uri = "/ws",
    .method = HTTP_GET,
    .handler = websocket_handler,
    .user_ctx = NULL,
    .is_websocket = true};

void websocket_broadcast_task(void *arg)
{
    char msg[128];

    while (1)
    {
        time_t now;
        struct tm timeinfo;
        time(&now);
        localtime_r(&now, &timeinfo);
        snprintf(msg, sizeof(msg),
                 "{\"temperature\":%.2f,\"humidity\":%.2f,\"pressure\":%.2f,"
                 "\"gas_resistance\":%.2f,\"timestamp\":\"%04d-%02d-%02d %02d:%02d:%02d\"}",
                 sensor_data.temperature,
                 sensor_data.humidity,
                 sensor_data.pressure,
                 sensor_data.gas_resistance,
                 timeinfo.tm_year + 1900, timeinfo.tm_mon + 1, timeinfo.tm_mday,
                 timeinfo.tm_hour, timeinfo.tm_min, timeinfo.tm_sec);
        // ESP_LOGI(TAG, "Broadcasting JSON: %s", msg);

        httpd_ws_frame_t frame = {
            .final = true,
            .fragmented = false,
            .type = HTTPD_WS_TYPE_TEXT,
            .payload = (uint8_t *)msg,
            .len = strlen(msg)};

        xSemaphoreTake(clients_mutex, portMAX_DELAY);
        for (int i = 0; i < MAX_CLIENTS; ++i)
        {
            if (client_fds[i] != 0)
            {
                esp_err_t err = httpd_ws_send_frame_async(ws_server, client_fds[i], &frame);
                if (err != ESP_OK)
                {
                    ESP_LOGW(TAG, "Failed to send to client_fd %d: %s", client_fds[i], esp_err_to_name(err));
                    client_fds[i] = 0;
                }
            }
        }
        xSemaphoreGive(clients_mutex);

        vTaskDelay(pdMS_TO_TICKS(1000));
    }
}

static esp_err_t reinitialize_handler(httpd_req_t *req)
{
    httpd_resp_send(req, "System reinitializing...", HTTPD_RESP_USE_STRLEN);
    vTaskDelay(pdMS_TO_TICKS(1000));
    esp_restart();
    return ESP_OK;
}

static httpd_uri_t reinitialize_uri = {
    .uri = "/reinitialize",
    .method = HTTP_GET,
    .handler = reinitialize_handler,
    .user_ctx = NULL};

// Serve index.html
static esp_err_t root_get_handler(httpd_req_t *req)
{
    FILE *f = fopen("/spiffs/index.html", "r");
    if (!f)
    {
        httpd_resp_send_404(req);
        return ESP_FAIL;
    }

    fseek(f, 0, SEEK_END);
    long file_size = ftell(f);
    fseek(f, 0, SEEK_SET);

    char *buffer = malloc(file_size + 1);
    if (!buffer)
    {
        fclose(f);
        httpd_resp_send_500(req);
        return ESP_FAIL;
    }

    fread(buffer, 1, file_size, f);
    buffer[file_size] = '\0';
    fclose(f);

    httpd_resp_set_type(req, "text/html");
    httpd_resp_send(req, buffer, file_size);
    free(buffer);
    return ESP_OK;
}

static httpd_uri_t root_uri = {
    .uri = "/",
    .method = HTTP_GET,
    .handler = root_get_handler,
    .user_ctx = NULL};

// Download Handler
static esp_err_t download_get_handler(httpd_req_t *req)
{
    FILE *f = fopen("/spiffs/sensor_data.txt", "r");
    if (!f)
    {
        httpd_resp_send_404(req);
        return ESP_FAIL;
    }

    char buffer[128];
    size_t bytes_read;
    httpd_resp_set_type(req, "text/plain");
    while ((bytes_read = fread(buffer, 1, sizeof(buffer), f)) > 0)
    {
        httpd_resp_send_chunk(req, buffer, bytes_read);
    }
    fclose(f);
    httpd_resp_send_chunk(req, NULL, 0);
    return ESP_OK;
}

static httpd_uri_t download_uri = {
    .uri = "/download",
    .method = HTTP_GET,
    .handler = download_get_handler,
    .user_ctx = NULL};


static esp_err_t delete_log_handler(httpd_req_t *req)
{
    FILE *f = fopen("/spiffs/sensor_data.txt", "w");
    if (!f)
    {
        httpd_resp_send_500(req);
        return ESP_FAIL;
    }
    fclose(f);
    httpd_resp_send(req, "Log file erased", HTTPD_RESP_USE_STRLEN);
    return ESP_OK;
}

static httpd_uri_t delete_log_uri = {
    .uri = "/delete_log",
    .method = HTTP_GET,
    .handler = delete_log_handler,
    .user_ctx = NULL};

esp_err_t set_wifi_handler(httpd_req_t *req)
{
    char buf[128];
    int ret = httpd_req_recv(req, buf, MIN(req->content_len, sizeof(buf) - 1));
    if (ret <= 0)
        return ESP_FAIL;

    buf[ret] = 0;

    cJSON *json = cJSON_Parse(buf);
    if (!json)
        return ESP_FAIL;

    const cJSON *ssid = cJSON_GetObjectItem(json, "ssid");
    const cJSON *password = cJSON_GetObjectItem(json, "password");

    if (ssid && password && ssid->valuestring && password->valuestring)
    {
        nvs_handle_t nvs;
        ESP_ERROR_CHECK(nvs_open("wifi_creds", NVS_READWRITE, &nvs));
        ESP_ERROR_CHECK(nvs_set_str(nvs, "ssid", ssid->valuestring));
        ESP_ERROR_CHECK(nvs_set_str(nvs, "pass", password->valuestring));
        ESP_ERROR_CHECK(nvs_commit(nvs));
        nvs_close(nvs);
        cJSON_Delete(json);
        httpd_resp_sendstr(req, "OK");
        vTaskDelay(pdMS_TO_TICKS(500));
        esp_restart(); 
        return ESP_OK;
    }

    cJSON_Delete(json);
    return ESP_FAIL;
}

httpd_uri_t set_wifi_uri = {
    .uri = "/set_wifi",
    .method = HTTP_POST,
    .handler = set_wifi_handler};

httpd_uri_t set_mqtt_uri = {
    .uri      = "/set_mqtt",
    .method   = HTTP_POST,
    .handler  = handle_set_mqtt,
    .user_ctx = NULL
};


esp_err_t handle_set_softap(httpd_req_t *req) {
    char buf[256];
    int len = httpd_req_recv(req, buf, sizeof(buf) - 1);
    if (len <= 0) return ESP_FAIL;
    buf[len] = 0;

    cJSON *json = cJSON_Parse(buf);
    if (!json) {
        httpd_resp_send_err(req, HTTPD_400_BAD_REQUEST, "Invalid JSON");
        return ESP_OK;
    }

    const cJSON *ssid_json = cJSON_GetObjectItem(json, "ssid");
    const cJSON *pass_json = cJSON_GetObjectItem(json, "password");
    const cJSON *auth_json = cJSON_GetObjectItem(json, "authmode");
    
    if (!ssid_json || !pass_json || !auth_json || 
        !ssid_json->valuestring || !pass_json->valuestring) {
        cJSON_Delete(json);
        httpd_resp_send_err(req, HTTPD_400_BAD_REQUEST, "Missing required fields");
        return ESP_OK;
    }

    const char *ssid = ssid_json->valuestring;
    const char *pass = pass_json->valuestring;
    int authmode = auth_json->valueint;
    
    ESP_LOGI(TAG, "SoftAP Config -> SSID: %s | Password: %s | Authmode: %d", 
             ssid, pass, authmode);

    if (authmode != WIFI_AUTH_OPEN && strlen(pass) < 8) {
        cJSON_Delete(json);
        httpd_resp_send_err(req, HTTPD_400_BAD_REQUEST, "Password too short");
        return ESP_OK;
    }

  
    nvs_handle_t nvs;
    esp_err_t nvs_err = nvs_open("softap_creds", NVS_READWRITE, &nvs);
    if (nvs_err == ESP_OK) {
        ESP_ERROR_CHECK(nvs_set_str(nvs, "ssid", ssid));
        ESP_ERROR_CHECK(nvs_set_str(nvs, "pass", pass));
        ESP_ERROR_CHECK(nvs_set_i32(nvs, "authmode", authmode)); 
        ESP_ERROR_CHECK(nvs_commit(nvs));
        nvs_close(nvs);
        ESP_LOGI(TAG, "SoftAP credentials saved to NVS");
    } else {
        ESP_LOGE(TAG, "Failed to open NVS for writing: %s", esp_err_to_name(nvs_err));
        cJSON_Delete(json);
        httpd_resp_send_err(req, HTTPD_500_INTERNAL_SERVER_ERROR, "NVS write failed");
        return ESP_OK;
    }

    cJSON_Delete(json);
    httpd_resp_sendstr(req, "OK");
    vTaskDelay(pdMS_TO_TICKS(500));
    esp_restart();
    return ESP_OK;
}

esp_err_t handle_erase_softap(httpd_req_t *req) {
    nvs_handle_t nvs;
    if (nvs_open("softap_creds", NVS_READWRITE, &nvs) == ESP_OK) {
        nvs_erase_all(nvs);
        nvs_commit(nvs);
        nvs_close(nvs);
        httpd_resp_sendstr(req, "SoftAP NVS cleared");
        esp_restart(); 
        return ESP_OK;
    } else {
        httpd_resp_send_err(req, HTTPD_500_INTERNAL_SERVER_ERROR, "Failed to erase NVS");
        return ESP_OK;
    }
}

httpd_uri_t softap_set_uri = {
    .uri = "/set_softap",
    .method = HTTP_POST,
    .handler = handle_set_softap,
    .user_ctx = NULL
};


httpd_uri_t erase_softap_uri = {
    .uri = "/erase_softap",
    .method = HTTP_GET,
    .handler = handle_erase_softap,
    .user_ctx = NULL
};


void start_webserver(void)
{
    httpd_config_t config = HTTPD_DEFAULT_CONFIG();
    config.max_uri_handlers = 10;
    httpd_handle_t server = NULL;

    clients_mutex = xSemaphoreCreateMutex();

    if (httpd_start(&server, &config) == ESP_OK)
    {
        ws_server = server;
        httpd_register_uri_handler(server, &root_uri);
        httpd_register_uri_handler(server, &download_uri);
        httpd_register_uri_handler(server, &delete_log_uri);
        httpd_register_uri_handler(server, &reinitialize_uri);
        httpd_register_uri_handler(server, &ws_uri);
        httpd_register_uri_handler(server, &set_wifi_uri);
        httpd_register_uri_handler(server, &softap_set_uri);
        httpd_register_uri_handler(server, &erase_softap_uri);
        httpd_register_uri_handler(server, &set_mqtt_uri);
    }

    xTaskCreate(websocket_broadcast_task, "ws_broadcast", 4096, NULL, 5, NULL);
}