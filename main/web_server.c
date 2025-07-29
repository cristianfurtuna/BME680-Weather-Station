#include "esp_http_server.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include "esp_log.h"
#include "web_server.h"


sensor_data_t sensor_data;

esp_err_t reinitialize_handler(httpd_req_t *req)
{
    httpd_resp_send(req, "System reinitializing...", HTTPD_RESP_USE_STRLEN);
    vTaskDelay(pdMS_TO_TICKS(1000));
    esp_restart();
    
    return ESP_OK;
}

httpd_uri_t reinitialize_uri = {
    .uri      = "/reinitialize",
    .method   = HTTP_GET,
    .handler  = reinitialize_handler,
    .user_ctx = NULL
};

esp_err_t root_get_handler(httpd_req_t *req)
{

    FILE *f = fopen("/spiffs/index.html", "r");

    if (f == NULL) {
        httpd_resp_send_404(req);
        return ESP_FAIL;
    }

    fseek(f, 0, SEEK_END);
    long file_size = ftell(f);
    fseek(f, 0, SEEK_SET);

    char *buffer = malloc(file_size + 1);
    if (buffer == NULL) {
        fclose(f);
        httpd_resp_send_500(req);
        return ESP_FAIL;
    }

    fread(buffer, 1, file_size, f);
    buffer[file_size] = '\0';
    fclose(f);


    time_t now;
    time(&now);
    struct tm timeinfo;
    localtime_r(&now, &timeinfo);

    size_t resp_size = snprintf(NULL, 0, buffer,
             timeinfo.tm_mday, timeinfo.tm_mon + 1, timeinfo.tm_year + 1900, 
             timeinfo.tm_hour, timeinfo.tm_min, timeinfo.tm_sec,
             sensor_data.temperature, sensor_data.humidity,
             sensor_data.pressure, sensor_data.gas_resistance);
             

    char *resp_str = malloc(resp_size + 1);
    if (resp_str == NULL) {
        free(buffer);
        httpd_resp_send_500(req);
        return ESP_FAIL;
    }

    snprintf(resp_str, resp_size + 1, buffer,
             timeinfo.tm_mday, timeinfo.tm_mon + 1, timeinfo.tm_year + 1900, 
             timeinfo.tm_hour, timeinfo.tm_min, timeinfo.tm_sec,
             sensor_data.temperature, sensor_data.humidity,
             sensor_data.pressure, sensor_data.gas_resistance);


    size_t chunk_size = 512; 
    size_t remaining = strlen(resp_str);
    char *ptr = resp_str;

    while (remaining > 0) {
        size_t send_size = (remaining < chunk_size) ? remaining : chunk_size;
        if (httpd_resp_send_chunk(req, ptr, send_size) != ESP_OK) {
            free(buffer);
            free(resp_str);
            return ESP_FAIL;
        }
        ptr += send_size;
        remaining -= send_size;
    }

    httpd_resp_send_chunk(req, NULL, 0);

    free(buffer);
    free(resp_str);

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


esp_err_t delete_log_handler(httpd_req_t *req)
{
    FILE* f = fopen("/spiffs/sensor_data.txt", "w");
    if (f == NULL) {
        httpd_resp_send_500(req);
        return ESP_FAIL;
    }
    fclose(f);
    httpd_resp_send(req, "Log file erased", HTTPD_RESP_USE_STRLEN);
    return ESP_OK;
}

httpd_uri_t delete_log_uri = {
    .uri      = "/delete_log",
    .method   = HTTP_GET,
    .handler  = delete_log_handler,
    .user_ctx = NULL
};


void start_webserver(void)
{
    httpd_config_t config = HTTPD_DEFAULT_CONFIG();
    httpd_handle_t server = NULL;

    if (httpd_start(&server, &config) == ESP_OK) {
        httpd_register_uri_handler(server, &root_uri);
        httpd_register_uri_handler(server, &download_uri);
        httpd_register_uri_handler(server, &delete_log_uri);
        httpd_register_uri_handler(server, &reinitialize_uri);
    }
}