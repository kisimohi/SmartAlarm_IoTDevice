#include <ESP8266WiFi.h>
#include <PubSubClient.h>
#include <ArduinoJson.h>
extern "C" {
#include "user_interface.h"
}

#define WIFI_SSID "<<your ssid>>"
#define WIFI_PASSWORD "<<your ssid password>>"

#define MAX_RETRY_WAIT_MS 30000
#define INIT_RETRY_WAIT_MS 1000

#define KII_APP_ID  "<<your app id>>"
#define KII_APP_KEY "<<your app key>>"
#define KII_MQTT_SERVER "mqtt-jp.kii.com"
#define THING_VENDOR_ID  "1111"
#define THING_PASSWORD   "2222"

#define GPIO_LED_PORT 4
#define DELAY_SUCCESS_MS 100
#define SENSOR_HISTORY_COUNT 5
#define THRESHOLD_LIGHT_SENSOR_VALUE 600

#define MIN(a,b) (((a)<(b))?(a):(b))

struct MQTT_ENDPOINT {
  String host;
  String user;
  String password;
  String topic;
  String clientID;
  String accessToken;
  String thingID;
};

class LightHistory {
    int m_sensorHistory[SENSOR_HISTORY_COUNT];
    int m_writeIndex;

  public:
    LightHistory();
    void writeSensorValue(int value);
    int readSensorValue();
};

class KiiMqttClient {
    PubSubClient* m_mqttClient;

  public:
    KiiMqttClient(WiFiClient* wifiClient, String host);
    virtual ~KiiMqttClient();
    boolean connect(String user, String password, String clientID, void (*callback)(const MQTT::Publish& pub));
    boolean subscribe(String topic);
    boolean publish(String topic, String payload);
    void close();
    boolean connected();
    void loop();
};

MQTT_ENDPOINT* g_mqttEndpoint = NULL;
KiiMqttClient* g_mqttClientPrimary;
KiiMqttClient* g_mqttClientMain;
WiFiClient g_wifiClient;
LightHistory g_lightHistory;
int g_loopWaitPrevious = 0;
int g_tickCount = 1;

void setup()
{
  Serial.begin(115200);
  delay(10);
  Serial.println("Start");
  WiFi.mode(WIFI_STA);
  WiFi.printDiag(Serial);
  Serial.println();
  Serial.println("-----");
  pinMode(GPIO_LED_PORT, OUTPUT);
}

void loop()
{
  boolean success;
  if (WiFi.status() != WL_CONNECTED) {
    // If Wi-Fi unconnected, connect to Wi-Fi
    success = connectWifi();
  } else if (g_mqttEndpoint == NULL) {
    // If undefined the MQTT main server, connect to the MQTT primary server
    success = connectPrimaryServer();
  } else {
    // Otherwise, keep connecting to MQTT main server
    success = connectMqttServer();
  }

  if (success) {
    // If connected, execute the main logic
    if (g_mqttClientPrimary != NULL) {
      g_mqttClientPrimary->loop();
    }
    if (g_mqttClientMain != NULL) {
      g_mqttClientMain->loop();
    }
    g_loopWaitPrevious = 0;
    delay(DELAY_SUCCESS_MS);
  } else {
    if (g_loopWaitPrevious == 0) {
      g_loopWaitPrevious = INIT_RETRY_WAIT_MS;
    } else {
      g_loopWaitPrevious = MIN(MAX_RETRY_WAIT_MS, g_loopWaitPrevious * 2);
    }
    delay(g_loopWaitPrevious);
  }

  blinkLED();

  int sensorValue = system_adc_read();
  g_lightHistory.writeSensorValue(sensorValue);
}

void blinkLED()
{
  g_tickCount++;
  digitalWrite(GPIO_LED_PORT, g_tickCount / 5 % 2);
}

boolean connectWifi()
{
  Serial.print("Connecting");
  WiFi.begin(WIFI_SSID, WIFI_PASSWORD);
  Serial.println("...");

  while (WiFi.status() != WL_CONNECTED) {
    delay(1000);
    Serial.print(WiFi.status() );
  }

  Serial.println("WiFi connected");
  return true;
}

boolean connectPrimaryServer()
{
  boolean success;
  if (g_mqttClientPrimary == NULL) {
    g_mqttClientPrimary = new KiiMqttClient(&g_wifiClient, KII_MQTT_SERVER);
  }
  if (!g_mqttClientPrimary->connected()) {
    Serial.println("Connecting to primary MQTT server...");
    String user = "type=oauth2&client_id=" KII_APP_ID;
    String password  = "client_secret=" KII_APP_KEY;
    String clientID = "anonymous";
    success = g_mqttClientPrimary->connect(user, password, clientID, callbackGetEndpoint);
    if (!success) {
      Serial.println("Failed to connect.");
      g_mqttClientPrimary->close();
      g_mqttClientPrimary = NULL;
      return false;
    }
    Serial.println("Succeeded to connect.");

    // Onboard the thing
    String topicName = "p/anonymous/thing-if/apps/" KII_APP_ID "/onboardings";
    String publishPayload = String("POST\r\n"
                                   "Content-type: application/vnd.kii.OnboardingWithVendorThingIDByThing+json\r\n"
                                   "\r\n"
                                   "{\"vendorThingID\" : \"") + String(THING_VENDOR_ID) + String("\", \"thingPassword\" : \"") + String(THING_PASSWORD) + String("\"}");
    success = g_mqttClientPrimary->publish(topicName, publishPayload);
    if (!success) {
      Serial.println("Failed to publish.");
      g_mqttClientPrimary->close();
      g_mqttClientPrimary = NULL;
      return false;
    }
    Serial.println("Start onboarding");
  }
  return true;
}

void callbackGetEndpoint(const MQTT::Publish& pub)
{
  // {
  //   "accessToken" : "xxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxx",
  //   "thingID" : "th.xxxxxxxxxxxx-xxxx-xxxx-xxxx-xxxxxxxx",
  //   "mqttEndpoint" : {
  //     "installationID" : "xxxxxxxxxxxxxxxxxxxxxxxxx",
  //     "username" : "xxxxxxxx-xxxxxxxxxxxxxxxxxxxxxxx",
  //     "password" : "xxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxx",
  //     "mqttTopic" : "xxxxxxxxxxxxxxxxxxxxxxx",
  //     "host" : "jp-mqtt-xxxxxxxxxxxx.kii.com",
  //     "portTCP" : 1883,
  //     "portSSL" : 8883,
  //     "portWS" : 12470,
  //     "portWSS" : 12473,
  //     "ttl" : 2147483647
  //   }
  // }
  String payload = pub.payload_string();
  Serial.println("Message arrived");

  int index = payload.indexOf("\r\n\r\n");
  if (index == -1) {
    Serial.println("Unexpected payload format for onbording.");
    return;
  }
  String json = payload.substring(index + 4, payload.length());
  Serial.println(json);

  StaticJsonBuffer<1000> jsonBuffer;
  JsonObject& root = jsonBuffer.parseObject(json);
  if (!root.success()) {
    Serial.println("Failed to parseObject() for onboarding.");
    return;
  }
  String host = root["mqttEndpoint"]["host"];
  String user = root["mqttEndpoint"]["username"];
  String password = root["mqttEndpoint"]["password"];
  String topic = root["mqttEndpoint"]["mqttTopic"];
  String accessToken = root["accessToken"];
  String thingID = root["thingID"];
  Serial.println(host);
  Serial.println(user);
  Serial.println(password);
  Serial.println(topic);

  g_mqttEndpoint = new MQTT_ENDPOINT();
  g_mqttEndpoint->host = host;
  g_mqttEndpoint->user = user;
  g_mqttEndpoint->password = password;
  g_mqttEndpoint->topic = topic;
  g_mqttEndpoint->clientID = topic;
  g_mqttEndpoint->accessToken = accessToken;
  g_mqttEndpoint->thingID = thingID;
}

boolean connectMqttServer()
{
  boolean success;

  // The primary connection is no more necessary
  g_mqttClientPrimary->close();

  if (g_mqttClientMain == NULL) {
    g_mqttClientMain = new KiiMqttClient(&g_wifiClient, g_mqttEndpoint->host);
  }

  if (!g_mqttClientMain->connected()) {
    Serial.println("Connecting to main MQTT server...");
    success = g_mqttClientMain->connect(g_mqttEndpoint->user, g_mqttEndpoint->password, g_mqttEndpoint->clientID, callbackMain);
    if (!success) {
      Serial.println("Failed to connect.");
      g_mqttClientMain->close();
      g_mqttClientMain = NULL;
      return false;
    }
    Serial.println("Succeeded to connect.");

    g_mqttClientMain->subscribe(g_mqttEndpoint->topic);
    if (!success) {
      Serial.println("Failed to subscribe.");
      g_mqttClientMain->close();
      g_mqttClientMain = NULL;
      return false;
    }
    Serial.println("Succeeded to subscribe.");
  }
  return true;
}

void callbackMain(const MQTT::Publish& pub)
{
  // {"schema":"HelloThingIF-Schema","schemaVersion":1,"commandID":"xxxxxxxx-xxxx-xxxx-xxxxxxxxxxxxxxxxx","actions":[{"turnPower":{}],"issuer":"user:xxxxxxxxxxxx-xxxx-xxxx-xxxx-xxxxxxxx}"
  boolean success;

  String mqttTopicName = pub.topic();
  if (!mqttTopicName.equals(g_mqttEndpoint->topic)) {
    return;
  }

  // handle message arrived
  Serial.println("Message arrived");
  String json = pub.payload_string();
  Serial.println(json);
  StaticJsonBuffer<1000> jsonBuffer;
  JsonObject& root = jsonBuffer.parseObject(json);
  if (!root.success()) {
    Serial.println("Failed to parseObject() for command.");
    return;
  }
  String schema = root["schema"];
  int version = root["schemaVersion"];
  String commandID = root["commandID"];

  int brightness = g_lightHistory.readSensorValue();
  String result;
  if (brightness > THRESHOLD_LIGHT_SENSOR_VALUE) {
    result = String("\"succeeded\":true");
  } else {
    result = String("\"succeeded\":false,\"errorMessage\":\"error\"");
  }
  Serial.print("LightSensor:");
  Serial.print(brightness);
  Serial.print(", ActionResult:");
  Serial.println(result);
  
  String topicName = String("p/") + g_mqttEndpoint->clientID + String("/thing-if/apps/" KII_APP_ID
                     "/targets/thing:") + g_mqttEndpoint->thingID + String("/commands/") + commandID + String("/action-results");
  String publishPayload = String("PUT\r\n"
                                 "Authorization: Bearer ") + String(g_mqttEndpoint->accessToken) + String("\r\n"
                                 "X-Kii-AppID: " KII_APP_ID "\r\n"
                                 "X-Kii-AppKey: " KII_APP_KEY "\r\n"
                                 "Content-Type: application/json\r\n"
                                 "\r\n"
                                 "{\"actionResults\" : ["
                                   "{\"checkSensorAction\":{") + result + String("}}"
                                 "]}");

  success = g_mqttClientMain->publish(topicName, publishPayload);
  if (!success) {
    Serial.println("Failed to publish the command result.");
    g_mqttClientMain->close();
    g_mqttClientMain = NULL;
    return;
  }
  Serial.println("Command result published");
}


LightHistory::LightHistory()
{
  m_writeIndex = 0;
}

void LightHistory::writeSensorValue(int value)
{
  m_sensorHistory[m_writeIndex] = value;
  m_writeIndex = (m_writeIndex + 1) % SENSOR_HISTORY_COUNT;
}

int LightHistory::readSensorValue()
{
  int total = 0;
  for (int i = 0; i < SENSOR_HISTORY_COUNT; i++) {
    total += m_sensorHistory[i];
  }
  return total / SENSOR_HISTORY_COUNT;
}


KiiMqttClient::KiiMqttClient(WiFiClient* wifiClient, String host)
{
  m_mqttClient = new PubSubClient(*wifiClient, host);
}

KiiMqttClient::~KiiMqttClient()
{
  close();
}

boolean KiiMqttClient::connect(String user, String password, String clientID, void (*callback)(const MQTT::Publish& pub))
{
  boolean success;

  if (m_mqttClient == NULL) {
    return false;
  }
  success = m_mqttClient->connect(MQTT::Connect(clientID).set_auth(user, password));
  if (!success) {
    return false;
  }
  m_mqttClient->set_callback(callback);

  return true;
}

boolean KiiMqttClient::subscribe(String topic)
{
  boolean success;
  success = m_mqttClient->subscribe(topic);
  if (!success) {
    close();
    return false;
  }

  return true;
}

boolean KiiMqttClient::publish(String topic, String payload)
{
  boolean success;
  success = m_mqttClient->publish(topic, payload);
  if (!success) {
    close();
    return false;
  }

  return true;
}

void KiiMqttClient::close()
{
  if (m_mqttClient != NULL) {
    m_mqttClient->set_callback(NULL);
    m_mqttClient->disconnect();
    m_mqttClient = NULL;
  }
}

void KiiMqttClient::loop()
{
  if (m_mqttClient == NULL) {
    return;
  }
  if (!m_mqttClient->connected()) {
    return;
  }
  m_mqttClient->loop();
}

boolean KiiMqttClient::connected()
{
  if (m_mqttClient == NULL) {
    return false;
  }
  return m_mqttClient->connected();
}
