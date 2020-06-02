/*
   FogLAMP embedded south microservice

   This is an example south microservice designed to run on an ESP8266
   processor using a DHT11/22 temperature and humidity sensor as an example
   sensing device.

   It connects to a FogLAMP core and storage microservice that is hosted
   in a separate processor using the management API's defined within FogLAMP.
   The core microservice is discovered using mDNS, the name of which can be
   configured via the WiFi Manager that is built into this example.

   The prototype was implemented using a WEMOS D1 mini processor board and
   a WEMOS DHT22 shield for the sensor.

   Copyright (c) 2018 Dianomic Systems

   Released under the Apache 2.0 Licence

   Author: Mark Riddoch
*/

#include <ESP8266WebServer.h>

#include <ArduinoJson.h>

#include <ESP8266mDNS.h>
#include <FS.h>
#include <ESP8266WiFi.h>
//#include <ESP8266TrueRandom.h>
#include <mdns.h>
#include <DNSServer.h>            //Local DNS Server used for redirecting all requests to the configuration portal
#include <ESP8266WebServer.h>     //Local WebServer used to serve the configuration portal
#include <WiFiManager.h>
#include "DHTesp.h"

#define DHTPIN 2       // The digital pin to which the DHT22 is connected
#define DHTTYPE DHTesp::DHT22   // there are multiple kinds of DHT sensors

DHTesp dht;
const String host = "foglamp.local";
const String service = "foglamp-manage";

char coreName[40];  // Name of the core to connect to or NULL for any core
char hostAddress[20];
short managementPort;

char assetCode[40];

#define DEBUG_DISCOVERY 0
#define DEBUG_STATE 0
#define DEBUG_MANAGEMENT 0

/**
   The states of the south service

   Thw service is implemented as a simple state machine
*/
enum {
  WaitingForWiFi,
  WaitingForCoreAddress,
  WaitingForStorage,
  ConsumingReadings
} state = WaitingForWiFi;


#define MANAGEMENT_PORT   1080  // Management port for this service

//flag for saving data
bool shouldSaveConfig = false;

//callback notifying us of the need to save config
void saveConfigCallback () {
  shouldSaveConfig = true;
}

/*
   Initial setup routine.

   We store the parameters in a file in the NVRAM filesytem called config.json
   These parameters give us the SSID and password of the WiFi to connect to,
   the name of the foglamp server which will be our core server (or * for any)
   and the asset code we will use to report our readings.
*/
void setup() {
  Serial.begin(115200);

  delay(10);

  if (SPIFFS.begin()) {
    if (SPIFFS.exists("/config.json")) {
      //file exists, reading and loading
      File configFile = SPIFFS.open("/config.json", "r");
      if (configFile) {
        size_t size = configFile.size();
        // Allocate a buffer to store contents of the file.
        std::unique_ptr<char[]> buf(new char[size]);

        configFile.readBytes(buf.get(), size);
        DynamicJsonBuffer jsonBuffer;
        JsonObject& json = jsonBuffer.parseObject(buf.get());
        json.printTo(Serial);
        if (json.success()) {

          strcpy(coreName, json["foglamp_server"]);
          strcpy(assetCode, json["asset_code"]);

        } else {
          Serial.println("failed to load json config");
        }
      }
    }
  } else {
    Serial.println("failed to mount FS");
  }
  WiFiManagerParameter custom_foglamp_server("foglamp", "*", coreName, 40);
  WiFiManagerParameter custom_asset_code("asset", "ESP_DHT22", assetCode, 40);
  // We start by connecting to a WiFi network
  WiFiManager wifiManager;

  //set config save notify callback
  wifiManager.setSaveConfigCallback(saveConfigCallback);

  //add all your parameters here
  wifiManager.addParameter(&custom_foglamp_server);
  wifiManager.addParameter(&custom_asset_code);

  //wifiManager.resetSettings(); // Uncomment to invalid settings for test purposes/

  wifiManager.autoConnect();

  //read updated parameters
  strcpy(coreName, custom_foglamp_server.getValue());
  strcpy(assetCode, custom_asset_code.getValue());

  //save the custom parameters to FS
  if (shouldSaveConfig) {
    DynamicJsonBuffer jsonBuffer;
    JsonObject& json = jsonBuffer.createObject();
    json["foglamp_server"] = coreName;
    json["asset_code"] = assetCode;

    File configFile = SPIFFS.open("/config.json", "w");
    if (!configFile) {
      Serial.println("failed to open config file for writing");
    }

    json.printTo(Serial);
    json.printTo(configFile);
    configFile.close();
    //end save
  }
  // Start mDNS and announce ourself
  if (!MDNS.begin("foglamp-south-ESP8266")) {
    Serial.println("Error setting up MDNS responder!");
  }

#if DEBUG_STATE
  Serial.println("Waiting for core address");
#endif
  state = WaitingForCoreAddress;

  dht.setup(DHTPIN, DHTTYPE);
}

// use mDNS to discover the FogLAMP core to which we want to connect
bool findManagementAPI()
{
  int n = MDNS.queryService("foglamp-manage", "tcp"); // Send out query for esp tcp services
#if DEBUG_DISCOVERY
  Serial.println("mDNS query done");
  if (n == 0) {
    Serial.println("no services found");
  }
  else {
    Serial.print(n);
    Serial.println(" service(s) found");
    for (int i = 0; i < n; ++i) {
      // Print details for each service found
      Serial.print(i + 1);
      Serial.print(": ");
      Serial.print(MDNS.hostname(i));
      Serial.print(" (");
      Serial.print(MDNS.IP(i));
      Serial.print(":");
      Serial.print(MDNS.port(i));
      Serial.println(")");
    }
  }
  Serial.println();
#endif

  for (int i = 0; i < n; ++i) {
    if (MDNS.port(i) == 0)
    {
      continue;
    }
    if (*coreName == 0 || strcmp(coreName, "*") == 0 || strcmp(coreName, MDNS.hostname(i).c_str()) == 0)
    {
      strcpy(hostAddress, MDNS.IP(i).toString().c_str());
      managementPort = MDNS.port(i);
#if DEBUG_DISCOVERY
      Serial.printf("Using mamnagement service %s:%d\r\n", hostAddress, managementPort);
#endif
      return true;
    }
  }
  Serial.printf("Failed to get management service matching '%s'.\r\n", coreName);
  return false;
}

/*
   Called by the main loop routine to gather data from the sensor, in this
   case a DHT11/22.
*/
bool DHTLoop(String & payload)
{
  // Reading temperature or humidity takes about 250 milliseconds!
  // Sensor readings may also be up to 2 seconds 'old' (its a very slow sensor)
  float h = dht.getHumidity();
  // Read temperature as Celsius (the default)
  float t = dht.getTemperature();

  // Check if any reads failed and exit early (to try again).
  if (isnan(h) || isnan(t)) {
    return false;
  }

  // Compute heat index in Celsius (isFahreheit = false)
  float hic = dht.computeHeatIndex(t, h, false);

  payload = String("{ \"humidity\" : ") + h +
            ", \"temperature\" : " + t +
            ", \"heat_index\" : " + hic + " }";
  return true;
}


// Generic HTTP Client class from which we derive other clients, e.g. Storage and Management
class HTTPClient
{
  public:
    HTTPClient(const String& host, const int port)
    {
      this->host = host;
      this->port = port;
      this->connected = false;
    };

    bool SendRequest(const String& method, const String& url)
    {
      if ((!connected) && (!connect()))
      {
        Serial.println("connect failed");
        return false;
      }
      client.print(method + " " + url + " HTTP/1.1\r\n" +
                   "Host: " + host + "\r\n" +
                   "Connection: close\r\n\r\n");
      header = true;
      return true;
    };

    bool SendRequest(const String& method, const String& url, const String& payload)
    {
      if ((!connected) && (!connect()))
      {
        return false;
      }
      client.print(method + " " + url.c_str() + " HTTP/1.1\r\n" +
                   "Host: " + host.c_str() + "\r\n" +
                   "Content-Length: " + payload.length() + "\r\n" +
                   "Content-Type: application/json" + "\r\n" +
                   "Connection: close\r\n\r\n" + payload.c_str() + "\r\n");
      header = true;
      return true;
    };

    bool GetResponse(String& payload)
    {
      if (!connected)
      {
        return false;
      }
      while (client.available()) {
        String data = client.readStringUntil('\r');
        data.trim();
        data.remove(data.length());
        if (data.length() == 0)
        {
          header = false;
          continue;
        }
        if (header)
          continue;
        payload += data;
      }
      connected = false;
      if (payload.length() > 0)
      {
        return true;
      }
      return false;
    }

  protected:
    bool        connected;

  private:
    bool connect()
    {
      if (!client.connect(host.c_str(), port)) {
        Serial.println(String("Connection to ") + host.c_str() + ":" + port + " failed");
        return false;
      }
      connected = true;
      return true;
    }

    WiFiClient  client;
    String      host;
    int         port;
    bool        header;
};

// Management API client
class ManagementClient : public HTTPClient
{
  public:
    ManagementClient(const String& host, const int port) : HTTPClient(host, port)
    {
      storagePort = 0;
    };

    // Find a FogLAMP mnicroservice given the tyoe of the microservice
    bool LookupServiceByType(const String& type)
    {
      if (!SendRequest(get, String("/foglamp/service?type=") + type))
      {
        return false;
      }
      delay(100);
      String payload;
      while (connected && GetResponse(payload) == false)
        ;

      StaticJsonBuffer<400> JSONBuffer;
      JSONBuffer.clear();
      JsonObject&  parsed = JSONBuffer.parseObject(payload.c_str());
      if (!parsed.success())
      {
        return false;
      }
      JsonArray& services = parsed["services"];

      storagePort = services[0]["service_port"];
      Serial.printf("Storage port %d\r\n", storagePort);
      return true;
    };

    // Register this microservice
    bool RegisterService(const String& name, const String& type, uint16_t port)
    {
      StaticJsonBuffer<400> JSONBuffer;

      JsonObject& root = JSONBuffer.createObject();
      root["name"] = name.c_str();
      root["type"] = type.c_str();
      root["management_port"] = port;
      root["address"] = WiFi.localIP().toString();
      root["protocol"] = "http";

      String payload;
      root.printTo(payload);
      if (!SendRequest(post, String("/foglamp/service"), payload))
      {
        Serial.println("Failed to register service");
        return false;
      }
      delay(100);
      String response;
      while (connected && GetResponse(response) == false)
        ;

      JSONBuffer.clear();
      JsonObject&  parsed = JSONBuffer.parseObject(response.c_str());
      if (!parsed.success())
      {
        Serial.print("Failed to parse response for registration message: ");
        Serial.println(response.c_str());
        return false;
      }
      m_uuid = parsed["id"].as<String>();
      return true;
    };

    int getStoragePort()
    {
      return storagePort;
    };

    void AddTrack(const String& service, const String& asset)
    {

      StaticJsonBuffer<400> JSONBuffer;

      JsonObject& root = JSONBuffer.createObject();
      root["asset"] = asset.c_str();
      root["plugin"] = service.c_str();
      root["service"] = service.c_str();
      root["event"] = "Ingest";

      String payload;
      root.printTo(payload);
      if (!SendRequest(post, String("/foglamp/track"), payload))
      {
        Serial.println("Failed to track asset");
        return;
      }
      delay(100);
      String response;
      while (connected && GetResponse(response) == false)
        ;
      Serial.print("Add asset tracking data: ");
      Serial.println(response.c_str());
    }

  private:
    const String  get = String("GET");
    const String  post = String("POST");
    int           storagePort;
    String        m_uuid;
};

// Simple storage client
class StorageClient : public HTTPClient
{
  public:
    StorageClient(const String& host, const int port) : HTTPClient(host, port)
    {
    };

    bool SendReading(String& payload)
    {
      byte uuidNumber[16]; // UUIDs in binary form are 16 bytes long

      //String uuidStr = ESP8266TrueRandom.uuidToString(uuidNumber);

      String fullPayload = String(" { \"readings\" : [ { \"asset_code\" : \"") +
                           assetCode +
                           "\", \"reading\" : " + payload.c_str() + ", \"user_ts\" : \"now()\" } ] }";
      if (!SendRequest(post, String("/storage/reading"), fullPayload))
      {
        return false;
      }
      String response;
      while (connected && GetResponse(response) == false)
        ;
      StaticJsonBuffer<400> JSONBuffer;
      JSONBuffer.clear();
      JsonObject&  parsed = JSONBuffer.parseObject(payload.c_str());
      if (!parsed.success())
      {
        return false;
      }
      return true;
    };

  private:
    const String  get = String("GET");
    const String  post = String("POST");
};

// Management API server
class ManagementServer {

  public:
    ManagementServer()
    {
      port = MANAGEMENT_PORT;
      webServer = new ESP8266WebServer(port);

      webServer->on("/foglamp/service/ping", mgmtPing);
      webServer->onNotFound(mgmtHandleNotFound);

      webServer->begin();
    }

    void loop()
    {
      webServer->handleClient();
    }

    int getPort()
    {
      return this->port;
    }

    // Handle a ping request
    void ping()
    {
      webServer->send(200, "application/json", String("{ \"uptime\" : ") + millis() / 1000 + String(" }"));
    }

    void notFound()
    {
      String message = "File Not Found\n\n";
      message += "URI: ";
      message += webServer->uri();
      message += "\nMethod: ";
      message += (webServer->method() == HTTP_GET) ? "GET" : "POST";
      message += "\nArguments: ";
      message += webServer->args();
      message += "\n";
      for (uint8_t i = 0; i < webServer->args(); i++) {
        message += " " + webServer->argName(i) + ": " + webServer->arg(i) + "\n";
      }
      webServer->send(404, "text/plain", message);
#if DEBUG_MANAGEMENT
      Serial.println(message);
#endif
    }

  private:
    int port;
    ESP8266WebServer *webServer;
};


ManagementClient *managementClient;
ManagementServer *managementServer;
StorageClient *storage;

void mgmtPing()
{
  managementServer->ping();
}

void mgmtHandleNotFound()
{
  managementServer->notFound();
}

const String storageType = String("Storage");

unsigned long lastReading = 0;

void loop()
{
  int storagePort = 0;

  switch (state)
  {
    case WaitingForCoreAddress:
      if (! findManagementAPI())
      {
        delay(1000);
      }
      else
      {
        managementClient = new ManagementClient(hostAddress, managementPort);
        state = WaitingForStorage;
#if DEBUG_STATE
        Serial.println("Waiting for storage");
#endif
      }
      break;
    case WaitingForStorage:
      if (managementClient->LookupServiceByType(storageType) == false)
      {
        delay(5000);
        return;
      }
      storagePort = managementClient->getStoragePort();
      if (storagePort)
      {
        storage = new StorageClient(hostAddress, storagePort);
        managementServer = new ManagementServer();
        managementClient->RegisterService(String("ESP8266-South"), String("Southbound"), managementServer->getPort());
        state = ConsumingReadings;
#if DEBUG_STATE
        Serial.println("Consuming readings");
#endif
        managementClient->AddTrack(String("ESP8266-South"), String(assetCode));
      }
      break;
    case ConsumingReadings:
      managementServer->loop();
      if (millis() > lastReading + dht.getMinimumSamplingPeriod())
      {
        String reading;
        if (DHTLoop(reading))
        {
          managementServer->loop();
          storage->SendReading(reading);
          managementServer->loop();
        }
        lastReading = millis();
      }
      break;
  }
}
