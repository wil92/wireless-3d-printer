#pragma once

#include <functional>
#include <map>
#include <memory>

#ifdef ARDUINO_ARCH_ESP32
#include <WiFiClient.h>
#include <WiFiServer.h>

#include "../flash/flash_manager.h"
#else
#include "./webserverfake.h"

using namespace webserverfake;
#endif

#define CLIENT_TIMEOUT 2000

using ResetFunction = std::function<void(void)>;

class WebServerManager {
#ifndef ARDUINO_ARCH_ESP32
public:
#endif
    bool checkEndpoints(WiFiClient *client, const String &message);

    void handleClient(WiFiClient &client);

#ifndef ARDUINO_ARCH_ESP32
private:
#endif
    void handleDeviceInfo(WiFiClient *client) const;

    void startNetworkScan(WiFiClient *client) const;

    void sendNetworks(WiFiClient *client) const;

    void sendTextResponse(WiFiClient *client, int code, const String &text) const;

    String prepareHeaders(int code) const;

    String prepareBaseHeaders(int code) const;

    String prepareIndexHtmlPage() const;

    void checkForConnections();

    void handleWebsite(WiFiClient *client, const String &message);

    bool handleUpdateConfig(WiFiClient *client, const String &message) const;

#ifndef ARDUINO_ARCH_ESP32
public:
#endif
    std::map<String, String> parseQueryParameters(const String &query,
                                                  int startPoint) const;
#ifndef ARDUINO_ARCH_ESP32
private:
#endif

    String consumeIncomingData(WiFiClient *client) const;

    void setParameter(const String &key, const String &value) const;

    static String urlDecode(const String &source);

public:
    FlashManager *flashManager = nullptr;
    std::function<void()> checkResetButton;
    std::function<bool()> startScan;
    std::function<int()> scanComplete;
    std::function<String(int)> scanSsid;
    std::unique_ptr<WiFiServer> server;
    ResetFunction rf;
    String deviceId;

    void setup();

    void run() const;

    void loop();
};
