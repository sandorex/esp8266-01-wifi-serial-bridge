#include <ESP8266WiFi.h>
#include <WiFiClient.h>
#include <SoftwareSerial.h>
#include <stdlib.h>
#include <EEPROM.h>
#include <algorithm>  // std::min

#include "utils.hh"

const uint16_t VERSION = 3;

#define PORT_SETUP 20
#define PORT_SERIAL 23

#define RXBUFFERSIZE 1024
#define STACK_PROTECTOR 512  // bytes

#define NAME "ESP8266 Wifi Serial Bridge"

typedef struct {
    uint8_t channel = 7;
    char ssid[32] = "ESP8266 Serial";
    char password[16] = "password";
    SerialConfig config = SerialConfig::SERIAL_8N1;
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

    MENU_BAUD,
    MENU_CONFIG,

    MENU_WIFI_PRINT,
    MENU_WIFI,
    MENU_SSID,
    MENU_PASSWORD,
    MENU_CHANNEL
} MenuIndex;

MenuIndex menu_index = MENU_START_PRINT;

void setup() {
    // i do not need receive pin here
    swSerial.begin(9600, EspSoftwareSerial::SWSERIAL_8N1, -1, 2, false);
    swSerial.enableRx(false);
    swSerial.enableTx(false);

    delay(100);

    swSerial.println(NAME);
    swSerial.printf("Version: %d\r\n", VERSION);

    EEPROM.begin(512);

    {
        uint16_t version;
        EEPROM.get(0, version);

        // only load the eeprom data if same version as the current program
        if (version == VERSION) {
            swSerial.println("Using settings from EEPROM");

            EEPROM.get(sizeof(version), settings);
        } else {
            swSerial.printf("EEPROM settings version mismatch (%d != %d)\r\n", version, VERSION);
        }
    }

    swSerial.print("Setup: ");
    swSerial.print(local_ip);
    swSerial.print(":");
    swSerial.println(PORT_SETUP);

    swSerial.print("Serial: ");
    swSerial.print(local_ip);
    swSerial.print(":");
    swSerial.println(PORT_SERIAL);

    Serial.begin(settings.baud, settings.config);
    Serial.setRxBufferSize(RXBUFFERSIZE);

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

    delay(500);
}

void loop() {
    // each new client just drops the old one
    if (server_setup.hasClient()) {
        if (client) {
            client.stop();
        }

        client = server_setup.accept();
        is_setup = true;

        client.println(NAME " Setup");
        swSerial.println("New setup client connected");

        menu_index = MENU_START_PRINT;
    }

    if (server_serial.hasClient()) {
        if (client) {
            client.stop();
        }

        client = server_serial.accept();
        is_setup = false;

        client.println(NAME);
        swSerial.println("New serial client connected");
    }

    // there is nothing to do without a client
    if (!client) {
        return;
    }

    if (is_setup) {
        do_menu();
    } else {
        // NOTE: this is a simpler version of the below code
        /*
        // if there is any data from client send it to the serial
        if (client.available() > 0 && Serial.availableForWrite() > 0) {
            client.sendAvailable(Serial);
        }

        // if there is any data from serial send it to the client
        if (Serial.available() > 0 && client.availableForWrite() > 0) {
            Serial.sendAvailable(client);
        }
        */

        // more complex code
        while (client.available() > 0 && Serial.availableForWrite() > 0) {
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

void do_menu() {
    String response;
    bool hasResponded = false;

    // read 
    int len = client.available();
    if (len) {
        char raw[64];
        hasResponded = true;
        const int raw_length = std::min(len, 64);
        client.read(raw, raw_length);
        raw[raw_length] = '\0';
        response = raw;

        // removes any whitespace including newline and linefeed
        response.trim();
        
        client.printf("\r\n");
    }

    switch (menu_index) {
        case MENU_START_PRINT:
            client.printf("w) WiFi AP options\r\n");
            client.printf("b) Serial baud rate (%d)\r\n", settings.baud);
            client.printf("c) Serial config (%s)\r\n\n", serial_config_to_string(settings.config).value_or("???").c_str());

            client.printf("s) Save to EEPROM\r\n");
            client.printf("d) Restore default settings\r\n");
            client.printf("r) Reboot\r\n");
            client.printf("p) Programming mode\r\n");
            client.printf("q) Quit\r\n\n");
            client.printf("Choose an option: ");

            menu_index = MENU_START;
            return;
        case MENU_WIFI_PRINT:
            client.printf("s) Set SSID ('%s')\r\n", settings.ssid);
            client.printf("p) Set password ('%s')\r\n", settings.password);
            client.printf("c) Set channel (%d)\r\n", settings.channel);
            client.printf("h) Set hidden (%s)\r\n\n", settings.hidden ? "true" : "false");

            client.printf("b) Go back\r\n\n");

            client.printf("Choose an option: ");

            menu_index = MENU_WIFI;
            return;
        default:
            break;
    }

    // rest of the cases are for interactive stuff
    if (!hasResponded) {
        return;
    }

    switch (menu_index) {
        case MENU_START:
            if (response.isEmpty() || response.length() != 1) {
                client.printf("Invalid option '%s'\r\n", response.c_str());
                return;
            }

            // dont care about case sensitivity
            response.toLowerCase();

            switch (response.charAt(0)) {
                case 'w':
                    menu_index = MENU_WIFI_PRINT;
                    break;
                case 'b':
                    client.printf("Please enter baud rate: ");
                    menu_index = MENU_BAUD;
                    break;
                case 'c':
                    client.printf("Please enter serial config: ");
                    menu_index = MENU_CONFIG;
                    break;
                case 's':
                    client.printf("Saving to EEPROM..\r\n");
                    EEPROM.put(0, VERSION);
                    EEPROM.put(sizeof(VERSION), settings);
                    EEPROM.commit();

                    menu_index = MENU_START_PRINT;

                    break;
                case 'd':
                    client.printf("Restoring default settings..\r\n");
                    {
                        settings_t defaults;
                        settings = defaults;
                    }
                    menu_index = MENU_START_PRINT;
                    break;
                case 'r':
                    client.printf("Rebooting..\r\n");
                    client.flush();
                    ESP.restart();
                    break;
                case 'p':
                    client.printf("Rebooting into prog mode..\r\n");
                    client.flush();
                    ESP.rebootIntoUartDownloadMode();
                    break;
                case 'q':
                    client.printf("Disconnecting..\r\n");
                    client.stop();
                    break;
                default:
                    client.printf("Unknown option '%s'\r\n", response.c_str());
                    break;
            }
            break;
        case MENU_WIFI:
            if (response.isEmpty() || response.length() != 1) {
                client.printf("Invalid option '%s'\r\n", response.c_str());
                return;
            }

            // dont care about case sensitivity
            response.toLowerCase();

            switch (response.charAt(0)) {
                case 's':
                    client.printf("Please enter WiFi SSID: ");
                    menu_index = MENU_SSID;
                    break;
                case 'p':
                    client.printf("Please enter WiFi password: ");
                    menu_index = MENU_PASSWORD;
                    break;
                case 'c':
                    client.printf("Please enter WiFi channel (1-13): ");
                    menu_index = MENU_CHANNEL;
                    break;
                case 'h':
                    settings.hidden = !settings.hidden;
                    menu_index = MENU_WIFI_PRINT;
                    break;
                case 'b':
                    menu_index = MENU_START_PRINT;
                    break;
                default:
                    client.printf("Unknown option '%s'\r\n", response.c_str());
                    break;
            }

            break;
        case MENU_BAUD:
            {
                const long baud = atol(response.c_str());
                if (baud == 0 || baud < 0) {
                    client.printf("Invalid baud rate '%s'\r\n", response.c_str());
                    return;
                }

                settings.baud = baud;

                // serial does not need to be restarted for changes in baud rate
                Serial.updateBaudRate(baud);
                delay(500);
            }

            menu_index = MENU_START_PRINT;

            break;
        case MENU_CONFIG:
            response.toLowerCase();

            {
                const auto config = parse_serial_config(response);
                if (!config.has_value()) {
                    client.printf("Invalid serial config '%s'\r\n", response.c_str());
                    return;
                }

                settings.config = config.value();

                // restart serial with new settings
                Serial.flush();
                Serial.end();
                Serial.begin(settings.baud, settings.config);
                delay(500);
            }

            menu_index = MENU_START_PRINT;
            break;
        case MENU_SSID:
            if (response.isEmpty()) {
                client.printf("SSID cannot be empty\r\n");
                return;
            }

            strncpy(settings.ssid, response.c_str(), sizeof(settings.ssid));

            // make sure it has a null character
            settings.ssid[sizeof(settings.ssid) - 1] = '\0';

            menu_index = MENU_WIFI_PRINT;
            break;
        case MENU_PASSWORD:
            if (response.isEmpty()) {
                client.printf("Password cannot be empty\r\n");
                return;
            }

            strncpy(settings.password, response.c_str(), sizeof(settings.password));

            // make sure it has a null character
            settings.ssid[sizeof(settings.password) - 1] = '\0';

            menu_index = MENU_WIFI_PRINT;
            break;
        case MENU_CHANNEL:
            if (response.isEmpty()) {
                client.printf("Channel cannot be empty\r\n");
                return;
            }

            {
                int channel = atoi(response.c_str());

                // channels 1 - 13 are valid
                if (channel > 0 && channel <= 13) {
                    settings.channel = channel;
                } else {
                    client.printf("Invalid channel '%s'", response.c_str());
                    return;
                }
            }

            menu_index = MENU_WIFI_PRINT;
            break;
        default:
            break;
    }
}
