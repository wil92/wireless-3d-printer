#include <ArduinoFake.h>
#include <EEPROM.h>
#include <algorithm>
#include <cstring>
#include <deque>
#include <map>
#include <string>
#include <unity.h>
#include <vector>

#include "../../../lib/webserver/web_server_manager.h"
#include "../../../lib/scan_status.h"

using namespace fakeit;

uint8_t eepromData[CONFIG_STORAGE_SIZE];
unsigned long fakeMillis;

class WiFiClientTmp : public webserverfake::WiFiClient {
public:
    std::vector<byte> data;
    size_t index = 0;
    std::string response;

    bool connected() override { return index < data.size(); }

    bool available() override { return index < data.size(); }

    String readStringUntil(char delimiter) override {
        String result;
        while (index < data.size() &&
               data[index] != static_cast<byte>(delimiter)) {
            result += static_cast<char>(data[index++]);
        }
        if (index < data.size()) {
            ++index;
        }
        return result;
    }

    int read() override {
        if (index >= data.size()) {
            return -1;
        }
        return data[index++];
    }

    void print(String value) override { response += value.c_str(); }

    void buildHeaders() { addData("Host: localhost\r\n\r\n"); }

    void buildSensitiveHeaders() {
        addData("Referer: http://localhost/?ssid=net&password=dummy-secret\r\n\r\n");
    }

private:
    void addData(const char *message) {
        data.insert(data.end(), message, message + std::strlen(message));
    }
};

class SetupConnectionClient : public WiFiClientTmp {
public:
    bool keepOpen = false;
    bool stopped = false;

    bool connected() override {
        return !stopped && (keepOpen || index < data.size());
    }

    void stop() override { stopped = true; }

    void setRequest(const std::string &request) {
        data.assign(request.begin(), request.end());
    }
};

class PrinterTestTransport : public MarlinTransport {
public:
    std::deque<char> incoming;
    std::vector<std::string> writes;
    unsigned long time = 0;
    bool busy = false;
    std::string listing = "OLD.GCO 42 Old print.gcode\n";

    bool connected() override { return true; }
    bool write(const char *data, size_t length) override {
        std::string command(data, length), reply = "ok\n";
        writes.push_back(command);
        if (command == "M115\n") reply = "FIRMWARE_NAME:Marlin\nok\n";
        else if (command == "M27\n") reply = busy ? "SD printing byte 1/5\nok\n" : "Not SD printing\nok\n";
        else if (command == "M105\n") reply = "ok T:205.25 /210 B:60.5 /60\n";
        else if (command == "M20 L\n") reply = "Begin file list\n" + listing + "End file list\nok\n";
        else if (command.find("M28 ") == 0) reply = "Writing to file: NEW.GCO\nok\n";
        else if (command == "M29\n") reply = "Done saving file\nok\n";
        else if (command == "M30 /OLD.GCO\n") reply = "File deleted: OLD.GCO\nok\n";
        else if (command == "M30 /TWO.GCO\n") reply = "File deleted: TWO.GCO\nok\n";
        for (char c : reply) incoming.push_back(c);
        return true;
    }
    int read(unsigned long timeout) override {
        if (incoming.empty()) { time += timeout; return -1; }
        const char c = incoming.front();
        incoming.pop_front();
        ++time;
        return static_cast<unsigned char>(c);
    }
    unsigned long now() override { return time; }
    uint32_t session() override { return 1; }
};

void testPrinterStatusEndpoint() {
    FlashManager flash;
    flash.isSetup = true;
    flash.deviceName = "Printer \"A\"";
    PrinterTestTransport transport;
    MarlinClient printer(transport);
    WebServerManager manager;
    manager.flashManager = &flash;
    manager.printer = &printer;
    SetupConnectionClient request;
    request.setRequest("Host: localhost\r\n\r\n");
    manager.checkEndpoints(&request, "GET /api/status HTTP/1.1\r");
    TEST_ASSERT_TRUE(request.response.find("\"printer_name\":\"Printer \\\"A\\\"\"") != std::string::npos);
    TEST_ASSERT_TRUE(request.response.find("\"nozzle_temperature\":205.25,\"bed_temperature\":60.50") != std::string::npos);
    TEST_ASSERT_TRUE(request.response.find("\"printer_status\":\"not_sd_printing\"") != std::string::npos);
    transport.busy = true;
    SetupConnectionClient busy;
    busy.setRequest("Host: localhost\r\n\r\n");
    manager.checkEndpoints(&busy, "GET /api/status HTTP/1.1\r");
    TEST_ASSERT_TRUE(busy.response.find("\"printer_status\":\"sd_printing\"") != std::string::npos);
    flash.isSetup = false;
    SetupConnectionClient setup;
    manager.checkEndpoints(&setup, "GET /api/status HTTP/1.1\r");
    TEST_ASSERT_TRUE(setup.response.find("404 Not Found") != std::string::npos);
    flash.isSetup = true;
    manager.printer = nullptr;
    SetupConnectionClient offline;
    manager.checkEndpoints(&offline, "GET /api/status HTTP/1.1\r");
    TEST_ASSERT_TRUE(offline.response.find("503 Service Unavailable") != std::string::npos);
}

void testPrinterListAndRawUpload() {
    FlashManager flash;
    flash.isSetup = true;
    PrinterTestTransport transport;
    MarlinClient printer(transport);
    WebServerManager manager;
    manager.flashManager = &flash;
    manager.printer = &printer;
    SetupConnectionClient list;
    list.setRequest("Host: localhost\r\n\r\n");
    manager.checkEndpoints(&list, "GET /api/files HTTP/1.1\r");
    TEST_ASSERT_TRUE(list.response.find("\"name\":\"Old print.gcode\",\"size\":42,\"path\":\"OLD.GCO\"") != std::string::npos);
    SetupConnectionClient upload;
    const std::string body = "M117 TRANSFER TEST\r\n; ignored\r\nG1 X10 ; comment";
    upload.setRequest("Content-Type: application/octet-stream\r\nContent-Length: " +
                      std::to_string(body.size()) + "\r\n\r\n" + body);
    manager.checkEndpoints(&upload, "POST /upload?name=new.gcode HTTP/1.1\r");
    TEST_ASSERT_TRUE(upload.response.find("200 OK") != std::string::npos);
    TEST_ASSERT_TRUE(upload.response.find("Saved as NEW.GCO") != std::string::npos);
    TEST_ASSERT_EQUAL_STRING("N1 M117 TRANSFER TEST*56\n", transport.writes[transport.writes.size() - 3].c_str());
    TEST_ASSERT_EQUAL_STRING("N2 G1 X10*83\n", transport.writes[transport.writes.size() - 2].c_str());
    TEST_ASSERT_EQUAL_STRING("M29\n", transport.writes.back().c_str());
}

void testPrinterDeleteEndpoint() {
    FlashManager flash;
    flash.isSetup = true;
    PrinterTestTransport transport;
    MarlinClient printer(transport);
    WebServerManager manager;
    manager.flashManager = &flash;
    manager.printer = &printer;
    SetupConnectionClient request;
    request.setRequest("Host: localhost\r\n\r\n");
    manager.checkEndpoints(&request, "DELETE /api/files?path=OLD.GCO HTTP/1.1\r");
    TEST_ASSERT_TRUE(request.response.find("200 OK") != std::string::npos);
    TEST_ASSERT_EQUAL_STRING("M30 /OLD.GCO\n", transport.writes.back().c_str());
    SetupConnectionClient invalid;
    invalid.setRequest("Host: localhost\r\n\r\n");
    manager.checkEndpoints(&invalid, "DELETE /api/files?path=OLD%00.GCO HTTP/1.1\r");
    TEST_ASSERT_TRUE(invalid.response.find("400 Bad Request") != std::string::npos);
}

void testBulkDeleteEndpoint() {
    FlashManager flash;
    flash.isSetup = true;
    PrinterTestTransport transport;
    transport.listing += "TWO.GCO 12 Two.gcode\n";
    MarlinClient printer(transport);
    WebServerManager manager;
    manager.flashManager = &flash;
    manager.printer = &printer;
    SetupConnectionClient request;
    const std::string body = "OLD.GCO\nTWO.GCO";
    request.setRequest("Content-Type: text/plain\r\nContent-Length: " +
                       std::to_string(body.size()) + "\r\n\r\n" + body);
    manager.checkEndpoints(&request, "POST /api/files/delete HTTP/1.1\r");
    TEST_ASSERT_TRUE(request.response.find("200 OK") != std::string::npos);
    TEST_ASSERT_TRUE(request.response.find("Deleted 2 files.") != std::string::npos);
    TEST_ASSERT_EQUAL(1, std::count(transport.writes.begin(), transport.writes.end(), "M30 /OLD.GCO\n"));
    TEST_ASSERT_EQUAL(1, std::count(transport.writes.begin(), transport.writes.end(), "M30 /TWO.GCO\n"));

    transport.writes.clear();
    const std::string partial = "OLD.GCO\nMISSING.GCO\nTWO.GCO";
    SetupConnectionClient failure;
    failure.setRequest("Content-Type: text/plain\r\nContent-Length: " +
                       std::to_string(partial.size()) + "\r\n\r\n" + partial);
    manager.checkEndpoints(&failure, "POST /api/files/delete HTTP/1.1\r");
    TEST_ASSERT_TRUE(failure.response.find("409 Conflict") != std::string::npos);
    TEST_ASSERT_TRUE(failure.response.find("Confirmed 1 of 3 deletions. Failed at MISSING.GCO") != std::string::npos);
    TEST_ASSERT_EQUAL(1, std::count(transport.writes.begin(), transport.writes.end(), "M30 /OLD.GCO\n"));
    TEST_ASSERT_EQUAL(0, std::count(transport.writes.begin(), transport.writes.end(), "M30 /TWO.GCO\n"));
}

void testBulkDeleteRejectsInvalidRequestsBeforePrinterAccess() {
    FlashManager flash;
    flash.isSetup = true;
    PrinterTestTransport transport;
    MarlinClient printer(transport);
    WebServerManager manager;
    manager.flashManager = &flash;
    manager.printer = &printer;
    const std::vector<std::string> bodies = {
        "", "OLD.GCO\n", "OLD.GCO\n../BAD", "OLD.GCO\nold.gco",
        std::string("OLD.GCO\nBAD\0.GCO", 16), std::string(129, 'A'),
        std::string("OLD.GCO\n") + std::string(129, 'A')
    };
    for (const std::string &body : bodies) {
        SetupConnectionClient request;
        request.setRequest("Content-Type: text/plain\r\nContent-Length: " +
                           std::to_string(body.size()) + "\r\n\r\n" + body);
        manager.checkEndpoints(&request, "POST /api/files/delete HTTP/1.1\r");
        TEST_ASSERT_TRUE(request.response.find("400 Bad Request") != std::string::npos);
        TEST_ASSERT_TRUE(transport.writes.empty());
    }
    for (const std::string &headers : {
             "Content-Type: text/plain\r\nContent-Length: 8\r\nContent-Length: 8\r\n\r\nOLD.GCO\n",
             "Content-Type: text/plain\r\nContent-Length: 10\r\n\r\nOLD.GCO",
             "Content-Type: text/plain\r\nContent-Length: 33024\r\n\r\n",
             "Content-Type: text/plain\r\nTransfer-Encoding: chunked\r\n\r\n",
             "Content-Type: application/json\r\nContent-Length: 7\r\n\r\nOLD.GCO"}) {
        SetupConnectionClient request;
        request.setRequest(headers);
        manager.checkEndpoints(&request, "POST /api/files/delete HTTP/1.1\r");
        TEST_ASSERT_TRUE(request.response.find("400 Bad Request") != std::string::npos ||
                         request.response.find("413 Payload Too Large") != std::string::npos);
        TEST_ASSERT_TRUE(transport.writes.empty());
    }
    std::string largest;
    for (int i = 0; i < 256; ++i) {
        if (i) largest += '\n';
        const std::string number = std::to_string(i);
        largest += number + std::string(128 - number.size(), 'A');
    }
    TEST_ASSERT_EQUAL(33023, largest.size());
    transport.busy = true;
    SetupConnectionClient limit;
    limit.setRequest("Content-Type: text/plain\r\nContent-Length: " +
                     std::to_string(largest.size()) + "\r\n\r\n" + largest);
    manager.checkEndpoints(&limit, "POST /api/files/delete HTTP/1.1\r");
    TEST_ASSERT_TRUE(limit.response.find("409 Conflict") != std::string::npos);
    TEST_ASSERT_TRUE(limit.response.find("Confirmed 0 of 256 deletions") != std::string::npos);

    transport.writes.clear();
    std::string entries;
    for (int i = 0; i < 257; ++i) {
        if (i) entries += '\n';
        entries += std::to_string(i) + ".GCO";
    }
    SetupConnectionClient overflow;
    overflow.setRequest("Content-Type: text/plain\r\nContent-Length: " +
                        std::to_string(entries.size()) + "\r\n\r\n" + entries);
    manager.checkEndpoints(&overflow, "POST /api/files/delete HTTP/1.1\r");
    TEST_ASSERT_TRUE(overflow.response.find("400 Bad Request") != std::string::npos);
    TEST_ASSERT_TRUE(transport.writes.empty());
}

void testPrinterBusyDoesNotStartUpload() {
    FlashManager flash;
    flash.isSetup = true;
    PrinterTestTransport transport;
    transport.busy = true;
    MarlinClient printer(transport);
    WebServerManager manager;
    manager.flashManager = &flash;
    manager.printer = &printer;
    SetupConnectionClient upload;
    upload.setRequest("Content-Type: application/octet-stream\r\n"
                      "Content-Length: 6\r\n\r\nG1 X1\n");
    manager.checkEndpoints(&upload, "POST /upload?name=new.gcode HTTP/1.1\r");
    TEST_ASSERT_TRUE(upload.response.find("409 Conflict") != std::string::npos);
    TEST_ASSERT_EQUAL_UINT(2, transport.writes.size());
}

void testTruncatedUploadClosesPrinterFileAndReportsFailure() {
    FlashManager flash;
    flash.isSetup = true;
    PrinterTestTransport transport;
    MarlinClient printer(transport);
    WebServerManager manager;
    manager.flashManager = &flash;
    manager.printer = &printer;
    SetupConnectionClient upload;
    upload.setRequest("Content-Type: application/octet-stream\r\n"
                      "Content-Length: 10\r\n\r\nG1 X1\n");
    manager.checkEndpoints(&upload, "POST /upload?name=new.gcode HTTP/1.1\r");
    TEST_ASSERT_TRUE(upload.response.find("503 Service Unavailable") != std::string::npos);
    TEST_ASSERT_TRUE(upload.response.find("partial file") != std::string::npos);
    TEST_ASSERT_EQUAL_STRING("M29\n", transport.writes.back().c_str());
}

void testUploadRejectsDuplicateContentLengthBeforePrinterAccess() {
    FlashManager flash;
    flash.isSetup = true;
    PrinterTestTransport transport;
    MarlinClient printer(transport);
    WebServerManager manager;
    manager.flashManager = &flash;
    manager.printer = &printer;
    SetupConnectionClient upload;
    upload.setRequest("Content-Type: application/octet-stream\r\n"
                      "Content-Length: 6\r\nContent-Length: 6\r\n\r\nG1 X1\n");
    manager.checkEndpoints(&upload, "POST /upload?name=new.gcode HTTP/1.1\r");
    TEST_ASSERT_TRUE(upload.response.find("400 Bad Request") != std::string::npos);
    TEST_ASSERT_TRUE(transport.writes.empty());
}

void testDeviceInfoIsJson() {
    WiFiClientTmp client;
    client.buildHeaders();
    WebServerManager manager;
    FlashManager flash;
    flash.isSetup = true;
    manager.flashManager = &flash;
    manager.deviceId = "wireless-3d-printer-001122334455";

    TEST_ASSERT_FALSE(manager.checkEndpoints(&client, "GET /api/device HTTP/1.1\r"));
    TEST_ASSERT_EQUAL_STRING(
        "HTTP/1.1 200 OK\r\nContent-Type: application/json\r\n"
        "Cache-Control: no-store\r\nConnection: close\r\n\r\n"
        "{\"type\":\"wireless-3d-printer\",\"id\":\"wireless-3d-printer-001122334455\","
        "\"name\":\"wireless-3d-printer\",\"api_version\":1}",
        client.response.c_str());
}

void testFileOperationsAreUnavailable() {
    FlashManager flash;
    WebServerManager manager;
    manager.flashManager = &flash;
    const char *requests[] = {
        "POST /upload HTTP/1.1\r",
        "GET /api/files HTTP/1.1\r",
        "POST /api/files/delete HTTP/1.1\r",
        "DELETE /?file=test.gcode HTTP/1.1\r"
    };
    for (bool configured : {false, true}) {
        flash.isSetup = configured;
        for (const char *request : requests) {
            SetupConnectionClient client;
            client.setRequest("Host: localhost\r\n\r\nunread body");
            manager.checkEndpoints(&client, request);
            TEST_ASSERT_TRUE(client.response.find(
                configured ? "503 Service Unavailable" : "404 Not Found") !=
                std::string::npos);
            TEST_ASSERT_TRUE(client.response.find("File operations are unavailable") !=
                             std::string::npos);
            TEST_ASSERT_TRUE(client.stopped);
            TEST_ASSERT_EQUAL(0, client.index);
        }
    }
}

void testConfiguredPageShowsPrinterControls() {
    WiFiClientTmp client;
    client.buildHeaders();
    FlashManager flash;
    flash.isSetup = true;
    WebServerManager manager;
    manager.flashManager = &flash;
    manager.checkEndpoints(&client, "GET / HTTP/1.1\r");
    TEST_ASSERT_TRUE(client.response.find("Refresh printer files") != std::string::npos);
    TEST_ASSERT_TRUE(client.response.find("Upload to printer") != std::string::npos);
}

void testSetupSavesNameAndEndpointReturnsIt() {
    FlashManager flash;
    WebServerManager manager;
    manager.flashManager = &flash;
    manager.deviceId = "wireless-3d-printer-001122334455";
    WiFiClientTmp setupClient;
    setupClient.buildHeaders();
    TEST_ASSERT_TRUE(manager.checkEndpoints(
        &setupClient,
        "POST /?ssid=net&password=pwd&name=Printer+%22A%22+%5C+%C3%A9 HTTP/1.1\r"));
    TEST_ASSERT_TRUE(setupClient.response.find("204 No Content") !=
                     std::string::npos);
    FlashManager restored;
    restored.loadSetup();
    TEST_ASSERT_EQUAL_STRING("Printer \"A\" \\ \xc3\xa9", restored.deviceName.c_str());
    manager.flashManager = &restored;
    WiFiClientTmp infoClient;
    infoClient.buildHeaders();
    manager.checkEndpoints(&infoClient, "GET /api/device HTTP/1.1\r");
    TEST_ASSERT_TRUE(infoClient.response.find(
        "\"name\":\"Printer \\\"A\\\" \\\\ \xc3\xa9\"") != std::string::npos);
}

void testSetupDoesNotLogRequestHeaders() {
    FlashManager flash;
    WebServerManager manager;
    manager.flashManager = &flash;
    WiFiClientTmp client;
    client.buildSensitiveHeaders();

    TEST_ASSERT_TRUE(manager.checkEndpoints(
        &client, "POST /?ssid=net&password=dummy-secret HTTP/1.1\r"));
    Verify(OverloadedMethod(ArduinoFake(Serial), println,
                            size_t(const String &))).Never();
}

void testSetupRestartsAfterResponseEvenWithMoreClientData() {
    FlashManager flash;
    WebServerManager manager;
    SetupConnectionClient client;
    manager.flashManager = &flash;
    client.keepOpen = true;
    const std::string request = "POST /?ssid=net&name=Printer HTTP/1.1\r\n"
                                "Host: localhost\r\n\r\n";
    client.setRequest(request + "unexpected trailing data\r\n");
    int restarts = 0;
    int resetPolls = 0;
    manager.checkResetButton = [&]() { ++resetPolls; };
    manager.rf = [&]() {
        ++restarts;
        TEST_ASSERT_TRUE(client.stopped);
        TEST_ASSERT_TRUE(client.response.find("204 No Content") !=
                         std::string::npos);
        TEST_ASSERT_TRUE(flash.isSetup);
    };

    manager.handleClient(client);

    TEST_ASSERT_EQUAL(1, restarts);
    TEST_ASSERT_EQUAL(request.size(), client.index);
    TEST_ASSERT_GREATER_OR_EQUAL(CLIENT_TIMEOUT, fakeMillis);
    TEST_ASSERT_LESS_THAN(CLIENT_TIMEOUT + 20, fakeMillis);
    TEST_ASSERT_GREATER_THAN(1, resetPolls);
}

void testSetupRestartsPromptlyWhenClientDisconnects() {
    FlashManager flash;
    WebServerManager manager;
    SetupConnectionClient client;
    manager.flashManager = &flash;
    client.setRequest("POST /?ssid=net&name=Printer HTTP/1.1\r\n"
                      "Host: localhost\r\n\r\n");
    int restarts = 0;
    manager.rf = [&]() {
        ++restarts;
        TEST_ASSERT_TRUE(client.stopped);
        TEST_ASSERT_TRUE(client.response.find("204 No Content") !=
                         std::string::npos);
    };

    manager.handleClient(client);

    TEST_ASSERT_EQUAL(1, restarts);
    TEST_ASSERT_LESS_THAN(CLIENT_TIMEOUT, fakeMillis);
}

void testRejectedSetupDoesNotRestart() {
    FlashManager flash;
    WebServerManager manager;
    SetupConnectionClient client;
    manager.flashManager = &flash;
    client.setRequest("POST /?ssid=net&name= HTTP/1.1\r\n"
                      "Host: localhost\r\n\r\n");
    int restarts = 0;
    manager.rf = [&]() { ++restarts; };

    manager.handleClient(client);

    TEST_ASSERT_EQUAL(0, restarts);
    TEST_ASSERT_TRUE(client.stopped);
    TEST_ASSERT_TRUE(client.response.find("400 Bad Request") != std::string::npos);
    TEST_ASSERT_EQUAL_UINT8(0xff, eepromData[CONFIG_STATUS_ADDRESS]);
}

void testSetupRejectsInvalidNameWithoutSavingOrRestarting() {
    FlashManager flash;
    WebServerManager manager;
    manager.flashManager = &flash;
    WiFiClientTmp client;
    client.buildHeaders();
    TEST_ASSERT_FALSE(manager.checkEndpoints(
        &client, "POST /?ssid=net&name= HTTP/1.1\r"));
    TEST_ASSERT_TRUE(client.response.find("400 Bad Request") != std::string::npos);
    TEST_ASSERT_EQUAL_STRING("wireless-3d-printer", flash.deviceName.c_str());
    TEST_ASSERT_EQUAL_STRING(NETWORK_SSID, flash.ssidNetwork.c_str());
    TEST_ASSERT_EQUAL_UINT8(0xff, eepromData[CONFIG_STATUS_ADDRESS]);

    WiFiClientTmp invalidUtf8Client;
    invalidUtf8Client.buildHeaders();
    TEST_ASSERT_FALSE(manager.checkEndpoints(
        &invalidUtf8Client, "POST /?name=%FF HTTP/1.1\r"));
    TEST_ASSERT_TRUE(invalidUtf8Client.response.find("400 Bad Request") !=
                     std::string::npos);
    TEST_ASSERT_EQUAL_UINT8(0xff, eepromData[CONFIG_STATUS_ADDRESS]);

    WiFiClientTmp nullClient;
    nullClient.buildHeaders();
    TEST_ASSERT_FALSE(manager.checkEndpoints(
        &nullClient, "POST /?name=Printer%00A HTTP/1.1\r"));
    TEST_ASSERT_TRUE(nullClient.response.find("400 Bad Request") !=
                     std::string::npos);
    TEST_ASSERT_EQUAL_UINT8(0xff, eepromData[CONFIG_STATUS_ADDRESS]);
}

void testSetupPageEscapesNameAndDisplaysDefault() {
    FlashManager flash;
    WebServerManager manager;
    manager.flashManager = &flash;
    WiFiClientTmp defaultClient;
    defaultClient.buildHeaders();
    manager.checkEndpoints(&defaultClient, "GET / HTTP/1.1\r");
    TEST_ASSERT_TRUE(defaultClient.response.find("value=\"wireless-3d-printer\"") !=
                     std::string::npos);

    flash.deviceName = "A \"<&' $$SSID$$";
    WiFiClientTmp escapedClient;
    escapedClient.buildHeaders();
    manager.checkEndpoints(&escapedClient, "GET / HTTP/1.1\r");
    TEST_ASSERT_TRUE(escapedClient.response.find(
        "value=\"A &quot;&lt;&amp;&#39; $$SSID$$\"") != std::string::npos);
}

void testDeviceInfoUnavailableInSetupMode() {
    WiFiClientTmp client;
    client.buildHeaders();
    WebServerManager manager;
    FlashManager flash;
    flash.isSetup = false;
    manager.flashManager = &flash;

    TEST_ASSERT_FALSE(manager.checkEndpoints(&client, "GET /api/device HTTP/1.1\r"));
    TEST_ASSERT_TRUE(client.response.find("404 Not Found") != std::string::npos);
    TEST_ASSERT_TRUE(client.response.find("\"type\":\"wireless-3d-printer\"") == std::string::npos);
}

void testOtherGetPathsStillServeWebsite() {
    WiFiClientTmp client;
    client.buildHeaders();
    WebServerManager manager;
    FlashManager flash;
    flash.isSetup = false;
    manager.flashManager = &flash;

    TEST_ASSERT_FALSE(
        manager.checkEndpoints(&client, "GET /api/device-extra HTTP/1.1\r"));
    TEST_ASSERT_TRUE(client.response.find("200 OK") != std::string::npos);
    TEST_ASSERT_TRUE(client.response.find("Content-Type: text/html") !=
                     std::string::npos);
}

void testNetworkScanLifecycleAndJson() {
    FlashManager flash;
    WebServerManager manager;
    manager.flashManager = &flash;
    int state = NETWORK_SCAN_FAILED;
    int starts = 0;
    const std::vector<String> networks = {
        "Wi-Fi \"A\"\\", "", "\xff", "Cafe \xc3\xa9", "line\nbreak"
    };
    manager.scanComplete = [&]() { return state; };
    manager.startScan = [&]() { ++starts; state = NETWORK_SCAN_RUNNING; return true; };
    manager.scanSsid = [&](int index) { return networks[index]; };

    WiFiClientTmp startClient;
    startClient.buildHeaders();
    manager.checkEndpoints(&startClient, "POST /scan HTTP/1.1\r");
    TEST_ASSERT_TRUE(startClient.response.find("202 Accepted") != std::string::npos);
    TEST_ASSERT_EQUAL(1, starts);

    WiFiClientTmp concurrentClient;
    concurrentClient.buildHeaders();
    manager.checkEndpoints(&concurrentClient, "POST /scan HTTP/1.1\r");
    TEST_ASSERT_TRUE(concurrentClient.response.find("202 Accepted") != std::string::npos);
    TEST_ASSERT_EQUAL(1, starts);

    WiFiClientTmp pendingClient;
    pendingClient.buildHeaders();
    manager.checkEndpoints(&pendingClient, "GET /refresh-ssid HTTP/1.1\r");
    TEST_ASSERT_TRUE(pendingClient.response.find("202 Accepted") != std::string::npos);

    state = static_cast<int>(networks.size());
    WiFiClientTmp resultClient;
    resultClient.buildHeaders();
    manager.checkEndpoints(&resultClient, "GET /refresh-ssid HTTP/1.1\r");
    TEST_ASSERT_TRUE(resultClient.response.find("application/json; charset=utf-8") !=
                     std::string::npos);
    TEST_ASSERT_TRUE(resultClient.response.find(
        "[\"Wi-Fi \\\"A\\\"\\\\\",\"Cafe \xc3\xa9\",\"line\\u000abreak\"]") !=
                     std::string::npos);

    state = 0;
    WiFiClientTmp emptyClient;
    emptyClient.buildHeaders();
    manager.checkEndpoints(&emptyClient, "GET /refresh-ssid HTTP/1.1\r");
    TEST_ASSERT_TRUE(emptyClient.response.find("\r\n\r\n[]") != std::string::npos);
}

void testNetworkScanFailuresAndSetupMode() {
    FlashManager flash;
    WebServerManager manager;
    manager.flashManager = &flash;
    int calls = 0;
    manager.scanComplete = []() { return NETWORK_SCAN_FAILED; };
    manager.startScan = [&]() { ++calls; return false; };
    manager.scanSsid = [](int) { return String("unused"); };
    WiFiClientTmp startClient;
    startClient.buildHeaders();
    manager.checkEndpoints(&startClient, "POST /scan HTTP/1.1\r");
    TEST_ASSERT_TRUE(startClient.response.find("500 Internal Server Error") != std::string::npos);
    TEST_ASSERT_EQUAL(1, calls);
    WiFiClientTmp resultClient;
    resultClient.buildHeaders();
    manager.checkEndpoints(&resultClient, "GET /refresh-ssid HTTP/1.1\r");
    TEST_ASSERT_TRUE(resultClient.response.find("Wi-Fi scan failed") != std::string::npos);
    flash.isSetup = true;
    WiFiClientTmp configuredClient;
    configuredClient.buildHeaders();
    manager.checkEndpoints(&configuredClient, "POST /scan HTTP/1.1\r");
    TEST_ASSERT_TRUE(configuredClient.response.find("404 Not Found") != std::string::npos);
    TEST_ASSERT_EQUAL(1, calls);
    WiFiClientTmp configuredResult;
    configuredResult.buildHeaders();
    manager.checkEndpoints(&configuredResult, "GET /refresh-ssid HTTP/1.1\r");
    TEST_ASSERT_TRUE(configuredResult.response.find("404 Not Found") != std::string::npos);
}

void testNetworkScanCapsResults() {
    FlashManager flash;
    WebServerManager manager;
    manager.flashManager = &flash;
    int reads = 0;
    manager.scanComplete = []() { return 100; };
    manager.scanSsid = [&](int) { ++reads; return String("network"); };
    WiFiClientTmp client;
    client.buildHeaders();
    manager.checkEndpoints(&client, "GET /refresh-ssid HTTP/1.1\r");
    TEST_ASSERT_EQUAL(40, reads);

    reads = 0;
    manager.scanComplete = []() { return 300; };
    manager.scanSsid = [&](int) { ++reads; return String(""); };
    WiFiClientTmp emptyClient;
    emptyClient.buildHeaders();
    manager.checkEndpoints(&emptyClient, "GET /refresh-ssid HTTP/1.1\r");
    TEST_ASSERT_EQUAL(256, reads);
    TEST_ASSERT_TRUE(emptyClient.response.find("\r\n\r\n[]") != std::string::npos);
}

void testSetupRejectsMissingAndOversizeSsid() {
    FlashManager flash;
    WebServerManager manager;
    manager.flashManager = &flash;
    WiFiClientTmp missing;
    missing.buildHeaders();
    TEST_ASSERT_FALSE(manager.checkEndpoints(&missing, "POST /?ssid= HTTP/1.1\r"));
    TEST_ASSERT_TRUE(missing.response.find("Invalid device name, SSID") !=
                     std::string::npos);
    WiFiClientTmp oversized;
    oversized.buildHeaders();
    TEST_ASSERT_FALSE(manager.checkEndpoints(
        &oversized, "POST /?ssid=abcdefghijklmnopqrstuvwxyz1234567 HTTP/1.1\r"));
    TEST_ASSERT_TRUE(oversized.response.find("400 Bad Request") != std::string::npos);
    TEST_ASSERT_EQUAL_UINT8(0xff, eepromData[CONFIG_STATUS_ADDRESS]);
}

void testParseQueryParameters() {
    WebServerManager webServerManager;
    String query = "GET /delete?file=test.txt&other=123 HTTP/1.1";
    std::map<String, String> params =
        webServerManager.parseQueryParameters(query, 12);

    TEST_ASSERT_EQUAL(2, static_cast<int>(params.size()));
    TEST_ASSERT_EQUAL_STRING("test.txt", params["file"].c_str());
    TEST_ASSERT_EQUAL_STRING("123", params["other"].c_str());
}

void testParseQueryParametersDecodesFormValues() {
    WebServerManager webServerManager;
    String query =
        "POST /?ssid=home+network&note=tea%26coffee%3Dhot&empty= HTTP/1.1";
    std::map<String, String> params =
        webServerManager.parseQueryParameters(query, 7);

    TEST_ASSERT_EQUAL(3, static_cast<int>(params.size()));
    TEST_ASSERT_EQUAL_STRING("home network", params["ssid"].c_str());
    TEST_ASSERT_EQUAL_STRING("tea&coffee=hot", params["note"].c_str());
    TEST_ASSERT_EQUAL_STRING("", params["empty"].c_str());
}

void testParseQueryParametersPreservesMalformedEscapes() {
    WebServerManager webServerManager;
    String query = "GET /?value=bad%2&other=bad%XZ HTTP/1.1";
    std::map<String, String> params =
        webServerManager.parseQueryParameters(query, 6);

    TEST_ASSERT_EQUAL(2, static_cast<int>(params.size()));
    TEST_ASSERT_EQUAL_STRING("bad%2", params["value"].c_str());
    TEST_ASSERT_EQUAL_STRING("bad%XZ", params["other"].c_str());
}

void testParseQueryParametersHandlesEmptyQuery() {
    WebServerManager webServerManager;
    String query = "GET /? HTTP/1.1";
    std::map<String, String> params =
        webServerManager.parseQueryParameters(query, 6);

    TEST_ASSERT_TRUE(params.empty());
}

void setUp(void) {
    ArduinoFakeReset();
    fakeMillis = 0;
    std::memset(eepromData, 0xff, sizeof(eepromData));
    When(Method(ArduinoFake(Function), millis))
        .AlwaysDo([]() { return fakeMillis++; });
    When(Method(ArduinoFake(Function), delay))
        .AlwaysDo([](unsigned long duration) { fakeMillis += duration; });
    When(Method(ArduinoFake(EEPROM), write))
        .AlwaysDo([](int index, uint8_t value) { eepromData[index] = value; });
    When(Method(ArduinoFake(EEPROM), read))
        .AlwaysDo([](int index) { return eepromData[index]; });
    When(OverloadedMethod(ArduinoFake(Serial), println, size_t(const String &)))
        .AlwaysReturn(0);
    When(OverloadedMethod(ArduinoFake(Serial), println, size_t(const char *)))
        .AlwaysReturn(0);
    When(OverloadedMethod(ArduinoFake(Serial), print, size_t(const String &)))
        .AlwaysReturn(0);
    When(OverloadedMethod(ArduinoFake(Serial), print, size_t(const char *)))
        .AlwaysReturn(0);
}

void tearDown(void) {}

int runUnitTests() {
    UNITY_BEGIN();
    RUN_TEST(testPrinterStatusEndpoint);
    RUN_TEST(testDeviceInfoIsJson);
    RUN_TEST(testPrinterListAndRawUpload);
    RUN_TEST(testPrinterDeleteEndpoint);
    RUN_TEST(testBulkDeleteEndpoint);
    RUN_TEST(testBulkDeleteRejectsInvalidRequestsBeforePrinterAccess);
    RUN_TEST(testPrinterBusyDoesNotStartUpload);
    RUN_TEST(testTruncatedUploadClosesPrinterFileAndReportsFailure);
    RUN_TEST(testUploadRejectsDuplicateContentLengthBeforePrinterAccess);
    RUN_TEST(testFileOperationsAreUnavailable);
    RUN_TEST(testConfiguredPageShowsPrinterControls);
    RUN_TEST(testSetupSavesNameAndEndpointReturnsIt);
    RUN_TEST(testSetupDoesNotLogRequestHeaders);
    RUN_TEST(testSetupRestartsAfterResponseEvenWithMoreClientData);
    RUN_TEST(testSetupRestartsPromptlyWhenClientDisconnects);
    RUN_TEST(testRejectedSetupDoesNotRestart);
    RUN_TEST(testSetupRejectsInvalidNameWithoutSavingOrRestarting);
    RUN_TEST(testSetupPageEscapesNameAndDisplaysDefault);
    RUN_TEST(testDeviceInfoUnavailableInSetupMode);
    RUN_TEST(testOtherGetPathsStillServeWebsite);
    RUN_TEST(testNetworkScanLifecycleAndJson);
    RUN_TEST(testNetworkScanFailuresAndSetupMode);
    RUN_TEST(testNetworkScanCapsResults);
    RUN_TEST(testSetupRejectsMissingAndOversizeSsid);
    RUN_TEST(testParseQueryParameters);
    RUN_TEST(testParseQueryParametersDecodesFormValues);
    RUN_TEST(testParseQueryParametersPreservesMalformedEscapes);
    RUN_TEST(testParseQueryParametersHandlesEmptyQuery);
    return UNITY_END();
}

int main(void) { return runUnitTests(); }
