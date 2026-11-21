#include "main.h"

#include <Arduino.h>

#include "../include/hardware_pins.h"
#include "../lib/debug_log/debug_log.h"
#include "../lib/flash/flash_manager.h"
#include "../lib/network/access_point_config.h"
#include "../lib/network/network_manager.h"
#include "../lib/printer_usb/printer_usb.h"
#include "../lib/reset/reset_button.h"
#include "../lib/webserver/web_server_manager.h"

WifiNetworkManager networkManager;
FlashManager flashManager;
WebServerManager webServer;
ResetButton resetButton;
PrinterUsb printerUsb;
MarlinClient printer(printerUsb);
bool webServerReady = false;

void resetConfigWhenButtonPress() {
    resetButton.check(
        digitalRead(HardwarePins::resetConfig) == LOW,
        []() {
            DebugLog.println("Reset configuration requested");
            if (!flashManager.resetSetup()) {
                DebugLog.println("Failed to clear saved configuration");
                return false;
            }
            return true;
        },
        []() {
            DebugLog.println("Restarting ESP...");
            ESP.restart();
        });
}

void setup() {
    Serial.begin(9600);
#if defined(NETWORK_DEBUG_ADDRESS)
    DebugLog.begin();
#endif

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
        if (printerUsb.begin()) {
            webServer.printer = &printer;
        } else {
            DebugLog.println("Printer file operations unavailable");
        }
        webServer.rf = []() { ESP.restart(); };
        webServer.setup();
        webServer.run();
        webServerReady = true;

        String deviceId = WiFi.macAddress();
        deviceId.replace(":", "");
        deviceId.toLowerCase();
        webServer.deviceId = "remote-sd-" + deviceId;
        if (!networkManager.startDiscovery(webServer.deviceId)) {
            DebugLog.println("Device discovery unavailable; HTTP remains active");
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
