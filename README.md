# bedSensor
Simple PIR sensor to turn on LED strip under the bed via MQTT

Supports more than one PIR sensor on the same ESP32 for left and right operation.
Uses WiFi manager for WiFi and MQTT configuration.

To-Do:
Add option to configure MQTT and sensor delay over web server when ESP32 is already conented to WiFi. 
This requires nonblocking WiFi Manager and MQTT client implementation or separate web server.
