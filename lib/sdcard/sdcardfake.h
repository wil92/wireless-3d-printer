#pragma once

#include <Arduino.h>

#define FILE_APPEND "a"
#define FILE_WRITE "w"

namespace sdcardfake {
class File {
    bool valid_;
    bool directory_;
    int remaining_;

public:
    File(bool valid = false, bool directory = false, int entries = 0)
        : valid_(valid), directory_(directory), remaining_(entries) {}

    operator bool() const { return valid_; }
    bool isDirectory() const { return directory_; }
    File openNextFile() {
        return valid_ && remaining_-- > 0 ? File(true) : File();
    }
    String name() { return "file.gcode"; }
    size_t size() { return 42; }
    size_t write(const byte *, size_t size) { return size; }
    void close() { valid_ = false; }
};

class SDLib {
public:
    bool pinsResult = true;
    bool beginResult = true;
    bool rootValid = true;
    bool rootDirectory = true;
    int rootEntries = 0;
    int pins[6] = {};
    int setPinsCalls = 0;
    int beginCalls = 0;
    int endCalls = 0;
    int openCalls = 0;
    int removeCalls = 0;
    String mountpoint;
    bool mode1bit = true;
    bool format = true;
    int frequency = 0;

    bool setPins(int clk, int cmd, int d0, int d1, int d2, int d3) {
        ++setPinsCalls;
        pins[0] = clk; pins[1] = cmd; pins[2] = d0;
        pins[3] = d1; pins[4] = d2; pins[5] = d3;
        return pinsResult;
    }

    bool begin(const char *path, bool oneBit, bool formatOnFailure, int freq) {
        ++beginCalls;
        mountpoint = path;
        mode1bit = oneBit;
        format = formatOnFailure;
        frequency = freq;
        return beginResult;
    }

    void end() { ++endCalls; }
    File open(const String &) {
        ++openCalls;
        return File(rootValid, rootDirectory, rootEntries);
    }
    File open(const String &, const String &) {
        ++openCalls;
        return File(true);
    }
    bool remove(const String &) { ++removeCalls; return true; }
};

extern SDLib SD_MMC;
} // namespace sdcardfake
