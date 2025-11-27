#ifndef MQTT_H
#define MQTT_H

#include <stdbool.h>
#include "web_server.h"

#define MQTT_DEFAULT_BROKER_URI   "mqtt://192.168.0.159:1883"

#define MQTT_CLIENT_ID    "bme680_weather_station"
#define MQTT_TOPIC_STATE  "home/weather/bme680/state"
#define MQTT_TOPIC_STATUS "home/weather/bme680/status"

void mqtt_start(void);
void mqtt_stop(void);
void mqtt_publish_sensor(void);
bool mqtt_is_connected(void);

#endif
