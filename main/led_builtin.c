#include <stdio.h>
#include "led_strip.h"
#include "driver/gpio.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "led_builtin.h"

static led_strip_t led_builtin = {
    .type = LED_STRIP_WS2812,
    .is_rgbw = false,
    .length = LED_STRIP_LENGTH,        
    .gpio = LED_GPIO, 
    .channel = RMT_CHANNEL_0,
    .brightness = 128    
};

void set_brightness (uint8_t brightness){
    led_builtin.brightness = brightness;
}

void led_builtin_color (rgb_t color, uint8_t brightness){
    led_strip_install(&led_builtin);
    led_strip_init(&led_builtin);
    for (int i = 0; i < LED_STRIP_LENGTH; i++) {
            led_strip_set_pixel(&led_builtin, i, color);
        }
    set_brightness (brightness);
    ESP_ERROR_CHECK(led_strip_flush(&led_builtin));
}

void led_blink_red (void *arg) {
    while (1) {
        led_builtin_color(red, 128);  
        vTaskDelay(pdMS_TO_TICKS(500));  
        led_builtin_color(black, 0); 
        vTaskDelay(pdMS_TO_TICKS(500));  
    }
}

void led_blink_blue (void *arg) {
    while (1) {
        led_builtin_color(blue, 128);  
        vTaskDelay(pdMS_TO_TICKS(500));  
        led_builtin_color(black, 0); 
        vTaskDelay(pdMS_TO_TICKS(500));  
    }
}


