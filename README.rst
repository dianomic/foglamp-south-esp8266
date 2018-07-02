.. |br| raw:: html

   <br />


***********************************
FogLAMP embedded south microservice
***********************************
 
This is an example south microservice designed to run on an ESP8266 processor using a DHT11/22 temperature and humidity sensor as an example sensing device.

It connects to a FogLAMP core and storage microservice that is hosted in a separate processor using the management API's defined within FogLAMP.  The core microservice is discovered using mDNS, the name of which can be configured via the WiFi Manager that is built into this example.

The prototype was implemented using a WEMOS D1 mini processor board and a WEMOS DHT22 shield for the sensor.

Building FogLAMP South ESP8266
==============================

The FogLAMP south ESP8266 service is designed to be built using the Arduino IDE that has been customised to support the ESP8266 board by use of the board mamager.

In order to add support to the Arduino IDE, open the preferences dialog and enter into the ``Additional Board Manager URLs`` field the URL

- http://arduino.esp8266.com/stable/package_esp8266com_index.json

In the ``Tools`` - ``Board`` menu item select the ``Board Manager...`` option and look for the esp8266 boards and install these.

Additional libraries are also required and can be installed via the ``Sketch`` - ``Include Libraries...`` option and the ``Library Manager...``. The libraries that will be reqired are:

- ArduinoJson
- DHT sensor library for ESPx
- esp8266_mdns
- ESP8266mDNS
- ESP8266TrueRandom
- ESP8266WebServer
- ESP8266WiFi
- WiFiManager

Build using the Arduino IDE and upload the code to your ESP8266 device.

Configuring
===========

Configuration is acheived via the WiFiManager. With a couple of simple steps:

- Turn on your ESP device
- Use another device, phone, tablet or computer and look for a WiFi Network called ESPxxxxxx where xxxxxx is a string of digits. Connect to this WiFi network
- Once connected a popup should appear, if it does not then browse to the web address 192.168.4.1
- You will be presented with a set of options, select either ``Configure WiFi`` or ``Configure WiFi (No Scan)``. Use the later if the WiFi you wish your device to connect to is not browsable.
- Either enter your SSID or select it form the list if you choose the scan option.
- Enter the password for your WiFi network into the password field
- The third field contains the name of the FogLAMP to which you wish to connect. Set this name to the name you assigned your FogLAMP or set it to * to connect to the first available FogLAMP core service.
- The final field is the asset name to use when adding reading data to FogLAMP. Set this to the alphanumeric string that you wish your asset to appear as in your PI server.
- Click on save and disconnect from this WiFi

Your ESP8266 should now connect to FogLAMP and start to send readings. These settings will be retained in the NVRam of the ESP8266 and will not need to be reset following power failure or after a reset. Should you wish to change the setup of your ESP8266 simply reset the device and connect to its local WiFi network and repeat the above procedure.
