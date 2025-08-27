#include <ESP8266WiFi.h>
#include <WiFiClient.h>
#include <ESP8266mDNS.h>
#include <algorithm>  // std::min
#include <SoftwareSerial.h>
#include <stdlib.h>

#define VERSION "1.0"

#define SSID "ESP8266 Serial"
#define SSID_CHANNEL 5
#define SSID_HIDDEN false
#define PSK "password" // NOTE change the password please

#define RXBUFFERSIZE 1024
#define STACK_PROTECTOR 512  // bytes

const char *ssid = SSID;
const char *password = PSK;

const int port = 23;

// ip address of the device itself
IPAddress local_ip(192, 168, 1, 1);
IPAddress gateway(192, 168, 1, 1);
IPAddress subnet(255, 255, 255, 0);

WiFiServer server(port);
WiFiClient serverClient;

// software serial for debugging
EspSoftwareSerial::UART swSerial;

unsigned int setupProgress = 0;

// has the communication started (used to setup the serial connection on power-on)
bool started = false;

void setup() {
    delay(500);

    // i do not need receive pin here
    swSerial.begin(9600, EspSoftwareSerial::SWSERIAL_8N1, -1, 2, false);
    swSerial.enableRx(false);
    swSerial.enableTx(false);

    Serial.setRxBufferSize(RXBUFFERSIZE);

    swSerial.println("ESP8266 Wifi Serial Bridge");
    swSerial.println("Version: " VERSION);
    swSerial.print("IP: ");
    swSerial.println(local_ip);

    // limit to only one connection
    if (WiFi.softAP(ssid, password, SSID_CHANNEL, SSID_HIDDEN, 1) == true) {
        swSerial.printf("Started WiFi AP '%s' with password '%s'\r\n", ssid, password);
    } else {
        swSerial.println("Could not start WiFi access point, restarting..");
        ESP.restart();
    }

    WiFi.softAPConfig(local_ip, gateway, subnet);

    if (!MDNS.begin("esp8266")) {
        swSerial.println("Error setting up MDNS responder!");
        while (1) { delay(1000); }
    }

    server.begin();
    server.setNoDelay(true);

    MDNS.addService("telnet", "tcp", port);
}

void loop() {
    // accept new client only if there are none connected
    if (server.hasClient()) {
        if (!serverClient) {
            serverClient = server.accept();

            swSerial.println("New client connected");

            // ask user to set baud rate
            if (!started) {
                swSerial.println("Starting setup");

                // if not finished then reset the progress
                setupProgress = 0;
            }
        } else {
            // hints: server.accept() is a WiFiClient with short-term scope
            // when out of scope, a WiFiClient will
            // - flush() - all data will be sent
            // - stop() - automatically too
            server.accept().println("Client already connected");
            swSerial.println("Client rejected");
        }
    }

    if (!started) {
        serialSetup();
    } else {
        while (serverClient.available() && Serial.availableForWrite() > 0) {
            // working char by char is not very efficient
            Serial.write(serverClient.read());
        }

        // determine maximum output size "fair TCP use"
        // client.availableForWrite() returns 0 when !client.connected()
        int maxToTcp = 0;
        if (serverClient) {
            int afw = serverClient.availableForWrite();
            if (afw) {
                if (!maxToTcp) {
                    maxToTcp = afw;
                } else {
                    maxToTcp = std::min(maxToTcp, afw);
                }
            } else {
                // warn but ignore congested clients
                swSerial.println("Client is congested");
            }
        }

        // check UART for data
        size_t len = std::min(Serial.available(), maxToTcp);
        len = std::min(len, (size_t)STACK_PROTECTOR);
        if (len) {
            uint8_t sbuf[len];
            int serial_got = Serial.readBytes(sbuf, len);
            // push UART data to all connected telnet clients
            // if client.availableForWrite() was 0 (congested)
            // and increased since then,
            // ensure write space is sufficient:
            if (serverClient.availableForWrite() >= serial_got) {
                size_t tcp_sent = serverClient.write(sbuf, serial_got);
                if (tcp_sent != len) {
                    swSerial.printf("len mismatch: available:%zd serial-read:%zd tcp-write:%zd\r\n", len, serial_got, tcp_sent);
                }
            }
        }
    }
}

void serialSetup() {
    String response = "";
    bool hasResponded = false;
    // read input
    if (serverClient.available()) {
        hasResponded = true;
        // 10 => LineFeed
        response = serverClient.readStringUntil(10);

        swSerial.printf("Got '%s' (%d)\r\n", response.c_str(), response.length());
    }

    if (setupProgress == 0) {
        serverClient.write(
            "Baud rates:\n"
            " 4800\n"
            " 9600*\n"
            " 19200\n"
            " 28800\n"
            " 38400\n"
            " 57600\n"
            " 76800\n"
            " 115200\n"
            "\n"
            "Parity options:\n"
            " 5N1 5E1 5O1\n"
            " 6N1 6E1 6O1\n"
            " 7N1 7E1 7O1\n"
            " 5N2 5E2 5O2\n"
            " 6N2 6E2 6O2\n"
            " 7N2 7E2 7O2\n"
            " 8N2 8E2 8O2\n"
            " 8N1*\n"
            "\n"
            "Select config: "
        );

        ++setupProgress;
    } else if (setupProgress == 1) {
        if (response.isEmpty()) {
            if (hasResponded) {
                // the default config
                response = "9600-8N1";
            } else {
                return;
            }
        }

        String baudRate = "";
        size_t index = 0;
        while (response.charAt(index) >= '0' && response.charAt(index) <= '9') {
            baudRate += response.charAt(index++);
        }

        // there should be 4 more characters left  `*-8N1`
        if (response.length() - index < 4) {
            serverClient.printf("Invalid option '%s'\r\n", response.c_str());
            --setupProgress;
            return;
        }

        // skip dash
        ++index;

        long baud = atol(baudRate.c_str());
        if (baud == 0) {
            swSerial.printf("Invalid baud rate '%s'\r\n", response.c_str());
            --setupProgress;
            return;
        }

        String config = response.substring(index);
        config.toUpperCase();

        if (config == "5N1") {
            Serial.begin(baud, SERIAL_5N1);
        } else if (config == "6N1") {
            Serial.begin(baud, SERIAL_6N1);
        } else if (config == "7N1") {
            Serial.begin(baud, SERIAL_7N1);
        } else if (config == "5N2") {
            Serial.begin(baud, SERIAL_5N2);
        } else if (config == "6N2") {
            Serial.begin(baud, SERIAL_6N2);
        } else if (config == "7N2") {
            Serial.begin(baud, SERIAL_7N2);
        } else if (config == "8N2") {
            Serial.begin(baud, SERIAL_8N2);
        } else if (config == "5E1") {
            Serial.begin(baud, SERIAL_5E1);
        } else if (config == "6E1") {
            Serial.begin(baud, SERIAL_6E1);
        } else if (config == "7E1") {
            Serial.begin(baud, SERIAL_7E1);
        } else if (config == "8E1") {
            Serial.begin(baud, SERIAL_8E1);
        } else if (config == "5E2") {
            Serial.begin(baud, SERIAL_5E2);
        } else if (config == "6E2") {
            Serial.begin(baud, SERIAL_6E2);
        } else if (config == "7E2") {
            Serial.begin(baud, SERIAL_7E2);
        } else if (config == "8E2") {
            Serial.begin(baud, SERIAL_8E2);
        } else if (config == "5O1") {
            Serial.begin(baud, SERIAL_5O1);
        } else if (config == "6O1") {
            Serial.begin(baud, SERIAL_6O1);
        } else if (config == "7O1") {
            Serial.begin(baud, SERIAL_7O1);
        } else if (config == "8O1") {
            Serial.begin(baud, SERIAL_8O1);
        } else if (config == "5O2") {
            Serial.begin(baud, SERIAL_5O2);
        } else if (config == "6O2") {
            Serial.begin(baud, SERIAL_6O2);
        } else if (config == "7O2") {
            Serial.begin(baud, SERIAL_7O2);
        } else if (config == "8O2") {
            Serial.begin(baud, SERIAL_8O2);
        } else if (config == "8N1") {
            Serial.begin(baud, SERIAL_8N1);
        } else if (config == "8N2") {
            Serial.begin(baud, SERIAL_8N2);
        } else {
            serverClient.printf("Invalid config '%s'\r\n", config.c_str());
            swSerial.printf("Invalid config '%s'\r\n", config.c_str());
            --setupProgress;
            return;
        }

        swSerial.printf("Config baudrate=%ld config=%s\r\n", baud, config.c_str());
        serverClient.printf("Config: baudrate=%ld config=%s\r\n", baud, config.c_str());
        started = true;
    }
}
