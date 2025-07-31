#ifndef WEB_SERVER_H
#define WEB_SERVER_H


typedef struct {
    float temperature;
    float humidity;
    float pressure;
    float gas_resistance;
} sensor_data_t;

extern sensor_data_t sensor_data;

void start_webserver(void);
void websocket_broadcast_task(void *arg);
//void update_sensor_data(float temp, float hum, float press, float gas);

#endif