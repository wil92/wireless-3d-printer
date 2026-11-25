#pragma once

#include <Arduino.h>

#if defined(ARDUINO_ARCH_ESP32) && (defined(NETWORK_DEBUG_ADDRESS) || defined(NETWORK_DEBUG_RUNTIME_CONFIG))
class NetworkDebugLog {
public:
    bool begin(const String &address);
    void println(const char *message);
    void println(const String &message);
};

extern NetworkDebugLog DebugLog;
#else
#define DebugLog Serial
#endif
