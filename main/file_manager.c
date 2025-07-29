#include <stdio.h>
#include <stdlib.h>
#include "esp_spiffs.h"
#include "esp_log.h"
#include "file_manager.h"
#include "esp_system.h"
#include "esp_heap_caps.h"


void write_data_to_file(const char *data)
{
    FILE* f = fopen("/spiffs/sensor_data.txt", "r");
    if (f == NULL) {
        ESP_LOGE("SPIFFS", "Failed to open file for reading");
        f = fopen("/spiffs/sensor_data.txt", "w");
        if (f == NULL) {
            ESP_LOGE("SPIFFS", "Failed to create file for writing");
            return;
        }
        fprintf(f, "%s\n", data);
        fclose(f);
        return;
    }

    fseek(f, 0, SEEK_END);
    long file_size = ftell(f);
    fseek(f, 0, SEEK_SET);

    char* buffer = (char*)malloc(file_size + 1);
    if (buffer == NULL) {
        ESP_LOGE("SPIFFS", "Failed to allocate memory");
        fclose(f);
        return;
    }

    fread(buffer, 1, file_size, f);
    buffer[file_size] = '\0';
    fclose(f);

    f = fopen("/spiffs/sensor_data.txt", "w");
    if (f == NULL) {
        ESP_LOGE("SPIFFS", "Failed to open file for writing");
        free(buffer);
        return;
    }

    ESP_LOGI("Heap Info", "Free heap size: %lu bytes", esp_get_free_heap_size());
    ESP_LOGI("Heap Info", "Minimum free heap size: %lu bytes", esp_get_minimum_free_heap_size());
    fprintf(f, "%s\n%s", data, buffer);
    fclose(f);

    free(buffer);
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