BME680 weather station
====================

Weather station based on an ESP32C6 development module and a BME680 sensor.
Work in progress

v0 Basic Weather station with instant WiFi reading
v0.1 Added log function written as a .txt file in spiffs
v0.2 Updated webpage with some css and javascript confirmation functions and migrated it to spiffs, log entries arranged to be written from the newest to the latest, chunked http response for the webpage (so it can send more information without overloading the memory), reinitialization function added.