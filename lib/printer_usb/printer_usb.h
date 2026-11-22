#pragma once

#include <atomic>
#include <functional>

#include <EspUsbHost.h>
#include <freertos/FreeRTOS.h>
#include <freertos/queue.h>

#include "../marlin/marlin_client.h"

class PrinterUsb : public MarlinTransport {
public:
    bool begin();
    bool connected() override;
    bool write(const char *data, size_t length) override;
    int read(unsigned long timeoutMs) override;
    unsigned long now() override;
    uint32_t session() override;

private:
    EspUsbHost host_;
    QueueHandle_t received_ = nullptr;
    std::atomic<uint8_t> address_{0};
    std::atomic<bool> failed_{false};
    std::atomic<bool> overflow_{false};
    std::atomic<uint32_t> session_{0};
};
