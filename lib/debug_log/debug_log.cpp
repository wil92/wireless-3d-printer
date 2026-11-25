#include "debug_log.h"

#if defined(ARDUINO_ARCH_ESP32) && (defined(NETWORK_DEBUG_ADDRESS) || defined(NETWORK_DEBUG_RUNTIME_CONFIG))
#include <HTTPClient.h>
#include <WiFi.h>
#include <freertos/FreeRTOS.h>
#include <freertos/queue.h>
#include <freertos/task.h>

namespace {
constexpr size_t messageSize = 256;
struct LogMessage { char text[messageSize]; };
QueueHandle_t messages = nullptr;
String receiverAddress;

void sendLogs(void *) {
    LogMessage message;
    for (;;) {
        if (WiFi.status() != WL_CONNECTED) {
            vTaskDelay(pdMS_TO_TICKS(500));
            continue;
        }
        if (xQueueReceive(messages, &message, pdMS_TO_TICKS(500)) != pdTRUE) continue;
        HTTPClient http;
        http.setConnectTimeout(500);
        http.setTimeout(500);
        if (!http.begin(receiverAddress)) {
            Serial.println("Network debug URL is invalid");
            continue;
        }
        http.addHeader("Content-Type", "text/plain; charset=utf-8");
        const int status = http.POST(reinterpret_cast<uint8_t *>(message.text), strlen(message.text));
        if (status < 200 || status >= 300) {
            Serial.printf("Network debug POST failed: %d\n", status);
        }
        http.end();
    }
}
}

NetworkDebugLog DebugLog;

bool NetworkDebugLog::begin(const String &address) {
    if (address.length() == 0) {
        Serial.println("Network debug logging disabled: no receiver configured");
        return true;
    }
    receiverAddress = address;
    messages = xQueueCreate(32, sizeof(LogMessage));
    if (!messages) {
        Serial.println("Could not allocate network debug queue");
        return false;
    }
    if (xTaskCreate(sendLogs, "network-debug", 6144, nullptr, 1, nullptr) != pdPASS) {
        vQueueDelete(messages);
        messages = nullptr;
        Serial.println("Could not start network debug task");
        return false;
    }
    return true;
}

void NetworkDebugLog::println(const char *message) {
    Serial.println(message);
    if (!messages) return;
    LogMessage entry = {};
    if (strlcpy(entry.text, message, sizeof(entry.text)) >= sizeof(entry.text)) {
        Serial.println("Network debug message truncated");
    }
    if (xQueueSend(messages, &entry, 0) != pdTRUE) {
        Serial.println("Network debug queue full; message dropped");
    }
}

void NetworkDebugLog::println(const String &message) { println(message.c_str()); }
#endif
