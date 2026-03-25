BME680 weather station
======================

Weather station based on an ESP32C6 development module and a BME680 sensor.
The project's main objective is to create an autonomous data aquisition system based on the BME680 that is reconfigurable and uses Wi-Fi connectivity.

Changelog
v0.0 Basic Weather station with instant WiFi reading

v0.1 Added log function written as a .txt file in spiffs

v0.2 Updated webpage with some css and javascript confirmation functions and migrated it to spiffs, log entries arranged to be written from the   newest to the latest, chunked http response for the webpage (so it can send more information without overloading the memory), reinitialization function added.

v0.3 Added lights (for the builtin LED, red blinking - connecting to WiFi, green - connected to WiFi, blue - SoftAP mode), added SoftAP mode if the ESP can't find the desired WiFi station, added hostname using mDNS for an easier access to the webpage

v0.3.1 Bug fixes regarding SoftAP mode.

v0.3.2 Code reorganization.

v0.4   Migration to Websockets

v0.5   WiFi SSID and password change integrated in webpage

v1.0   SoftAP SSID, password change and authmode integrated in webpage

v1.3   Added MQTT support with configurable broker on webpage. Added gas resistance support with warnings on VOC detection.

Final assembly:
![Asamblare![Top_view](https://github.com/user-attachments/assets/39387e0b-113d-4c34-be6f-1f4ecdfa3755)
_finala](https://github.com/user-attachments/assets/65713bdb-93ed-4415-9723-563ea2c2f9e6)

Wiring diagram:
<img width="1974" height="950" alt="Schema_fritzing_bb" src="https://github.com/user-attachments/assets/9ec431bf-4ea8-4210-aafe-41cf0fb33f1d" />



