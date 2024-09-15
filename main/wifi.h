#ifndef WIFI_H
#define WIFI_H


#define WIFI_SSID "ssid"
#define WIFI_PASS "pass"

#define AP_SSID "BME680 Weather Station"
#define AP_PASS "123456789"

void wifi_init_sta(void);
void wifi_init_softap(void);
void initialize_mdns(void);

#endif