#pragma once

#include <Arduino.h>

#if defined(ARDUINO_ARCH_ESP32) && defined(NETWORK_DEBUG_ADDRESS)
class NetworkDebugLog {
public:
    bool begin();
    void println(const char *message);
    void println(const String &message);
};

extern NetworkDebugLog DebugLog;
#else
#define DebugLog Serial
#endif
