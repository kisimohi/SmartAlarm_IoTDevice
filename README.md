# Smart Alarm for ESP8266

This is a smart alarm application for Android and Thing Interaction Framework (Kii Cloud).

See author's personal [blog page](http://blog.kissy-software.com/) (Japanese) for the detail.

## How to build

* You need to setup Arduino IDE for ESP8266.
* Add ArduinoJson and PubSubClient to your Project.
* Replace the symbols below.

    ```c
    #define WIFI_SSID "<<your ssid>>"
    #define WIFI_PASSWORD "<<your ssid password>>"
    
    #define KII_APP_ID  "<<your app id>>"
    #define KII_APP_KEY "<<your app key>>"
    #define KII_MQTT_SERVER "mqtt-jp.kii.com"
    #define THING_VENDOR_ID  "1111"
    #define THING_PASSWORD   "2222"
    ```

## License

This software is distributed under [MIT License](http://opensource.org/licenses/mit-license.php).
