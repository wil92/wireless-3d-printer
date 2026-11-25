#pragma once

#include <string>

inline bool debugAddressUrl(const std::string &address, std::string &url) {
    if (address.empty() || address.size() > 128) return false;
    const size_t colon = address.find(':');
    const std::string host = address.substr(0, colon);
    if (host.empty()) return false;
    for (char c : host) {
        if (!((c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') ||
              (c >= '0' && c <= '9') || c == '.' || c == '-')) return false;
    }
    unsigned int port = 8080;
    if (colon != std::string::npos) {
        const std::string value = address.substr(colon + 1);
        if (value.empty() || value.size() > 5) return false;
        port = 0;
        for (char c : value) {
            if (c < '0' || c > '9') return false;
            port = port * 10 + static_cast<unsigned int>(c - '0');
        }
        if (port == 0 || port > 65535) return false;
    }
    url = "http://" + host + ":" + std::to_string(port) + "/";
    return true;
}
