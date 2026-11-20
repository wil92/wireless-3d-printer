#pragma once

#include <Arduino.h>

#include "../flash/flash_manager.h"

namespace webserverfake {
class WiFiClient {
public:
    virtual ~WiFiClient() {}

    operator bool() { return true; }

    virtual bool connected() { return true; }
    virtual bool available() { return true; }

    virtual String readStringUntil(char delimiter) { return ""; }

    virtual void stop() {}

    virtual void print(String ignore) {}

    virtual int read() { return '\0'; }
};

class WiFiServer {
public:
    WiFiServer(int port) {}

    void begin() {}

    void setNoDelay(bool ignore) {}

    WiFiClient accept() { return WiFiClient(); }
};

} // namespace webserverfake
