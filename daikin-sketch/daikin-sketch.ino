#define DHT11_TYPE 1
#define DHT12_TYPE 2
#define DHTTYPE DHT12_TYPE

#if DHTTYPE == DHT11_TYPE
#include <DHT.h>
#else
#include <WEMOS_DHT12.h>
#endif

#include <ArduinoJson.h>
#include <IRremoteESP8266.h>
#include <IRsend.h>
#include <ir_Mitsubishi.h>

#include <EEPROM.h>
#include <ESP8266WiFi.h>
#include <WiFiClient.h>

#include <WebSocketsServer.h>

#include <ESP8266WebServer.h>
#include <ESP8266mDNS.h>

// EEPROM Storage Address Locations
#define S_MITSUBISHI_MODE           200
#define S_MITSUBISHI_FAN            210
#define S_MITSUBISHI_VANE           220
#define S_MITSUBISHI_TEMP           230

MDNSResponder mdns;
ESP8266WebServer server(80);
WebSocketsServer webSocket = WebSocketsServer(81);

/* Wemos D1 Mini */
// 14 -> D5
// 4 -> D2
IRMitsubishiAC mitsubishi(14);
#if DHTTYPE == DHT11_TYPE
DHT dht(2, DHT11);
#else
DHT12 dht12;
#endif

// Replace with your network credentials
const char* ssid = "MOVISTAR_5E61";
const char* password = "MrMjjvkl4iEdKw5pGH5A";

// Hostname
const char* accessoryName = "daikin-thermostat";

// Default Settings
class AC {
  public:
    unsigned long loopLastRun;
    float currentTemperature;
    float currentHumidity;
    String targetMode;
    String targetFanSpeed;
    String targetVane;
    int targetTemperature;

    // Saves the settings to EEPROM
    void save() {
      if (dirty) {
        Serial.println("Saving to flash");
        EEPROM.commit();
        dirty = false;
      }
    }

    // Restores Settings from EEPROM
    void restore() {
      targetMode = load(S_MITSUBISHI_MODE);
      targetFanSpeed = load(S_MITSUBISHI_FAN);
      targetVane = load(S_MITSUBISHI_VANE);

      setTargetMode(targetMode);
      setTargetFanSpeed(targetFanSpeed);
      setTargetVane(targetVane);
    }

    void handler(String payload) {
      DynamicJsonBuffer jsonBuffer;
      JsonObject& req = jsonBuffer.parseObject(payload);

      /* Get and Set Target State */
      if (req.containsKey("targetMode")) {
        setTargetMode(req["targetMode"]);
      }

      if (req.containsKey("targetModeOn")) {
        if (req["targetModeOn"]) {
          setTargetMode("cool");
        } else {
          setTargetMode("off");
        }
      }

      /* Get and Set Fan Speed */
      if (req.containsKey("targetFanSpeed")) {
        setTargetFanSpeed(req["targetFanSpeed"]);
      }

      /* Get and Set Target Temperature */
      if (req.containsKey("targetTemperature")) {
        setTemperature(req["targetTemperature"]);
      }

      /* Get and Set Target Vane */
      if (req.containsKey("targetVane")) {
        setTargetVane(req["targetVane"]);
      }

      // send the IR signal.
      mitsubishi.send();

      // broadcast update
      broadcast();

      // save settings to EEPROM
      save();
    }

    void setTargetMode(String value) {
      value.toLowerCase();

      if (value == "off") {
         mitsubishi.off();
      } else if (value == "cool") {
        mitsubishi.on();
        mitsubishi.setMode(MITSUBISHI_AC_COOL);
      } else if (value == "heat") {
        mitsubishi.on();
        mitsubishi.setMode(MITSUBISHI_AC_HEAT);
      } else if (value == "auto") {
        mitsubishi.on();
        mitsubishi.setMode(MITSUBISHI_AC_AUTO);
      } else if (value == "dry") {
        mitsubishi.on();
        mitsubishi.setMode(MITSUBISHI_AC_DRY);
      } else {
        mitsubishi.off();
        value = "off";
        Serial.println("WARNING: No Valid Mode Passed. Turning Off.");
      }

      if (value != targetMode) {
        Serial.print("Target Mode Changed: ");
        Serial.println(value);
        targetMode = value;
        set(S_MITSUBISHI_MODE, targetMode);
      }
    }

    void setTargetFanSpeed(String value) {
      value.toLowerCase();
     if (value == "auto") {
       mitsubishi.setFan(MITSUBISHI_AC_FAN_AUTO);
     } else if (value == "low") {
       mitsubishi.setFan(1U);
     } else if (value == "medium") {
       mitsubishi.setFan(2U);
     } else if (value == "high") {
       mitsubishi.setFan(3U);
     } else if (value == "superhigh") {
       mitsubishi.setFan(4U);
     } else {
       mitsubishi.setFan(4U);
       value = "auto";
       Serial.println("WARNING: No Valid Fan Speed Passed. Setting to Auto.");
     }

      if (value != targetFanSpeed) {
        Serial.print("Target Fan Speed: ");
        Serial.println(value);
        targetFanSpeed = value;
        set(S_MITSUBISHI_FAN, targetFanSpeed);
      }
    }

    void setTargetVane(String value) {
      value.toLowerCase();
     if (value == "auto") {
       mitsubishi.setVane(MITSUBISHI_AC_VANE_AUTO);
     } else if (value == "automove") {
       mitsubishi.setVane(MITSUBISHI_AC_VANE_AUTO_MOVE);
      } else if (value == "1") {
       mitsubishi.setVane(1U);
            } else if (value == "2") {
       mitsubishi.setVane(2U);
            } else if (value == "3") {
       mitsubishi.setVane(3U);
            } else if (value == "4") {
       mitsubishi.setVane(4U);
            } else if (value == "5") {
       mitsubishi.setVane(5U);
            } else if (value == "6") {
       mitsubishi.setVane(6U);
     } else {
       mitsubishi.setVane(MITSUBISHI_AC_VANE_AUTO);
       value = "auto";
       Serial.println("WARNING: No Valid Vane Passed. Setting to Auto.");
     }

      if (value != targetVane) {
        Serial.print("Target Vane: ");
        Serial.println(value);
        targetVane = value;
        set(S_MITSUBISHI_VANE, targetVane);
      }
    }

    void setTemperature(int value) {
      Serial.print("Target Temperature: ");
      Serial.println(value);
      mitsubishi.setTemp(value);
      targetTemperature = value;
    }

    float getCurrentTemperature() {
      readDHT();
      return currentTemperature;
    }

    float getCurrentHumidity() {
      readDHT();
      return currentHumidity;
    }

    String toJson() {
      DynamicJsonBuffer jsonBuffer;
      JsonObject& root = jsonBuffer.createObject();

      root["currentTemperature"] = getCurrentTemperature();
      root["currentHumidity"] = getCurrentHumidity();
      root["targetMode"] = targetMode;
      root["targetFanSpeed"] = targetFanSpeed;
      root["targetTemperature"] = targetTemperature;
      root["targetVane"] = targetVane;
      String res;
      root.printTo(res);

      return res;
    }

    void broadcast() {
      String res = toJson();
      webSocket.broadcastTXT(res);
    }

    void loop () {
      unsigned long currentMillis = millis();

      if (currentMillis - loopLastRun >= 30000) {
        loopLastRun = currentMillis;
        broadcast();
      }
    }

  private:
    bool dirty;
    unsigned long dhtLastRead;
    float humidity;
    float temp;

    // Saves a setting value to EEPROM
    void set(int location, String value) {
      dirty = true;
      value += "|";
      for (int i = 0; i < value.length(); ++i) {
        EEPROM.write(i + location, value[i]);
      }
    }

    // Loads a setting value from EEPROM
    String load(int location) {
      String value;

      for (int i = 0; i < 10; ++i) {
        value += char(EEPROM.read(i + location));
      }

      int stopAt = value.indexOf("|");
      value = value.substring(0, stopAt);
      return value;
    }

    void readDHT() {
      unsigned long currentMillis = millis();

      if (currentMillis - dhtLastRead >= 5000) {
        dhtLastRead = currentMillis;
#if DHTTYPE == DHT11_TYPE
        humidity = dht.readHumidity();
        temp = dht.readTemperature(false);
#else
        if(dht12.get()==0){
          humidity = dht12.humidity;
          temp = dht12.cTemp;
        } else {
          Serial.println("Failed to read from DHT sensor!");
          return;
        }
#endif

        if (isnan(humidity) || isnan(temp)) {
          Serial.println("Failed to read from DHT sensor!");
          return;
        } else {
          currentTemperature = temp;
          currentHumidity = humidity;
        }
      }
    }
} ac;

// CORS Handler
void sendCors() {
  if (server.hasHeader("origin")) {
      String originValue = server.header("origin");
      server.sendHeader("Access-Control-Allow-Origin", originValue);
      server.sendHeader("Access-Control-Allow-Headers", "Origin, X-Requested-With, Content-Type, Accept, Authorization");
      server.sendHeader("Access-Control-Allow-Methods", "GET,POST,OPTIONS");
      server.sendHeader("Access-Control-Max-Age", "600");
      server.sendHeader("Vary", "Origin");
  }
}

void webSocketEvent(uint8_t num, WStype_t type, uint8_t * payload, size_t length) {
  switch(type) {
    case WStype_DISCONNECTED:
      Serial.printf("[%u] Disconnected!\r\n", num);
      break;
    case WStype_CONNECTED: {
      Serial.printf("[%u] Connected from url: %s\r\n", num, payload);
      // broadcast current settings
      ac.broadcast();
      break;
    }
    case WStype_TEXT: {
      // send the payload to the ac handler
      ac.handler((char *)&payload[0]);
      break;
    }
    case WStype_BIN:
      Serial.printf("[%u] get binary length: %u\r\n", num, length);
      break;
    default:
      Serial.printf("Invalid WStype [%d]\r\n", type);
      break;
  }
}

void setup(void) {
//  delay(1000);

  Serial.begin(115200, SERIAL_8N1, SERIAL_TX_ONLY);

  WiFi.mode(WIFI_STA);
  WiFi.hostname(accessoryName);

  WiFi.begin(ssid, password);

  Serial.println("");

  // Wait for connection
  while (WiFi.status() != WL_CONNECTED) {
    delay(500);
    Serial.print(".");
  }
  Serial.println("");
  Serial.print("Connected to ");
  Serial.println(ssid);
  Serial.print("IP address: ");
  Serial.println(WiFi.localIP());

  if (mdns.begin(accessoryName, WiFi.localIP())) {
    Serial.println("MDNS responder started");
  }

  // Default Settings
  ac.currentTemperature = 0;
  ac.currentHumidity = 0;
  ac.targetMode = "off";
  ac.targetFanSpeed = "low";
  ac.targetTemperature = 24;
  ac.targetVane = "1";
  

  server.on("/daikin", HTTP_OPTIONS, []() {
    sendCors();
    server.send(200, "text/html", "ok");
  });

  server.on("/daikin", HTTP_GET, []() {
    sendCors();
    server.send(200, "application/json", ac.toJson());
  });

  server.on("/daikin", HTTP_POST, []() {
    // send the body to the ac handler
    ac.handler(server.arg("plain"));

    sendCors();
    server.send(200, "application/json", "{\"status\": \"0\"}");
  });

  server.on("/restart", HTTP_GET, []() {
    server.send(202);
    ESP.restart();
  });

  // list of headers to be recorded
  const char * headerkeys[] = {"origin"};
  size_t headerkeyssize = sizeof(headerkeys)/sizeof(char*);

  // ask server to track these headers
  server.collectHeaders(headerkeys, headerkeyssize);

  server.begin();
  Serial.println("HTTP Server Started");

  webSocket.begin();
  webSocket.onEvent(webSocketEvent);
  Serial.println("WebSocket Server Started");

  mitsubishi.begin();
  Serial.println("IR Send Ready");

  #if DHTTYPE == DHT11_TYPE
  dht.begin();
  Serial.println("DHT Ready");
  #endif

  // Add service to mdns-sd
  mdns.addService("oznu-platform", "tcp", 81);
  mdns.addServiceTxt("oznu-platform", "tcp", "type", "daikin-thermostat");
  mdns.addServiceTxt("oznu-platform", "tcp", "mac", WiFi.macAddress());

  // Restore previous settings
  EEPROM.begin(512);
  ac.restore();
}

void loop(void) {
  server.handleClient();
  webSocket.loop();
  ac.loop();
}
