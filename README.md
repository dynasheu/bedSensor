# bedSensor
Simple PIR sensor to turn on LED strip under the bed via MQTT

Supports more thatn one PIR sensor on the same ESP32 for left and right operation.
Uses WiFi manager for WiFi and MQTT configuration.

To-Do:
- add option to output value ona pin as well.
- add option to configure sensor delay over MQTT. This was added but maybe it would be better to convert sensor_delay to int variable.
- add option to configure MQTT and sensor delay over web server when ESP32 is already conented to WiFi. This requires nonblocking WiFi Manager and MQTT client implementation.
