#include "web_server_manager.h"

#include "views/server-config.index.h"
#include "views/status.index.h"

#include "../scan_status.h"
#include "../text_utils.h"

#include <cstdlib>

namespace {
String escapeJson(const String &value) {
    const char hex[] = "0123456789abcdef";
    String result;
    for (unsigned int i = 0; i < value.length(); ++i) {
        const unsigned char ch = static_cast<unsigned char>(value[i]);
        if (ch == '"' || ch == '\\') {
            result += '\\';
            result += static_cast<char>(ch);
        } else if (ch < 0x20) {
            result += "\\u00";
            result += hex[ch >> 4];
            result += hex[ch & 0x0f];
        } else {
            result += static_cast<char>(ch);
        }
    }
    return result;
}

String escapeHtmlAttribute(const String &value) {
    String result;
    for (unsigned int i = 0; i < value.length(); ++i) {
        switch (value[i]) {
        case '&': result += "&amp;"; break;
        case '"': result += "&quot;"; break;
        case '<': result += "&lt;"; break;
        case '>': result += "&gt;"; break;
        case '\'': result += "&#39;"; break;
        default: result += value[i]; break;
        }
    }
    return result;
}
}

void WebServerManager::setup() { server.reset(new WiFiServer(80)); }

void WebServerManager::run() const {
    server->begin();
    server->setNoDelay(true);
}

void WebServerManager::loop() { checkForConnections(); }

void WebServerManager::checkForConnections() {
    WiFiClient client = server->accept();
    if (client) {
        handleClient(client);
    }
}

void WebServerManager::handleClient(WiFiClient &client) {
    Serial.println("Client connected");

    const unsigned long connectionTime = millis();
    bool resetFlag = false;

    while (client.connected() &&
           millis() - connectionTime <= CLIENT_TIMEOUT) {
        if (checkResetButton) {
            checkResetButton();
        }
        if (client.available()) {
            String line = client.readStringUntil('\n');
            if (checkEndpoints(&client, line)) {
                resetFlag = true;
                const unsigned long responseTime = millis();
                while (client.connected() &&
                       millis() - responseTime < CLIENT_TIMEOUT) {
                    if (checkResetButton) {
                        checkResetButton();
                    }
                    delay(10);
                }
                break;
            }
        }
    }

    Serial.println("Client disconnected");
    client.stop();

    if (resetFlag && rf) {
        rf();
    }
}

bool WebServerManager::checkEndpoints(WiFiClient *client,
                                      const String &message) {
    if (message.startsWith("POST /scan HTTP/")) {
        startNetworkScan(client);
        return false;
    }
    if (message.startsWith("GET /refresh-ssid HTTP/")) {
        sendNetworks(client);
        return false;
    }
    if (message.startsWith("POST /upload ") ||
        message.startsWith("DELETE /") ||
        message.startsWith("GET /api/files HTTP/")) {
        sendTextResponse(client, flashManager->isSetup ? 503 : 404,
                         flashManager->isSetup
                             ? "File operations are unavailable."
                             : "File operations are unavailable during setup.");
        client->stop();
        return false;
    }
    if (message.startsWith("GET /api/device HTTP/")) {
        handleDeviceInfo(client);
        return false;
    }
    if (message.startsWith("GET /")) {
        handleWebsite(client, message);
    }
    if (message.startsWith("POST /?")) {
        return handleUpdateConfig(client, message);
    }
    return false;
}

void WebServerManager::sendTextResponse(WiFiClient *client, int code,
                                        const String &text) const {
    const char *status = code == 202 ? "202 Accepted" :
                         code == 400 ? "400 Bad Request" :
                         code == 404 ? "404 Not Found" :
                         code == 503 ? "503 Service Unavailable" :
                                       "500 Internal Server Error";
    client->print(String("HTTP/1.1 ") + status +
                  "\r\nContent-Type: text/plain; charset=utf-8\r\n"
                  "Cache-Control: no-store\r\nContent-Length: " +
                  String(text.length()) + "\r\nConnection: close\r\n\r\n" +
                  text);
}

void WebServerManager::startNetworkScan(WiFiClient *client) const {
    consumeIncomingData(client);
    if (flashManager->isSetup) {
        sendTextResponse(client, 404, "Network scanning is only available during setup.");
    } else if (!scanComplete || !startScan) {
        Serial.println("Network scanner unavailable");
        sendTextResponse(client, 500, "Network scanner unavailable.");
    } else if (scanComplete() == NETWORK_SCAN_RUNNING || startScan()) {
        sendTextResponse(client, 202, "Scanning");
    } else {
        Serial.println("Failed to start Wi-Fi scan");
        sendTextResponse(client, 500, "Could not start Wi-Fi scan.");
    }
}

void WebServerManager::sendNetworks(WiFiClient *client) const {
    consumeIncomingData(client);
    if (flashManager->isSetup) {
        sendTextResponse(client, 404, "Network scanning is only available during setup.");
        return;
    }
    if (!scanComplete || !scanSsid) {
        Serial.println("Network scanner unavailable");
        sendTextResponse(client, 500, "Network scanner unavailable.");
        return;
    }
    const int count = scanComplete();
    if (count == NETWORK_SCAN_RUNNING) {
        sendTextResponse(client, 202, "Scanning");
        return;
    }
    if (count < 0) {
        Serial.println("Wi-Fi scan failed or has not been started");
        sendTextResponse(client, 500, "Wi-Fi scan failed; try again.");
        return;
    }
    String result = "[";
    int included = 0;
    for (int i = 0; i < count && i < 256 && included < 40; ++i) {
        const String ssid = scanSsid(i);
        if (ssid.length() == 0 || !isValidUtf8(ssid, false)) continue;
        if (included++) result += ',';
        result += "\"" + escapeJson(ssid) + "\"";
    }
    result += "]";
    client->print(String("HTTP/1.1 200 OK\r\n"
                         "Content-Type: application/json; charset=utf-8\r\n"
                         "Cache-Control: no-store\r\nContent-Length: ") +
                  String(result.length()) + "\r\nConnection: close\r\n\r\n" +
                  result);
}

void WebServerManager::handleDeviceInfo(WiFiClient *client) const {
    consumeIncomingData(client);
    if (!flashManager->isSetup) {
        client->print(prepareHeaders(404));
        return;
    }
    client->print("HTTP/1.1 200 OK\r\n"
                  "Content-Type: application/json\r\n"
                  "Cache-Control: no-store\r\n"
                  "Connection: close\r\n\r\n");
    client->print("{\"type\":\"remote-sd\",\"id\":\"" + deviceId +
                  "\",\"name\":\"" + escapeJson(flashManager->deviceName) +
                  "\",\"api_version\":1}");
}

void WebServerManager::handleWebsite(WiFiClient *client,
                                     const String &message) {
    client->print(prepareHeaders(200));
    client->print(flashManager->isSetup ? String(status_index_html)
                                        : prepareIndexHtmlPage());
    client->print("\r\n");
    consumeIncomingData(client);
}

bool WebServerManager::handleUpdateConfig(WiFiClient *client,
                                          const String &message) const {
    const int index = static_cast<int>(String("POST /").length()) + 1;
    if (message.indexOf("%00", index) != -1) {
        consumeIncomingData(client);
        Serial.println("Invalid configuration");
        sendTextResponse(client, 400, "Invalid configuration.");
        return false;
    }
    std::map<String, String> params = parseQueryParameters(message, index);
    const String previousSsid = flashManager->ssidNetwork;
    const String previousPassword = flashManager->passwordNetwork;
    const String previousName = flashManager->deviceName;
    for (std::map<String, String>::const_iterator parameter = params.begin();
         parameter != params.end(); ++parameter) {
        setParameter(parameter->first, parameter->second);
    }
    consumeIncomingData(client);
    const bool valid = flashManager->canSaveSetup() &&
                       flashManager->ssidNetwork.length() > 0 &&
                       flashManager->ssidNetwork.length() <= 32 &&
                       isValidUtf8(flashManager->ssidNetwork, false);
    if (!valid || !flashManager->saveSetup()) {
        flashManager->ssidNetwork = previousSsid;
        flashManager->passwordNetwork = previousPassword;
        flashManager->deviceName = previousName;
        Serial.println(valid ? "Failed to save configuration" :
                               "Invalid configuration");
        sendTextResponse(client, valid ? 500 : 400,
                         valid ? "Could not save configuration." :
                                 "Invalid device name, SSID, or configuration length.");
        return false;
    }
    flashManager->loadSetup();
    client->print(prepareHeaders(204));
    return true;
}

String WebServerManager::consumeIncomingData(WiFiClient *client) const {
    while (client->connected()) {
        if (checkResetButton) {
            checkResetButton();
        }
        if (client->available()) {
            String line = client->readStringUntil('\n');
            if (line.length() <= 2) {
                Serial.println("finish consuming");
                break;
            }
        }
    }
    return "";
}

std::map<String, String>
WebServerManager::parseQueryParameters(const String &query,
                                       int startPoint) const {
    std::map<String, String> params;
    String key = "", value = "";
    bool flag = false;
    for (int index = startPoint;
         index < static_cast<int>(query.length()) && query[index] != ' ';
         index++) {
        if (!flag) {
            if (query[index] == '=') {
                flag = true;
            } else {
                key += query[index];
            }
        } else {
            if (query[index] == '&') {
                flag = false;
                params[urlDecode(key)] = urlDecode(value);
                key = "";
                value = "";
            } else {
                value += query[index];
            }
        }
    }
    if (key != "") {
        params[urlDecode(key)] = urlDecode(value);
    }
    return params;
}

String WebServerManager::urlDecode(const String &src) {
    String ret;
    for (int i = 0; i < static_cast<int>(src.length()); ++i) {
        if (src[i] == '%') {
            if (i + 2 < static_cast<int>(src.length())) {
                char encodedByte[3] = {src[i + 1], src[i + 2], '\0'};
                char *end = nullptr;
                const long decodedByte = std::strtol(encodedByte, &end, 16);
                if (end != encodedByte && *end == '\0') {
                    ret += static_cast<char>(decodedByte);
                    i += 2;
                    continue;
                }
            }
            ret += src[i];
        } else if (src[i] == '+') {
            ret += ' ';
        } else {
            ret += src[i];
        }
    }
    return ret;
}

void WebServerManager::setParameter(const String &key,
                                    const String &value) const {
    flashManager->setVariable(key, value);
}

String WebServerManager::prepareHeaders(int code) const {
    return prepareBaseHeaders(code) + "Connection: close\r\n\r\n";
}

String WebServerManager::prepareBaseHeaders(int code) const {
    switch (code) {
    case 500:
        return String("HTTP/1.1 500 Internal Server Error\r\nContent-Type: "
                      "text/html\r\n");
    case 400:
        return String(
            "HTTP/1.1 400 Bad Request\r\nContent-Type: text/html\r\n");
    case 404:
        return String("HTTP/1.1 404 Not Found\r\nContent-Type: text/html\r\n");
    case 204:
        return String("HTTP/1.1 204 No Content\r\n");
    case 200:
    default:
        return String("HTTP/1.1 200 OK\r\nContent-Type: text/html\r\n");
    }
}

String WebServerManager::prepareIndexHtmlPage() const {
    const String page = String(server_config_index_html) + "\r\n";
    String result;
    for (unsigned int i = 0; i < page.length();) {
        if (page.startsWith("$$SSID$$", i)) {
            result += escapeHtmlAttribute(flashManager->ssidNetwork);
            i += 8;
        } else if (page.startsWith("$$PASSWORD$$", i)) {
            result += escapeHtmlAttribute(flashManager->passwordNetwork);
            i += 12;
        } else if (page.startsWith("$$NAME$$", i)) {
            result += escapeHtmlAttribute(flashManager->deviceName);
            i += 8;
        } else {
            result += page[i++];
        }
    }
    return result;
}
