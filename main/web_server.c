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

#define MAX_CLIENTS 8

static const char *TAG = "web_server";

static httpd_handle_t ws_server = NULL;
sensor_data_t sensor_data;

static SemaphoreHandle_t clients_mutex;
static int client_fds[MAX_CLIENTS] = {0};

// WebSocket Handler
static esp_err_t websocket_handler(httpd_req_t *req) {
    if (!req) return ESP_FAIL;

    if (req->method == HTTP_GET) {
        int sock_fd = httpd_req_to_sockfd(req);
        xSemaphoreTake(clients_mutex, portMAX_DELAY);
        for (int i = 0; i < MAX_CLIENTS; ++i) {
            if (client_fds[i] == 0) {
                client_fds[i] = sock_fd;
                ESP_LOGI(TAG, "Client socket %d connected at index %d", sock_fd, i);
                break;
            }
        }
        xSemaphoreGive(clients_mutex);
        return ESP_OK;
    }

    httpd_ws_frame_t ws_pkt = {
        .final = true,
        .fragmented = false,
        .type = HTTPD_WS_TYPE_TEXT,
        .payload = NULL,
        .len = 0
    };

    esp_err_t ret = httpd_ws_recv_frame(req, &ws_pkt, 0);
    if (ret != ESP_OK) return ret;

    ws_pkt.payload = malloc(ws_pkt.len + 1);
    if (!ws_pkt.payload) return ESP_ERR_NO_MEM;

    ret = httpd_ws_recv_frame(req, &ws_pkt, ws_pkt.len);
    if (ret == ESP_OK) {
        ws_pkt.payload[ws_pkt.len] = '\0';
        ESP_LOGI(TAG, "Received: %s", (char *)ws_pkt.payload);
    }
    free(ws_pkt.payload);
    return ESP_OK;
}

static httpd_uri_t ws_uri = {
    .uri = "/ws",
    .method = HTTP_GET,
    .handler = websocket_handler,
    .user_ctx = NULL,
    .is_websocket = true
};

// Broadcast Task
void websocket_broadcast_task(void *arg) {
    char msg[128];



    while (1) {
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
         //ESP_LOGI(TAG, "Broadcasting JSON: %s", msg);


        httpd_ws_frame_t frame = {
            .final = true,
            .fragmented = false,
            .type = HTTPD_WS_TYPE_TEXT,
            .payload = (uint8_t *)msg,
            .len = strlen(msg)
        };

        xSemaphoreTake(clients_mutex, portMAX_DELAY);
        for (int i = 0; i < MAX_CLIENTS; ++i) {
            if (client_fds[i] != 0) {
                esp_err_t err = httpd_ws_send_frame_async(ws_server, client_fds[i], &frame);
                if (err != ESP_OK) {
                    ESP_LOGW(TAG, "Failed to send to client_fd %d: %s", client_fds[i], esp_err_to_name(err));
                    client_fds[i] = 0;
                }
            }
        }
        xSemaphoreGive(clients_mutex);

        vTaskDelay(pdMS_TO_TICKS(1000));
    }
}

// Reinitialize Handler
static esp_err_t reinitialize_handler(httpd_req_t *req) {
    httpd_resp_send(req, "System reinitializing...", HTTPD_RESP_USE_STRLEN);
    vTaskDelay(pdMS_TO_TICKS(1000));
    esp_restart();
    return ESP_OK;
}

static httpd_uri_t reinitialize_uri = {
    .uri = "/reinitialize",
    .method = HTTP_GET,
    .handler = reinitialize_handler,
    .user_ctx = NULL
};

// Serve index.html
static esp_err_t root_get_handler(httpd_req_t *req) {
    FILE *f = fopen("/spiffs/index.html", "r");
    if (!f) {
        httpd_resp_send_404(req);
        return ESP_FAIL;
    }

    fseek(f, 0, SEEK_END);
    long file_size = ftell(f);
    fseek(f, 0, SEEK_SET);

    char *buffer = malloc(file_size + 1);
    if (!buffer) {
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
    .user_ctx = NULL
};

// Download Handler
static esp_err_t download_get_handler(httpd_req_t *req) {
    FILE* f = fopen("/spiffs/sensor_data.txt", "r");
    if (!f) {
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

static httpd_uri_t download_uri = {
    .uri = "/download",
    .method = HTTP_GET,
    .handler = download_get_handler,
    .user_ctx = NULL
};

// Delete Log Handler
static esp_err_t delete_log_handler(httpd_req_t *req) {
    FILE* f = fopen("/spiffs/sensor_data.txt", "w");
    if (!f) {
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
    .user_ctx = NULL
};

// Start HTTP/WebSocket Server
void start_webserver(void) {
    httpd_config_t config = HTTPD_DEFAULT_CONFIG();
    httpd_handle_t server = NULL;

    clients_mutex = xSemaphoreCreateMutex();

    if (httpd_start(&server, &config) == ESP_OK) {
        ws_server = server;
        httpd_register_uri_handler(server, &root_uri);
        httpd_register_uri_handler(server, &download_uri);
        httpd_register_uri_handler(server, &delete_log_uri);
        httpd_register_uri_handler(server, &reinitialize_uri);
        httpd_register_uri_handler(server, &ws_uri);
    }

    xTaskCreate(websocket_broadcast_task, "ws_broadcast", 4096, NULL, 5, NULL);
}