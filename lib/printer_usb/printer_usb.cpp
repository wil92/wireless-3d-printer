#include "printer_usb.h"
#include "../debug_log/debug_log.h"

#include <Arduino.h>

namespace {
constexpr uint16_t ch340_vendor = 0x1a86;
constexpr uint16_t ch340_product = 0x7523;
constexpr size_t receive_buffer_size = 8192;
}

bool PrinterUsb::begin() {
    received_ = xQueueCreate(receive_buffer_size, sizeof(uint8_t));
    if (!received_) {
        DebugLog.println("Could not allocate printer USB receive buffer");
        return false;
    }
    host_.onDeviceConnected([this](const EspUsbHostDeviceInfo &device) {
        if (device.vid == ch340_vendor && device.pid == ch340_product &&
            address_.load() == 0) {
            xQueueReset(received_);
            failed_.store(false);
            session_.fetch_add(1);
            address_.store(device.address);
            DebugLog.println("CH340 printer USB connected");
        }
    });
    host_.onDeviceDisconnected([this](const EspUsbHostDeviceInfo &device) {
        if (address_.load() == device.address) {
            failed_.store(true);
            address_.store(0);
            session_.fetch_add(1);
            DebugLog.println("Printer USB disconnected");
        }
    });
    host_.onSerialData([this](const EspUsbHostSerialData &data) {
        if (data.address != address_.load() || data.port != 0) return;
        for (size_t i = 0; i < data.length; ++i) {
            if (xQueueSend(received_, &data.data[i], 0) != pdTRUE) {
                failed_.store(true);
                break;
            }
        }
    });
    if (!host_.setSerialBaudRate(115200) || !host_.begin()) {
        DebugLog.println("Could not start printer USB host");
        return false;
    }
    return true;
}

bool PrinterUsb::connected() {
    const uint8_t address = address_.load();
    return address != 0 && !failed_.load() && host_.serialReady(address, 0);
}

bool PrinterUsb::write(const char *data, size_t length) {
    const uint8_t address = address_.load();
    return connected() && host_.sendSerial(
        reinterpret_cast<const uint8_t *>(data), length, address, 0);
}

int PrinterUsb::read(unsigned long timeoutMs) {
    if (!connected()) return -2;
    uint8_t value;
    const unsigned long start = millis();
    do {
        if (!connected()) return -2;
        if (xQueueReceive(received_, &value, pdMS_TO_TICKS(10)) == pdTRUE) return value;
    } while (millis() - start < timeoutMs);
    return -1;
}

unsigned long PrinterUsb::now() { return millis(); }

uint32_t PrinterUsb::session() { return session_.load(); }
