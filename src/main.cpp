#include "main.h"

#include <Arduino.h>

#include "../include/hardware_pins.h"
#include "../lib/flash/flash_manager.h"
#include "../lib/network/access_point_config.h"
#include "../lib/network/network_manager.h"
#include "../lib/reset/reset_button.h"
#include "../lib/webserver/web_server_manager.h"

NetworkManager networkManager;
FlashManager flashManager;
WebServerManager webServer;
ResetButton resetButton;
bool webServerReady = false;

void resetConfigWhenButtonPress() {
    resetButton.check(
        digitalRead(HardwarePins::resetConfig) == LOW,
        []() {
            Serial.println("Reset configuration requested");
            if (!flashManager.resetSetup()) {
                Serial.println("Failed to clear saved configuration");
                return false;
            }
            return true;
        },
        []() {
            Serial.println("Restarting ESP...");
            ESP.restart();
        });
}

void setup() {
    Serial.begin(9600);

    EEPROM.begin(CONFIG_STORAGE_SIZE);

    flashManager.loadSetup();

    pinMode(HardwarePins::resetConfig, INPUT_PULLUP);

    resetConfigWhenButtonPress();

    webServer.checkResetButton = resetConfigWhenButtonPress;

    if (flashManager.isSetup) {
        if (!networkManager.connectToNetwork(flashManager.ssidNetwork.c_str(),
                                             flashManager.passwordNetwork.c_str(),
                                             resetConfigWhenButtonPress)) {
            return;
        }

        webServer.flashManager = &flashManager;
        webServer.rf = []() { ESP.restart(); };
        webServer.setup();
        webServer.run();
        webServerReady = true;

        String deviceId = WiFi.macAddress();
        deviceId.replace(":", "");
        deviceId.toLowerCase();
        webServer.deviceId = "remote-sd-" + deviceId;
        if (!networkManager.startDiscovery(webServer.deviceId)) {
            Serial.println("Device discovery unavailable; HTTP remains active");
        }
    } else {
        if (!networkManager.createHotspot(AP_SSID, AP_PASSWORD,
                                          resetConfigWhenButtonPress)) {
            ESP.restart();
        }
        delay(100);

        webServer.flashManager = &flashManager;
        webServer.rf = []() { ESP.restart(); };
        webServer.startScan = []() { return networkManager.startScan(); };
        webServer.scanComplete = []() { return networkManager.scanComplete(); };
        webServer.scanSsid = [](int index) {
            return networkManager.scanSsid(index);
        };
        webServer.setup();
        webServer.run();
        webServerReady = true;
    }
}

void loop() {
    resetConfigWhenButtonPress();
    if (webServerReady) {
        webServer.loop();
    }
}
