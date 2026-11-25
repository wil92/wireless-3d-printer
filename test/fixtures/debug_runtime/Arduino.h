#pragma once

#include <string>

class String {
public:
    String() = default;
    String(const char *value) : text(value) {}
    size_t length() const { return text.size(); }
    const char *c_str() const { return text.c_str(); }
    bool startsWith(const String &prefix) const { return text.find(prefix.text) == 0; }
    String substring(size_t start) const { return String(text.substr(start).c_str()); }
    String &operator+=(char c) { text += c; return *this; }
    bool operator==(const char *other) const { return text == other; }
private:
    std::string text;
};

struct FakeSerial {
    std::string input;
    std::string output;
    bool available() const { return !input.empty(); }
    int read() {
        const char c = input[0];
        input.erase(0, 1);
        return c;
    }
    void println(const char *message) { output += std::string(message) + "\n"; }
};

extern FakeSerial Serial;
