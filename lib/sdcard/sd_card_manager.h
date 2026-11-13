#pragma once

#ifdef ARDUINO_ARCH_ESP32
#include <Arduino.h>
#include <SD_MMC.h>
#else
#include "sdcardfake.h"

using namespace sdcardfake;
#endif

#include "file_info.h"

#include <memory>
#include <vector>

class SDCardManager {
    std::unique_ptr<File> file_;
    bool mounted_ = false;

public:
    std::vector<FileInfo> listFiles(bool beginConnectionToSD);

    void removeFile(const String &fileName, bool beginConnectionToSD);

    void openFile(const String &fileName);

    void closeFile();

    void appendFile(const String &fileName, const byte *chunk, size_t size);

    bool begin();

    void end();
};
