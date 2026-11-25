#pragma once

#if defined(ARDUINO_ARCH_ESP32) && defined(NETWORK_DEBUG_RUNTIME_CONFIG)
#include <Arduino.h>
#include <Preferences.h>

class RuntimeDebugConfig {
public:
    String begin();
    void poll();
    String receiver();
    bool saveReceiver(const String &address);

private:
    Preferences preferences;
    String input;
    bool available = false;
    bool overflow = false;
    void handleLine(const String &line);
};
#endif
