#pragma once

#include "Arduino.h"

extern std::string savedReceiver;
extern bool storageAvailable;
extern bool failWrite;
extern bool failRemove;

class Preferences {
public:
    bool begin(const char *, bool) { return storageAvailable; }
    String getString(const char *, const char *) { return String(savedReceiver.c_str()); }
    bool isKey(const char *) { return !savedReceiver.empty(); }
    bool remove(const char *) {
        if (failRemove) return false;
        savedReceiver.clear();
        return true;
    }
    size_t putString(const char *, const String &value) {
        if (failWrite) return 0;
        savedReceiver = value.c_str();
        return value.length();
    }
};
