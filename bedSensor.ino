#include <FS.h> // needs to be first
#include <WiFiManager.h> // https://github.com/tzapu/WiFiManager
#include <WiFi.h>
#include <PubSubClient.h>
#include <SPIFFS.h>
#include <ArduinoJson.h> // v6+

#define FORMAT_SPIFFS_IF_FAILED true
#define FILTER_LENGTH 50
#define LOOP_DELAY 10

//define your default values here, if there are different values in config.json, they are overwritten.
char mqtt_server[40];
char mqtt_port[6] = "1883";
char mqtt_username[15];
char mqtt_password[15];
char mqtt_topic[30] = "bedroom/bed_sensor";
char sensor_delay[10] = "60000";

//flag for saving data
bool shouldSaveConfig = false;

//sensor variables
const int noSensors = 2;
int pirPins[noSensors] = {5, 6}; // pin numbers
int sensorOutput[noSensors] = {0, 0}; // default to no output

// struc to hold sensor data
typedef struct {
  int buf[FILTER_LENGTH];
  float percentage;
  int bufIndex;
  unsigned long output_timer;
  int output;
} SensorObj;

SensorObj SensorData[noSensors];

// initilaize struct for sensors
void Sensor_Init(SensorObj *sensor) {
  for (int n = 0; n < FILTER_LENGTH; n++) {
    sensor->buf[n] = 0;
  }
  sensor->percentage = 0.0;
  sensor->bufIndex = 0;
  sensor->output_timer = 0;
  sensor->output = 0;
}

// update struct for sensors
int Sensor_Update(SensorObj *sensor, int input) {
  int old_output = sensor->output;
  sensor->buf[sensor->bufIndex] = input;

  sensor->bufIndex++;

  if (sensor->bufIndex == FILTER_LENGTH) {
    sensor->bufIndex = 0;
  }

  int pirOutput = 0;
  int trig_count = 0;
  for (int n = 0; n < FILTER_LENGTH; n++) {
    trig_count += sensor->buf[n];
  }

  sensor->percentage = (float)trig_count/FILTER_LENGTH * 100;

  if (sensor->percentage > 75.0) {
    pirOutput = 1;
  }

  if (pirOutput == 1) {
    sensor->output_timer = millis();
    sensor->output = 1;
  }

  int outputDelay = atoi(sensor_delay);
  if ((millis() - sensor->output_timer) > outputDelay) {
    sensor->output = 0;
  }

  if (old_output != sensor->output) {
    prepareMqttMessage();
  }
  
  return sensor->output;
}

//callback notifying us of the need to save config
void saveConfigCallback () {
  Serial.println("Should save config");
  shouldSaveConfig = true;
}

//mqtt client
WiFiClient espClient;
PubSubClient client(espClient);

// mqtt reconnect
void reconnect() {
  // Loop until we're reconnected
  while (!client.connected()) {
    Serial.print("Attempting MQTT connection...");
    // Create a random client ID
    String clientId = "ESP32Client-";
    clientId += String(random(0xffff), HEX);
    // Attempt to connect
    if (client.connect(clientId.c_str(), mqtt_username, mqtt_password)) {
      Serial.println("connected");
      char mqtt_sub_topic[40];
      const char* mqtt_config= "/config";
      strcpy(mqtt_sub_topic, mqtt_topic ); 
      strcat(mqtt_sub_topic, mqtt_config);
      client.subscribe(mqtt_sub_topic);
    } else {
      Serial.print("failed, rc=");
      Serial.print(client.state());
      Serial.println(" try again in 5 seconds");
      // Wait 5 seconds before retrying
      delay(5000);
    }
  }
}

// mqtt publish message
void publishMessage(const char* topic, String payload, boolean retained) {
  if (client.publish(topic, payload.c_str(), true))
    Serial.println("Message publised [" + String(topic) + "]: " + payload);
}

// mqtt call back for received messages
void mqttCallback(char* topic, byte* payload, unsigned int length) {
  String incommingMessage = "";
  for (int i = 0; i < length; i++) incommingMessage += (char)payload[i];

  Serial.println("Message arrived [" + String(topic) + "]: " + incommingMessage);

  // check for sensor_delay
  DynamicJsonDocument json(256);
  auto deserializeError = deserializeJson(json, incommingMessage);
  if ( ! deserializeError ) {
    char new_delay[10];
    sprintf(new_delay, "%d", json["sensor_delay"].as<unsigned int>()); // int to string conversion
    Serial.println(new_delay);
    if ( strlen(new_delay) > 2 ) { // strlen handles if property even exists
      strcpy(sensor_delay, new_delay);
    }
  } else {
    Serial.println("failed to load json config");
  }
}

void prepareMqttMessage() {
  StaticJsonDocument<265> doc;
  for (int i = 0; i < noSensors; i++) {
    doc["sensor" + String(i+1)] = SensorData[i].output ? "ON" : "OFF";
  }
  char mqtt_message[265];
  serializeJson(doc, mqtt_message);

  publishMessage(mqtt_topic, mqtt_message, true);
}

void setup() {
  // initialize serial
  Serial.begin(115200);
  while (!Serial)
    delay(10);
  Serial.println("Serial online");

  // initialize sonsor pins and sensor struct
  for (int i  = 0; i < noSensors; i++) {
    pinMode(pirPins[i], INPUT);
    Sensor_Init(&SensorData[i]);
  }
  
  //clean FS, for testing
  //SPIFFS.format();

  //read configuration from FS json
  Serial.println("mounting FS...");

  if (SPIFFS.begin(FORMAT_SPIFFS_IF_FAILED)) {
    Serial.println("mounted file system");
    if (SPIFFS.exists("/config.json")) {
      //file exists, reading and loading
      Serial.println("reading config file");
      File configFile = SPIFFS.open("/config.json", "r");
      if (configFile) {
        Serial.println("opened config file");
        size_t size = configFile.size();
        // Allocate a buffer to store contents of the file.
        std::unique_ptr<char[]> buf(new char[size]);

        configFile.readBytes(buf.get(), size);

        DynamicJsonDocument json(1024);
        auto deserializeError = deserializeJson(json, buf.get());
        serializeJson(json, Serial);
        if ( ! deserializeError ) {
          Serial.println("\nparsed json");
          strcpy(mqtt_server, json["mqtt_server"]);
          strcpy(mqtt_port, json["mqtt_port"]);
          strcpy(mqtt_username, json["mqtt_username"]);
          strcpy(mqtt_password, json["mqtt_password"]);
          strcpy(mqtt_topic, json["mqtt_topic"]);
          strcpy(sensor_delay, json["sensor_delay"]);
        } else {
          Serial.println("failed to load json config");
        }
        configFile.close();
      }
    }
  } else {
    Serial.println("failed to mount FS");
  }
  //end read

  // The extra parameters to be configured (can be either global or just in the setup)
  // After connecting, parameter.getValue() will get you the configured value
  // id/name placeholder/prompt default length
  WiFiManagerParameter custom_mqtt_server("server", "mqtt server", mqtt_server, 40);
  WiFiManagerParameter custom_mqtt_port("port", "mqtt port", mqtt_port, 6);
  WiFiManagerParameter custom_mqtt_username("username", "mqtt username", mqtt_username, 15);
  WiFiManagerParameter custom_mqtt_password("password", "mqtt password", mqtt_password, 15,"type='password'");
  WiFiManagerParameter custom_mqtt_topic("topic", "mqtt topic", mqtt_topic, 30);
  WiFiManagerParameter custom_sensor_delay("sensor_delay", "PIR sensor delay", sensor_delay, 8);

  //WiFiManager
  WiFiManager wifiManager;

  //set config save notify callback
  wifiManager.setSaveConfigCallback(saveConfigCallback);

  //add all your parameters here
  wifiManager.addParameter(&custom_mqtt_server);
  wifiManager.addParameter(&custom_mqtt_port);
  wifiManager.addParameter(&custom_mqtt_username);
  wifiManager.addParameter(&custom_mqtt_password);
  wifiManager.addParameter(&custom_mqtt_topic);
  wifiManager.addParameter(&custom_sensor_delay);

  //set dark mode
  wifiManager.setDarkMode(true);

  //reset settings - for testing
  // wifiManager.resetSettings();

  //and goes into a blocking loop awaiting configuration
  if (!wifiManager.autoConnect("AutoConnectAP", "password")) {
    Serial.println("failed to connect and hit timeout");
    delay(3000);
    //reset and try again, or maybe put it to deep sleep
    ESP.restart();
    delay(5000);
  }

  //if you get here you have connected to the WiFi
  Serial.println("connected to WiFi");
  Serial.print("Local ip: ");
  Serial.println(WiFi.localIP());

  //read updated parameters
  strcpy(mqtt_server, custom_mqtt_server.getValue());
  strcpy(mqtt_port, custom_mqtt_port.getValue());
  strcpy(mqtt_username, custom_mqtt_username.getValue());
  strcpy(mqtt_password, custom_mqtt_password.getValue());
  strcpy(mqtt_topic, custom_mqtt_topic.getValue());
  strcpy(sensor_delay, custom_sensor_delay.getValue());
  Serial.println("The values in the file are: ");
  Serial.println("\tmqtt_server : " + String(mqtt_server));
  Serial.println("\tmqtt_port : " + String(mqtt_port));
  Serial.println("\tmqtt_username : " + String(mqtt_username));
  Serial.println("\tmqtt_password : " + String(mqtt_password));
  Serial.println("\tmqtt_topic : " + String(mqtt_topic));
  Serial.println("\tsensor_delay : " + String(sensor_delay));

  //save the custom parameters to FS
  if (shouldSaveConfig) {
    Serial.println("saving config");
    DynamicJsonDocument json(1024);
    json["mqtt_server"] = mqtt_server;
    json["mqtt_port"] = mqtt_port;
    json["mqtt_username"] = mqtt_username;
    json["mqtt_password"] = mqtt_password;
    json["mqtt_topic"] = mqtt_topic;
    json["sensor_delay"] = sensor_delay;

    File configFile = SPIFFS.open("/config.json", "w");
    if (!configFile) {
      Serial.println("failed to open config file for writing");
    }

    // serializeJson(json, Serial);
    serializeJson(json, configFile);
    configFile.close();
    //end save
  }

  // mqtt
  int mqttPort = atoi(mqtt_port);
  client.setServer(mqtt_server, mqttPort);
  client.setCallback(mqttCallback);
}

void loop() {
  //mqtt client check
  if (!client.connected()) reconnect();
  client.loop();

  for (int i = 0; i < noSensors; i++) {
    int pirState = digitalRead(pirPins[i]);
    Sensor_Update(&SensorData[i], pirState);
  }

  delay(LOOP_DELAY);
}