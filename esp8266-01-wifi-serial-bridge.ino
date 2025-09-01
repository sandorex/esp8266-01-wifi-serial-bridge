#include <ESP8266WiFi.h>
#include <WiFiClient.h>
#include <algorithm>  // std::min
#include <SoftwareSerial.h>
#include <stdlib.h>
#include <EEPROM.h>

// NOTE this has to be size of one byte!
#define VERSION 3

#define PORT_SETUP 20
#define PORT_SERIAL 23

#define RXBUFFERSIZE 1024
#define STACK_PROTECTOR 512  // bytes

typedef struct {
    uint8_t channel = 7;
    char ssid[32] = "ESP8266 Serial";
    char password[16] = "password";
    char parity[4] = "8N1";
    bool hidden = false;
    uint32_t baud = 9600;
} settings_t;

settings_t settings;

// ip address of the device itself
IPAddress local_ip(192, 168, 1, 1);
IPAddress gateway(192, 168, 1, 1);
IPAddress subnet(255, 255, 255, 0);

// socket to configure the device
WiFiServer server_setup(PORT_SETUP);

// socket to talk serial
WiFiServer server_serial(PORT_SERIAL);

// only one client can be connected
WiFiClient client;

// is the client in setup server or serial
bool is_setup = false;

// software serial for debugging
EspSoftwareSerial::UART swSerial;

typedef enum {
    MENU_START_PRINT,
    MENU_START,
    MENU_RESET,

    MENU_BAUD,
    MENU_PARITY,

    MENU_WIFI_PRINT,
    MENU_WIFI,
    MENU_SSID,
    MENU_PASSWORD,
    MENU_CHANNEL,
    MENU_HIDDEN
} MenuIndex;

MenuIndex menu_index = MENU_START_PRINT;

void setup() {
    delay(500);

    EEPROM.begin(512);

    // only load the eeprom data if same version as the current program
    uint8_t version;
    EEPROM.get(0, version);
    if (version == VERSION) {
        EEPROM.get(1, settings);
    }

    // TODO use from eeprom
    Serial.begin(9600);
    Serial.setRxBufferSize(RXBUFFERSIZE);

    // i do not need receive pin here
    swSerial.begin(9600, EspSoftwareSerial::SWSERIAL_8N1, -1, 2, false);
    swSerial.enableRx(false);
    swSerial.enableTx(false);

    swSerial.println("ESP8266 Wifi Serial Bridge");
    swSerial.printf("Version: %d", VERSION);

    swSerial.print("Setup: ");
    swSerial.print(local_ip);
    swSerial.print(":");
    swSerial.print(PORT_SETUP);

    swSerial.print("Serial: ");
    swSerial.print(local_ip);
    swSerial.print(":");
    swSerial.print(PORT_SERIAL);

    // limit to only one connection
    if (WiFi.softAP(settings.ssid, settings.password, settings.channel, settings.hidden, 1) == true) {
        swSerial.printf("Started WiFi AP '%s' with password '%s'\r\n", settings.ssid, settings.password);
    } else {
        swSerial.println("Could not start WiFi access point, restarting..");
        ESP.restart();
    }

    WiFi.softAPConfig(local_ip, gateway, subnet);

    server_serial.begin();
    server_serial.setNoDelay(true);

    server_setup.begin();
    server_setup.setNoDelay(true);
}

void loop() {
    // each new client just drops the old one
    if (server_setup.hasClient()) {
        if (client) {
            client.stop();
        }

        client = server_setup.accept();
        is_setup = true;

        client.println("ESP8266 Wifi Serial Bridge Setup");
        swSerial.println("New setup client connected");

        menu_index = MENU_START;
        do_menu();
    }

    if (server_serial.hasClient()) {
        if (client) {
            client.stop();
        }

        client = server_serial.accept();
        is_setup = false;

        client.println("ESP8266 Wifi Serial Bridge");
        swSerial.println("New serial client connected");
    }

    if (client.available()) {
        if (is_setup) {
            do_menu();
        } else {
            while (client.available() && Serial.availableForWrite() > 0) {
                // working char by char is not very efficient
                Serial.write(client.read());
            }

            // determine maximum output size "fair TCP use"
            // client.availableForWrite() returns 0 when !client.connected()
            int maxToTcp = 0;
            if (client) {
                int afw = client.availableForWrite();
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
                if (client.availableForWrite() >= serial_got) {
                    size_t tcp_sent = client.write(sbuf, serial_got);
                    if (tcp_sent != len) {
                        swSerial.printf("len mismatch: available:%zd serial-read:%zd tcp-write:%zd\r\n", len, serial_got, tcp_sent);
                    }
                }
            }
        }
    }
}

void do_menu() {
    String response = "";
    bool hasResponded = false;

    // read input
    if (client.available()) {
        hasResponded = true;
        // 10 => LineFeed
        response = client.readStringUntil(10);

        client.printf("\r\n");

        // swSerial.printf("Got '%s' (%d)\r\n", response.c_str(), response.length());
    }

    switch (menu_index) {
        case MENU_START_PRINT:
            client.printf("w) WiFi AP options\r\n");
            client.printf("b) Set baud rate (%d)\r\n", settings.baud);
            client.printf("p) Parity (%s)\r\n\n", settings.parity);

            client.printf("s) Save to EEPROM\r\n");
            client.printf("r) Reset to defaults\r\n\n");
            client.printf("Choose an option: ");

            menu_index = MENU_START;
            break;
        case MENU_START:
            // wait for response
            if (!hasResponded) {
                return;
            }

            do_menu_start(response);
            break;
        case MENU_WIFI_PRINT:
            client.printf("s) Set SSID ('%s')\r\n", settings.ssid);
            client.printf("p) Set password ('%s')\r\n", settings.password);
            client.printf("c) Set channel (%d)\r\n", settings.channel);
            client.printf("h) Set hidden (%s)\r\n\n", settings.hidden ? "true" : "false");

            client.printf("b) Go back\r\n\n");

            client.printf("Choose an option: ");

            menu_index = MENU_WIFI;
            break;
        case MENU_WIFI:
            // wait for response
            if (!hasResponded) {
                return;
            }

            do_menu_wifi(response);
            break;
        case MENU_RESET:
            // wait for response
            if (!hasResponded) {
                return;
            }

            if (response == "y" || response == "Y") {
                client.printf("Resetting to default values\r\n");

                settings_t defaults;
                EEPROM.put(0, VERSION);
                EEPROM.put(1, defaults);

                client.printf("Restarting\r\n");
                ESP.restart();
            } else {
                client.printf("Cancelled\r\n");
                menu_index = MENU_START_PRINT;
            }
            break;
        case MENU_BAUD:
            // wait for response
            if (!hasResponded) {
                return;
            }

            const long x = atol(response.c_str());
            if (x == 0 || x < 0) {
                client.printf("Invalid baud rate '%s'", response.c_str());
                return;
            }

            settings.baud = x;
            Serial.updateBaudRate(x);

            menu_index = MENU_WIFI_PRINT;

            break;
        case MENU_PARITY:
            // wait for response
            if (!hasResponded) {
                return;
            }

            menu_index = MENU_WIFI_PRINT;

            break;
    }
}

void do_menu_start(String& response) {
    if (response.isEmpty() || response.length() != 1) {
        client.printf("Invalid option '%s'\r\n", response.c_str());
        return;
    }

    // dont care about case sensitivity
    response.toLowerCase();

    switch (response.charAt(0)) {
        case 'w':
            menu_index = MENU_WIFI;
            return;
        case 'b':
            client.printf("Please enter baud rate: ");
            menu_index = MENU_BAUD;
            break;
        case 'p':
            client.printf("parity..\r\n");
            menu_index = MENU_PARITY;
            break;
        case 's':
            client.printf("Saving to EEPROM..\r\n");
            EEPROM.put(0, VERSION);
            EEPROM.put(1, settings);
            break;
        case 'r':
            client.printf("This will reset all settings and restart the device, do you wish to proceed ? (y/n)");
            menu_index = MENU_RESET;
            break;
        default:
            client.printf("Unknown option '%s'\r\n", response.c_str());
            break;
    }
}

void do_menu_wifi(String& response) {
    if (response.isEmpty() || response.length() != 1) {
        client.printf("Invalid option '%s'\r\n", response.c_str());
        return;
    }

    // dont care about case sensitivity
    response.toLowerCase();

    switch (response.charAt(0)) {
        case 's':
            client.printf("wifi..\r\n");
            break;
        case 'p':
            client.printf("baud rate..\r\n");
            break;
        case 'c':
            client.printf("parity..\r\n");
            break;
        case 'h':
            client.printf("quitting..\r\n");
            break;
        case 'b':
            menu_index = MENU_START_PRINT;
            break;
        default:
            client.printf("Unknown option '%s'\r\n", response.c_str());
            break;
    }
}

bool is_valid_parity(char* config) {
    // TODO
    return false;
}

// bool serial_begin(unsigned long baud, char* config) {
//     if (config == "5N1") {
//         Serial.begin(baud, SERIAL_5N1);
//     } else if (config == "6N1") {
//         Serial.begin(baud, SERIAL_6N1);
//     } else if (config == "7N1") {
//         Serial.begin(baud, SERIAL_7N1);
//     } else if (config == "5N2") {
//         Serial.begin(baud, SERIAL_5N2);
//     } else if (config == "6N2") {
//         Serial.begin(baud, SERIAL_6N2);
//     } else if (config == "7N2") {
//         Serial.begin(baud, SERIAL_7N2);
//     } else if (config == "8N2") {
//         Serial.begin(baud, SERIAL_8N2);
//     } else if (config == "5E1") {
//         Serial.begin(baud, SERIAL_5E1);
//     } else if (config == "6E1") {
//         Serial.begin(baud, SERIAL_6E1);
//     } else if (config == "7E1") {
//         Serial.begin(baud, SERIAL_7E1);
//     } else if (config == "8E1") {
//         Serial.begin(baud, SERIAL_8E1);
//     } else if (config == "5E2") {
//         Serial.begin(baud, SERIAL_5E2);
//     } else if (config == "6E2") {
//         Serial.begin(baud, SERIAL_6E2);
//     } else if (config == "7E2") {
//         Serial.begin(baud, SERIAL_7E2);
//     } else if (config == "8E2") {
//         Serial.begin(baud, SERIAL_8E2);
//     } else if (config == "5O1") {
//         Serial.begin(baud, SERIAL_5O1);
//     } else if (config == "6O1") {
//         Serial.begin(baud, SERIAL_6O1);
//     } else if (config == "7O1") {
//         Serial.begin(baud, SERIAL_7O1);
//     } else if (config == "8O1") {
//         Serial.begin(baud, SERIAL_8O1);
//     } else if (config == "5O2") {
//         Serial.begin(baud, SERIAL_5O2);
//     } else if (config == "6O2") {
//         Serial.begin(baud, SERIAL_6O2);
//     } else if (config == "7O2") {
//         Serial.begin(baud, SERIAL_7O2);
//     } else if (config == "8O2") {
//         Serial.begin(baud, SERIAL_8O2);
//     } else if (config == "8N1") {
//         Serial.begin(baud, SERIAL_8N1);
//     } else if (config == "8N2") {
//         Serial.begin(baud, SERIAL_8N2);
//     } else {
//         return false;
//     }

//     return true;
// }