#include "main.h"

#include <Arduino.h>
#include <esp_system.h>

#include "../include/hardware_pins.h"
#include "../lib/debug_log/debug_log.h"
#ifdef NETWORK_DEBUG_RUNTIME_CONFIG
#include "../lib/debug_log/runtime_config.h"
#endif
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
#ifdef NETWORK_DEBUG_RUNTIME_CONFIG
RuntimeDebugConfig debugConfig;
#endif

void resetConfigWhenButtonPress() {
#ifdef NETWORK_DEBUG_RUNTIME_CONFIG
    debugConfig.poll();
#endif
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
#ifdef NETWORK_DEBUG_RUNTIME_CONFIG
    DebugLog.begin(debugConfig.begin());
#elif defined(NETWORK_DEBUG_ADDRESS)
    DebugLog.begin(NETWORK_DEBUG_ADDRESS);
#endif
    DebugLog.println(String("ESP boot reset_reason=") + String(static_cast<int>(esp_reset_reason())));
    printer.setDiagnosticLogger([](const std::string &message) {
        DebugLog.println(message.c_str());
    });

    EEPROM.begin(CONFIG_STORAGE_SIZE);

    flashManager.loadSetup();

    pinMode(HardwarePins::resetConfig, INPUT_PULLUP);

    resetConfigWhenButtonPress();

    webServer.checkResetButton = resetConfigWhenButtonPress;
#ifdef NETWORK_DEBUG_RUNTIME_CONFIG
    webServer.debugReceiver = []() { return debugConfig.receiver(); };
    webServer.saveDebugReceiver = [](const String &address) {
        return debugConfig.saveReceiver(address);
    };
#endif

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
        webServer.deviceId = "wireless-3d-printer-" + deviceId;
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
