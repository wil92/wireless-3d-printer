#include "runtime_config.h"

#if defined(ARDUINO_ARCH_ESP32) && defined(NETWORK_DEBUG_RUNTIME_CONFIG)
#include "debug_address.h"

String RuntimeDebugConfig::begin() {
    available = preferences.begin("debug-logging", false);
    if (!available) {
        Serial.println("Cannot open debug configuration storage");
        return "";
    }
    Serial.println("Debug receiver: use 'debug-address HOST[:PORT]' or 'debug-address off', then restart");
    const String saved = preferences.getString("receiver", "");
    if (saved.length() == 0) return "";
    std::string url;
    if (!debugAddressUrl(saved.c_str(), url)) {
        Serial.println("Saved debug receiver is invalid; network logging disabled");
        return "";
    }
    return String(url.c_str());
}

void RuntimeDebugConfig::handleLine(const String &line) {
    const String prefix = "debug-address ";
    if (!line.startsWith(prefix)) {
        Serial.println("Unknown command; use debug-address HOST[:PORT] or debug-address off");
        return;
    }
    if (!available) {
        Serial.println("Cannot save debug receiver: storage unavailable");
        return;
    }
    const String address = line.substring(prefix.length());
    if (address == "off") {
        if (preferences.isKey("receiver") && !preferences.remove("receiver")) {
            Serial.println("Failed to clear debug receiver");
            return;
        }
    } else {
        std::string url;
        if (!debugAddressUrl(address.c_str(), url)) {
            Serial.println("Invalid debug receiver; expected HOST[:PORT]");
            return;
        }
        if (preferences.putString("receiver", address) != address.length()) {
            Serial.println("Failed to save debug receiver");
            return;
        }
    }
    Serial.println("Debug receiver saved; restart to apply");
}

void RuntimeDebugConfig::poll() {
    for (unsigned int count = 0; count < 128 && Serial.available(); ++count) {
        const char c = static_cast<char>(Serial.read());
        if (c == '\r') continue;
        if (c == '\n') {
            if (overflow) Serial.println("Debug command too long");
            else if (input.length()) handleLine(input);
            input = "";
            overflow = false;
        } else if (!overflow) {
            if (c < 32 || c > 126 || input.length() >= 160) overflow = true;
            else input += c;
        }
    }
}
#endif
