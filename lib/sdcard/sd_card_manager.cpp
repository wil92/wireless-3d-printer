#include "sd_card_manager.h"
#include "../../include/hardware_pins.h"

#ifndef ARDUINO_ARCH_ESP32
sdcardfake::SDLib sdcardfake::SD_MMC;
#endif

bool SDCardManager::begin() {
    if (mounted_) return true;
    if (!SD_MMC.setPins(HardwarePins::sdClock, HardwarePins::sdCommand,
                        HardwarePins::sdData0, HardwarePins::sdData1,
                        HardwarePins::sdData2, HardwarePins::sdData3)) {
        Serial.println("SDMMC pin configuration failed");
        return false;
    }
    if (!SD_MMC.begin("/sdcard", false, false, 20000)) {
        Serial.println("Card Mount Failed");
        return false;
    }
    mounted_ = true;
    return true;
}

void SDCardManager::end() {
    closeFile();
    if (mounted_) {
        SD_MMC.end();
        mounted_ = false;
    }
}

std::vector<FileInfo> SDCardManager::listFiles(bool beginConnectionToSD) {
    std::vector<FileInfo> files;
    if (beginConnectionToSD && !begin()) return files;

    File root = SD_MMC.open("/");
    if (!root) {
        Serial.println("File Not Found");
        if (beginConnectionToSD) end();
        return files;
    }
    if (!root.isDirectory()) {
        Serial.println("Not a directory");
        root.close();
        if (beginConnectionToSD) end();
        return files;
    }

    File file = root.openNextFile();
    while (file) {
        if (!file.isDirectory()) {
            files.push_back({String(file.name()), file.size()});
        }
        file = root.openNextFile();
    }
    root.close();

    if (beginConnectionToSD) {
        end();
    }

    return files;
}

void SDCardManager::removeFile(const String &fileName,
                               bool beginConnectionToSD) {
    if (beginConnectionToSD && !begin()) return;
    if (SD_MMC.remove("/" + fileName)) {
        Serial.println("File removed");
    } else {
        Serial.println("File not found");
    }
    if (beginConnectionToSD) {
        end();
    }
}

void SDCardManager::openFile(const String &fileName) {
    file_.reset(new File(SD_MMC.open("/" + fileName, FILE_WRITE)));
    if (!*file_) Serial.println("Error opening file for writing");
}

void SDCardManager::closeFile() {
    if (file_) {
        file_->close();
        file_.reset();
    }
}

void SDCardManager::appendFile(const String &fileName, const byte *chunk,
                               const size_t length) {
    if (length > 0) {
        if (!file_) {
            if (!begin()) return;
            File file = SD_MMC.open("/" + fileName, FILE_APPEND);
            if (!file) {
                Serial.println("Error opening file for appending");
            } else if (file.write(chunk, length) != length) {
                Serial.println("Failed to write to the file");
            }
            file.close();
            end();
        } else if (!*file_ || file_->write(chunk, length) != length) {
            Serial.println("Failed to write to the file");
        }
    }
}
