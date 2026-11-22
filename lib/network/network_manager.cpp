#include "network_manager.h"
#include "../debug_log/debug_log.h"

#include <ESPmDNS.h>

namespace {
void waitWithResetButtonChecks(const std::function<void()> &checkResetButton) {
    for (int i = 0; i < 10; ++i) {
        if (checkResetButton) {
            checkResetButton();
        }
        delay(100);
    }
}
}

WifiNetworkManager::WifiNetworkManager() {
    localIp_ = IPAddress(192, 168, 1, 1);
    gateway_ = IPAddress(192, 168, 1, 1);
    subnet_ = IPAddress(255, 255, 255, 0);
}

bool WifiNetworkManager::connectToNetwork(
    const char *ssidNetwork, const char *passwordNetwork,
    const std::function<void()> &checkResetButton) {
    if (!ssidNetwork || !ssidNetwork[0] || !passwordNetwork) {
        DebugLog.println("Cannot connect: Wi-Fi configuration is invalid");
        return false;
    }

    if (!WiFi.setHostname("wireless-3d-printer")) {
        DebugLog.println("Cannot connect: failed to set Wi-Fi hostname");
        return false;
    }
    WiFi.persistent(false);
    if (!WiFi.mode(WIFI_STA)) {
        DebugLog.println("Cannot connect: failed to enable Wi-Fi station mode");
        return false;
    }
    if (!WiFi.setAutoReconnect(true)) {
        DebugLog.println("Cannot connect: failed to enable Wi-Fi auto-reconnect");
        return false;
    }
    if (WiFi.begin(ssidNetwork, passwordNetwork) == WL_CONNECT_FAILED) {
        DebugLog.println("Cannot start Wi-Fi connection");
        return false;
    }

    int lastStatus = -1;
    while (true) {
        const wl_status_t status = WiFi.status();
        if (status == WL_CONNECTED) {
            break;
        }
        if (static_cast<int>(status) != lastStatus) {
            lastStatus = static_cast<int>(status);
            DebugLog.println(String("Wi-Fi connection status: ") + String(lastStatus));
        }
        waitWithResetButtonChecks(checkResetButton);
    }
    DebugLog.println("Connected. IP address: " + WiFi.localIP().toString());
    return true;
}

bool WifiNetworkManager::startDiscovery(const String &hostname) const {
    if (!MDNS.begin(hostname)) {
        DebugLog.println("Failed to start mDNS");
        return false;
    }
    if (!MDNS.addService("wireless-3dp", "tcp", 80)) {
        DebugLog.println("Failed to register mDNS discovery service");
        MDNS.end();
        return false;
    }
    return true;
}

bool WifiNetworkManager::startScan() const {
    WiFi.scanDelete();
    return WiFi.scanNetworks(true) != WIFI_SCAN_FAILED;
}

int WifiNetworkManager::scanComplete() const {
    const int result = WiFi.scanComplete();
    if (result == WIFI_SCAN_RUNNING) return NETWORK_SCAN_RUNNING;
    return result < 0 ? NETWORK_SCAN_FAILED : result;
}

String WifiNetworkManager::scanSsid(int index) const { return WiFi.SSID(index); }

bool WifiNetworkManager::createHotspot(
    const char *ssidNetwork, const char *passwordNetwork,
    const std::function<void()> &checkResetButton) const {
    if (!WiFi.mode(WIFI_AP_STA) ||
        !WiFi.softAPConfig(localIp_, gateway_, subnet_)) {
        DebugLog.println("Failed to configure setup access point");
        return false;
    }
    int tries = 10;

    while (!WiFi.softAP(ssidNetwork, passwordNetwork) && tries-- > 0) {
        waitWithResetButtonChecks(checkResetButton);
    }

    if (tries == 0) {
        return false;
    }

    return true;
}
