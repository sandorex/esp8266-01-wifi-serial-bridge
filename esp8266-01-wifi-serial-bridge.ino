#include <ESP8266WiFi.h>
#include <WiFiClient.h>
#include <ESP8266mDNS.h>
#include <algorithm>  // std::min
#include <SoftwareSerial.h>
#include <stdlib.h>

#define VERSION "1.0"

#define SSID "ESP8266 WiFi Serial"
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

long baudRate = 0;

// has the communication started (used to setup the serial connection on power-on)
bool started = false;

void setup() {
    delay(500);
    swSerial.begin(9600, EspSoftwareSerial::SWSERIAL_8N1, -1, 2, false);
    swSerial.enableRx(false);
    swSerial.enableTx(false);

    Serial.begin(9600);
    Serial.setRxBufferSize(RXBUFFERSIZE);

    swSerial.println("ESP8266 Wifi Telnet Serial Bridge");
    swSerial.println("Version: " VERSION);

    // limit to only one connection
    if (WiFi.softAP(ssid, password, SSID_CHANNEL, SSID_HIDDEN, 1) == true) {
        swSerial.printf("Started WiFi AP '%s' with password '%s'\n", ssid, password);
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
                
                // TODO reset options if not fully setup
            }
        } else {
            // hints: server.accept() is a WiFiClient with short-term scope
            // when out of scope, a WiFiClient will
            // - flush() - all data will be sent
            // - stop() - automatically too
            server.accept().println("busy");
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
                    swSerial.printf("len mismatch: available:%zd serial-read:%zd tcp-write:%zd\n", len, serial_got, tcp_sent);
                }
            }
        }
    }
}

void serialSetup() {
    String response = "";
    // read input
    if (serverClient.available()) {
        // 10 => LineFeed
        response = serverClient.readStringUntil(10);

        swSerial.printf("Got '%s'\n", response.c_str());
    }

    if (baudRate == 0) {
        // serverClient.write(
        //     "Baud rates:\n"
        //     " 4800\n"
        //     " 9600*\n"
        //     " 19200\n"
        //     " 28800\n"
        //     " 38400\n"
        //     " 57600\n"
        //     " 76800\n"
        //     " 115200\n"
        //     "\n"
        // );

        if (response.isEmpty()) {
            return;
        }

        long baud = atol(response.c_str());
        if (baud == 0) {
            swSerial.printf("Invalid baud rate '%s'", response.c_str());
            return;
        }

        baudRate = baud;
        swSerial.printf("Setting baud rate to %l", baudRate);
    } else {
        
    }
    
    // switch (setupProgress) {
    //     case 1:
    //         serverClient.write(
    //             "Baud rates:\n"
    //             " 4800\n"
    //             " 9600*\n"
    //             " 19200\n"
    //             " 28800\n"
    //             " 38400\n"
    //             " 57600\n"
    //             " 76800\n"
    //             " 115200\n"
    //             "\n"
    //         );

    //         ++setupProgress;
    //         break;
    //     case 2:
    //         switch (response.c_str()) {
    //             case "":
    //                 break;
    //         }

            // String baudRate = "";
            // String config = "";
            // if (response.charAt(0) == (char)10) {
            //     baudRate = "9600";
            //     config = "8N1";
            // } else {
            //     size_t index = 0;
            //     while (response.charAt(index) >= '0' && response.charAt(index) <= '9') {
            //         baudRate += response.charAt(index);
            //     }

            //     // there should be 4 more characters left  `*-8N1`
            //     if (response.length() - index < 4) {
            //         serverClient.write("Invalid answer\n");
            //         --setupProgress;
            //         break;
            //     }

            //     // skip dash
            //     ++index;

            //     // if (response.length() <= index) {
            //     //     serverClient.write("Invalid answer, missing config");
            //     //     --setupProgress;
            //     //     break;
            //     // }

            //     config = response.substring(index);
            // }

            // long baudRateLong = baudRate.toInt();

            // swSerial.printf("Setting baudrate '%d', config '%s'\n", baudRateLong, config.c_str());

    //         ++setupProgress;
    //         break;
    //     case 3:
    //         serverClient.write(
    //             "Baud rates:\n"
    //             " 4800 9600\n"
    //             " 19200 28800\n"
    //             " 38400 57600\n"
    //             " 76800 115200\n"
    //             "\n"
    //             "Configs:\n"
    //             " 5N1 5E1 5O1\n"
    //             " 6N1 6E1 6O1\n"
    //             " 7N1 7E1 7O1\n"
    //             " 5N2 5E2 5O2\n"
    //             " 6N2 6E2 6O2\n"
    //             " 7N2 7E2 7O2\n"
    //             " 8N2 8E2 8O2\n"
    //             " 8N1\n"
    //             "\n"
    //             "Default: 9600-8N1\n"
    //         );

    //         ++setupProgress;
    //         break;
    //     case 4:
    //         break;
    // }

    // TODO ask questions repeadatly
    // while (true) {
        // serverClient.write("Common values:\n");
        // serverClient.write("1) 4800\n");
        // serverClient.write("2) 9600 (default)\n");
        // serverClient.write("3) 19200\n");
        // serverClient.write("4) 28800\n");
        // serverClient.write("5) 38400\n");
        // serverClient.write("6) 57600\n");
        // serverClient.write("7) 76800\n");
        // serverClient.write("8) 115200\n\n");
        // serverClient.write("Select baud rate (enter for default): ");

        // long baud = serverClient.parseInt();

        // swSerial.printf("Setting baud rate to %l", baud);

        // serverClient.readStringUntil('\r');

        // switch (ans) {
        //     case '1':
        //         break;
        //     case '2':
        //         break;
        //     case '3':
        //         break;
        //     case '4':
        //         break;
        //     case '5':
        //         break;
        //     case '6':
        //         break;
        //     case '7':
        //         break;
        //     case '8':
        //         break;
        //     case '':
        //         break;
        //     default:
        //         break;
        // }
    // }

    // switch (config) {
    //     // no parity
    //     case "5N1":
    //         Serial.begin(baudRate, SERIAL_5N1);
    //         break;
    //     case "6N1":
    //         Serial.begin(baudRate, SERIAL_6N1);
    //         break;
    //     case "7N1":
    //         Serial.begin(baudRate, SERIAL_7N1);
    //         break;
    //     case "5N2":
    //         Serial.begin(baudRate, SERIAL_5N2);
    //         break;
    //     case "6N2":
    //         Serial.begin(baudRate, SERIAL_6N2);
    //         break;
    //     case "7N2":
    //         Serial.begin(baudRate, SERIAL_7N2);
    //         break;
    //     case "8N2":
    //         Serial.begin(baudRate, SERIAL_8N2);
    //         break;

    //     // even parity
    //     case "5E1":
    //         Serial.begin(baudRate, SERIAL_5E1);
    //         break;
    //     case "6E1":
    //         Serial.begin(baudRate, SERIAL_6E1);
    //         break;
    //     case "7E1":
    //         Serial.begin(baudRate, SERIAL_7E1);
    //         break;
    //     case "8E1":
    //         Serial.begin(baudRate, SERIAL_8E1);
    //         break;
    //     case "5E2":
    //         Serial.begin(baudRate, SERIAL_5E2);
    //         break;
    //     case "6E2":
    //         Serial.begin(baudRate, SERIAL_6E2);
    //         break;
    //     case "7E2":
    //         Serial.begin(baudRate, SERIAL_7E2);
    //         break;
    //     case "8E2":
    //         Serial.begin(baudRate, SERIAL_8E2);
    //         break;

    //     // odd parity
    //     case "5O1":
    //         Serial.begin(baudRate, SERIAL_5O1);
    //         break;
    //     case "6O1":
    //         Serial.begin(baudRate, SERIAL_6O1);
    //         break;
    //     case "7O1":
    //         Serial.begin(baudRate, SERIAL_7O1);
    //         break;
    //     case "8O1":
    //         Serial.begin(baudRate, SERIAL_8O1);
    //         break;
    //     case "5O2":
    //         Serial.begin(baudRate, SERIAL_5O2);
    //         break;
    //     case "6O2":
    //         Serial.begin(baudRate, SERIAL_6O2);
    //         break;
    //     case "7O2":
    //         Serial.begin(baudRate, SERIAL_7O2);
    //         break;
    //     case "8O2":
    //         Serial.begin(baudRate, SERIAL_8O2);
    //         break;

    //     // 8N1 is the default
    //     case "":
    //         Serial.begin(baudRate, SERIAL_8N1);
    //         break;
    
    //     default: // TODO
    //         break;
    // }

    // String s = serverClient.readStringUntil('\n');
    // serverClient.write(s.c_str());
    // Serial.updateBaudRate(9600);
}
