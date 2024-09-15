#ifndef LED_BULTIN_H
#define LED_BULTIN_H

#include <stdio.h>
#include "led_strip.h"
#include "driver/gpio.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

#define LED_GPIO GPIO_NUM_8   
#define LED_STRIP_LENGTH 1     


static TaskHandle_t led_blink_task_handle = NULL;
static rgb_t white = { .r = 255, .g = 255, .b = 255};
static rgb_t black = { .r = 0,   .g = 0,   .b = 0};
static rgb_t red =   { .r = 255, .g = 0,   .b = 0};
static rgb_t blue =  { .r = 0,   .g = 0,   .b = 255};
static rgb_t green = { .r = 0,   .g = 255, .b = 0};

void led_builtin_color(rgb_t color, uint8_t brightness);
void set_brightness (uint8_t brightness);
void led_blink_red (void *arg);
void led_blink_blue (void *arg);

#endif