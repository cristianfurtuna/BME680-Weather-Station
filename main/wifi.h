#ifndef WIFI_H
#define WIFI_H


#define DEFAULT_WIFI_SSID "DEFAULT_WIFI_SSID"
#define DEFAULT_WIFI_PASS "DEFAULT_WIFI_PASS"

#define DEFAULT_SOFTAP_SSID "BME680 Weather Station"
#define DEFAULT_SOFTAP_PASS "123456789"
#define DEFAULT_SOFTAP_AUTHMODE WIFI_AUTH_WPA2_PSK

void wifi_init_sta(void);
void wifi_init_softap(void);
void initialize_mdns(void);
bool wifi_is_connected(void);
bool wifi_is_softap_mode(void);


#endif