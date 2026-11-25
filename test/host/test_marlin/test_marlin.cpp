#include <unity.h>

#include <deque>
#include <cstring>
#include <string>
#include <vector>

#include "../../../lib/marlin/marlin_client.h"

class FakeTransport : public MarlinTransport {
public:
    std::deque<char> response;
    std::vector<std::string> writes;
    std::string status = "Not SD printing\nok\n";
    std::deque<std::string> statusResponses;
    std::string temperatures = "ok T:205.25 /210.00 B:60.50 /60.00\n";
    std::string listing = "Begin file list\nEnd file list\nok\n";
    std::string opening;
    std::string longUpload;
    bool showLongUpload = true;
    std::string closing = "Done saving file\nok\n";
    std::string deletion = "File deleted: TEST.GCO\nok\n";
    std::string selection = "File opened: TEST.GCO Size: 123\nFile selected\nok\n";
    std::string start = "ok\n";
    std::string dataResponse = "ok\n";
    unsigned long time = 0;
    uint32_t generation = 1;
    bool online = true;

    bool connected() override { return online; }
    bool write(const char *data, size_t length) override {
        const std::string command(data, length);
        writes.push_back(command);
        std::string reply;
        if (command == "M115\n") reply = "FIRMWARE_NAME:Marlin\nok\n";
        else if (command == "M27\n") {
            reply = statusResponses.empty() ? status : statusResponses.front();
            if (!statusResponses.empty()) statusResponses.pop_front();
        }
        else if (command == "M105\n") reply = temperatures;
        else if (command == "M20 L\n") reply = listing;
        else if (command.find("M28 ") == 0) {
            reply = opening.empty() ? "Writing to file: " + command.substr(5) + "ok\n" : opening;
            if (command.find("M28 /WRP-") == 0) longUpload = command.substr(5, command.size() - 6);
        }
        else if (command == "M29\n") {
            reply = closing;
            if (!longUpload.empty() && showLongUpload) {
                listing = "Begin file list\nWRP-ES~1.GCO 123 " + longUpload + "\nEnd file list\nok\n";
            }
            longUpload.clear();
        }
        else if (command.find("M30 ") == 0) reply = deletion;
        else if (command.find("M23 ") == 0) reply = selection;
        else if (command == "M24\n") reply = start;
        else reply = dataResponse;
        for (char c : reply) response.push_back(c);
        return true;
    }
    int read(unsigned long timeout) override {
        if (response.empty()) { time += timeout; return -1; }
        const char c = response.front();
        response.pop_front();
        ++time;
        return static_cast<unsigned char>(c);
    }
    unsigned long now() override { return time; }
    uint32_t session() override { return generation; }
};

class SelectedFileTransport : public FakeTransport {
public:
    bool started = false;
    bool write(const char *data, size_t length) override {
        const std::string command(data, length);
        const bool sent = FakeTransport::write(data, length);
        if (command.find("M23 ") == 0) status = "SD printing byte 0/123\nok\n";
        if (command == "M24\n") started = true;
        return sent;
    }
};

class RecoveryTransport : public FakeTransport {
public:
    bool timeoutFirmware = false;
    bool respondToMarker = true;
    std::string beforeMarker;
    std::string afterMarker = "ok\n";
    std::string marker;

    void enqueue(const std::string &text) {
        for (char c : text) response.push_back(c);
    }

    bool write(const char *data, size_t length) override {
        const std::string command(data, length);
        if (command == "M115\n" && timeoutFirmware) {
            writes.push_back(command);
            enqueue(" T:25.34 /0.00 B:25.57 /55.00 W:?\n");
            return true;
        }
        if (command.find("M118 ") == 0) {
            writes.push_back(command);
            marker = command.substr(5, command.size() - 6);
            enqueue(beforeMarker);
            if (respondToMarker) enqueue(marker + "\n" + afterMarker);
            return true;
        }
        return FakeTransport::write(data, length);
    }
};

void testStatusTimeoutRecoversWithoutAcceptingStaleReplies() {
    RecoveryTransport transport;
    transport.timeoutFirmware = true;
    MarlinClient client(transport);
    MarlinStatus status;
    TEST_ASSERT_FALSE(client.getStatus(status));
    transport.timeoutFirmware = false;
    transport.respondToMarker = false;
    transport.beforeMarker = "ok\nFIRMWARE_NAME:Marlin\nok\n";
    std::vector<MarlinFile> files;
    TEST_ASSERT_FALSE(client.listFiles(files));
    TEST_ASSERT_EQUAL_UINT(2, transport.writes.size());
    TEST_ASSERT_TRUE(transport.writes.back().find("M118 W3DP_SYNC_") == 0);
    const std::string marker = transport.marker;
    transport.beforeMarker = "W3DP_SYNC_OLD\nok\n" + marker + "\nok\n";
    transport.respondToMarker = true;
    TEST_ASSERT_TRUE(client.listFiles(files));
    TEST_ASSERT_TRUE(marker != transport.marker);
    TEST_ASSERT_EQUAL_UINT(5, transport.writes.size());
    TEST_ASSERT_EQUAL_STRING("M115\n", transport.writes[3].c_str());
    TEST_ASSERT_EQUAL_STRING("M20 L\n", transport.writes[4].c_str());
}

void testStatusRecoveryWaitsForMarkerAcknowledgement() {
    RecoveryTransport transport;
    transport.timeoutFirmware = true;
    MarlinClient client(transport);
    MarlinStatus status;
    TEST_ASSERT_FALSE(client.getStatus(status));
    transport.timeoutFirmware = false;
    transport.afterMarker = "";
    TEST_ASSERT_FALSE(client.getStatus(status));
    TEST_ASSERT_EQUAL_UINT(2, transport.writes.size());
    const std::string oldMarker = transport.marker;
    transport.beforeMarker = "ok\n" + oldMarker + "\nok\n";
    transport.afterMarker = "ok\n";
    TEST_ASSERT_TRUE(client.getStatus(status));
    TEST_ASSERT_FALSE(status.sdPrinting);
    TEST_ASSERT_TRUE(oldMarker != transport.marker);
    TEST_ASSERT_EQUAL_STRING("M115\n", transport.writes[3].c_str());
}

void testStatusRecoveryRetriesMarkerDroppedByAbort() {
    RecoveryTransport transport;
    transport.timeoutFirmware = true;
    MarlinClient client(transport);
    MarlinStatus status;
    TEST_ASSERT_FALSE(client.getStatus(status));
    transport.timeoutFirmware = false;
    transport.respondToMarker = false;
    TEST_ASSERT_FALSE(client.getStatus(status));
    const std::string droppedMarker = transport.marker;
    transport.respondToMarker = true;
    TEST_ASSERT_TRUE(client.getStatus(status));
    TEST_ASSERT_TRUE(droppedMarker != transport.marker);
    TEST_ASSERT_FALSE(status.sdPrinting);
}

void testStatusTimeoutRecoveryForSdAndTemperature() {
    for (int which = 0; which < 2; ++which) {
        RecoveryTransport transport;
        if (which == 0) transport.status = "";
        else transport.temperatures = "";
        MarlinClient client(transport);
        MarlinStatus status;
        TEST_ASSERT_FALSE(client.getStatus(status));
        transport.status = "Not SD printing\nok\n";
        transport.temperatures = "ok T:20 B:21\n";
        TEST_ASSERT_TRUE(client.getStatus(status));
        TEST_ASSERT_FALSE(status.sdPrinting);
        TEST_ASSERT_FLOAT_WITHIN(0.001, 20, status.nozzleTemperature);
    }
}

void testStatusRecoveryRejectsChangedSessionAndUnsafeOperations() {
    RecoveryTransport transport;
    transport.timeoutFirmware = true;
    MarlinClient client(transport);
    MarlinStatus status;
    TEST_ASSERT_FALSE(client.getStatus(status));
    ++transport.generation;
    const size_t before = transport.writes.size();
    TEST_ASSERT_FALSE(client.getStatus(status));
    TEST_ASSERT_EQUAL_UINT(before, transport.writes.size());
    TEST_ASSERT_FALSE(client.getStatus(status));
    TEST_ASSERT_EQUAL_UINT(before, transport.writes.size());

    RecoveryTransport uploadTransport;
    MarlinClient uploadClient(uploadTransport);
    std::string target;
    TEST_ASSERT_TRUE(uploadClient.beginUpload("test.gcode", target));
    uploadTransport.closing = "";
    TEST_ASSERT_FALSE(uploadClient.finishUpload());
    const size_t uploadBefore = uploadTransport.writes.size();
    TEST_ASSERT_FALSE(uploadClient.getStatus(status));
    TEST_ASSERT_EQUAL_UINT(uploadBefore, uploadTransport.writes.size());

    RecoveryTransport printTransport;
    printTransport.listing = "Begin file list\nTEST.GCO 123\nEnd file list\nok\n";
    printTransport.start = "";
    MarlinClient printClient(printTransport);
    TEST_ASSERT_FALSE(printClient.startPrint("TEST.GCO"));
    const size_t printBefore = printTransport.writes.size();
    TEST_ASSERT_FALSE(printClient.getStatus(status));
    TEST_ASSERT_EQUAL_UINT(printBefore, printTransport.writes.size());

    RecoveryTransport errorTransport;
    errorTransport.status = "Error:printer fault\n";
    MarlinClient errorClient(errorTransport);
    TEST_ASSERT_FALSE(errorClient.getStatus(status));
    const size_t errorBefore = errorTransport.writes.size();
    TEST_ASSERT_FALSE(errorClient.getStatus(status));
    TEST_ASSERT_EQUAL_UINT(errorBefore, errorTransport.writes.size());
}

void testStartPrintRequiresIdleListedAndConfirmedFile() {
    FakeTransport transport;
    transport.listing = "Begin file list\nDIR/TEST.GCO 123 Test.gcode\nEnd file list\nok\n";
    MarlinClient client(transport);
    TEST_ASSERT_FALSE(client.startPrint("../TEST.GCO"));
    TEST_ASSERT_FALSE(client.startPrint(std::string(89, 'A')));
    TEST_ASSERT_TRUE(transport.writes.empty());
    TEST_ASSERT_FALSE(client.startPrint("MISSING.GCO"));
    transport.status = "SD printing byte 1/100\nok\n";
    TEST_ASSERT_FALSE(client.startPrint("DIR/TEST.GCO"));
    transport.status = "Not SD printing\nok\n";
    transport.selection = "File opened: OTHER.GCO Size: 123\nFile selected\nok\n";
    TEST_ASSERT_FALSE(client.startPrint("DIR/TEST.GCO"));
    transport.selection = "File opened: TEST.GCO Size: 124\nFile selected\nok\n";
    TEST_ASSERT_FALSE(client.startPrint("DIR/TEST.GCO"));
    transport.selection = "File opened: TEST.GCO Size: 123\nok\n";
    TEST_ASSERT_FALSE(client.startPrint("DIR/TEST.GCO"));
    for (const auto &command : transport.writes) TEST_ASSERT_TRUE(command != "M24\n");
    transport.selection = "File opened: TEST.GCO Size: 123\nFile selected\nok\n";
    TEST_ASSERT_TRUE(client.startPrint("dir/test.gco"));
    TEST_ASSERT_EQUAL_STRING("M24\n", transport.writes.back().c_str());
    TEST_ASSERT_EQUAL_STRING("M23 /DIR/TEST.GCO\n", transport.writes[transport.writes.size() - 2].c_str());
    TEST_ASSERT_EQUAL_STRING("M27\n", transport.writes[transport.writes.size() - 3].c_str());
}

void testStartPrintStopsIfStatusChanges() {
    FakeTransport transport;
    transport.listing = "Begin file list\nTEST.GCO 123\nEnd file list\nok\n";
    transport.statusResponses = {"Not SD printing\nok\n", "SD printing byte 1/100\nok\n"};
    MarlinClient client(transport);
    TEST_ASSERT_FALSE(client.startPrint("TEST.GCO"));
    for (const auto &command : transport.writes) {
        TEST_ASSERT_TRUE(command.find("M23 ") != 0 && command != "M24\n");
    }
}

void testStartPrintWhenSelectedFileReportsBusy() {
    SelectedFileTransport transport;
    transport.listing = "Begin file list\nTEST.GCO 123\nEnd file list\nok\n";
    transport.selection = "echo: Now fresh file: /TEST.GCO\n"
                          "File opened: TEST.GCO Size: 123\nFile selected\nok\n";
    MarlinClient client(transport);
    TEST_ASSERT_TRUE(client.startPrint("TEST.GCO"));
    TEST_ASSERT_TRUE(transport.started);
    TEST_ASSERT_EQUAL_STRING("M23 /TEST.GCO\n", transport.writes[transport.writes.size() - 2].c_str());
    TEST_ASSERT_EQUAL_STRING("M24\n", transport.writes.back().c_str());
    MarlinStatus status;
    TEST_ASSERT_TRUE(client.getStatus(status));
    TEST_ASSERT_TRUE(status.sdPrinting);
}

void testStartPrintTimeoutIsUncertain() {
    FakeTransport transport;
    transport.listing = "Begin file list\nTEST.GCO 123\nEnd file list\nok\n";
    transport.start = "";
    MarlinClient client(transport);
    TEST_ASSERT_FALSE(client.startPrint("TEST.GCO"));
    TEST_ASSERT_NOT_NULL(std::strstr(client.error().c_str(), "may have started"));
    TEST_ASSERT_FALSE(client.startPrint("TEST.GCO"));
}

void testPrinterStatusAndTemperatureResponses() {
    FakeTransport transport;
    MarlinClient client(transport);
    MarlinStatus status;
    TEST_ASSERT_TRUE(client.getStatus(status));
    TEST_ASSERT_FALSE(status.sdPrinting);
    TEST_ASSERT_TRUE(status.hasNozzleTemperature);
    TEST_ASSERT_TRUE(status.hasBedTemperature);
    TEST_ASSERT_FLOAT_WITHIN(0.001, 205.25, status.nozzleTemperature);
    TEST_ASSERT_FLOAT_WITHIN(0.001, 60.5, status.bedTemperature);
    transport.status = "SD printing byte 10/100\nok\n";
    transport.temperatures = "T:20.0 /0\nok\n";
    TEST_ASSERT_TRUE(client.getStatus(status));
    TEST_ASSERT_TRUE(status.sdPrinting);
    TEST_ASSERT_FALSE(status.hasBedTemperature);
    transport.temperatures = "ok T:nan B:40\n";
    TEST_ASSERT_FALSE(client.getStatus(status));
    transport.temperatures = "ok T:20 B:broken\n";
    TEST_ASSERT_FALSE(client.getStatus(status));
    transport.status = "Not SD printing\nSD printing byte 1/100\nok\n";
    TEST_ASSERT_FALSE(client.getStatus(status));
    transport.status = "ok\n";
    TEST_ASSERT_FALSE(client.getStatus(status));
}

class StrictUploadTransport : public FakeTransport {
public:
    uint32_t nextLine = 1;
    bool saving = false;
    bool protocolError = false;

    bool write(const char *data, size_t length) override {
        const std::string command(data, length);
        if (!response.empty()) protocolError = true;
        if (command == "N0 M110 N0*125\n") {
            nextLine = 1;
        } else if (command.find("M28 ") == 0) {
            saving = true;
        } else if (command == "M29\n") {
            saving = false;
        } else if (saving) {
            const std::string prefix = "N" + std::to_string(nextLine) + " ";
            const size_t star = command.rfind('*');
            unsigned char checksum = 0;
            if (star != std::string::npos) {
                for (size_t i = 0; i < star; ++i) {
                    checksum ^= static_cast<unsigned char>(command[i]);
                }
            }
            if (command.compare(0, prefix.size(), prefix) != 0 ||
                star == std::string::npos ||
                command.substr(star) != "*" + std::to_string(checksum) + "\n") {
                protocolError = true;
                writes.push_back(command);
                for (char c : std::string("Error:invalid numbered line\n")) response.push_back(c);
                return true;
            }
            ++nextLine;
        }
        return FakeTransport::write(data, length);
    }
};

void testSuccessfulTransferUsesCheckedLines() {
    FakeTransport transport;
    MarlinClient client(transport);
    std::string target;
    TEST_ASSERT_TRUE(client.beginUpload("test print.gcode", target));
    TEST_ASSERT_EQUAL_STRING("TEST_PRI.GCO", target.c_str());
    TEST_ASSERT_TRUE(client.uploadLine("G1 X10 ; comment\r"));
    TEST_ASSERT_TRUE(client.uploadLine("; comment only"));
    TEST_ASSERT_TRUE(client.finishUpload());
    TEST_ASSERT_EQUAL_STRING("N0 M110 N0*125\n", transport.writes[5].c_str());
    TEST_ASSERT_EQUAL_STRING("M28 /TEST_PRI.GCO\n", transport.writes[6].c_str());
    TEST_ASSERT_EQUAL_STRING("N1 G1 X10*80\n", transport.writes[7].c_str());
    TEST_ASSERT_EQUAL_STRING("M29\n", transport.writes[8].c_str());
}

void testLongUploadNameAndCollisions() {
    FakeTransport transport;
    MarlinClient client(transport);
    std::string target;
    TEST_ASSERT_TRUE(client.beginUpload("WRP-ESP32_S3_SUPERMINI_BOX.gcode", target, true));
    TEST_ASSERT_EQUAL_STRING("WRP-ESP32_S3_SUPERMINI_BOX.GCO", target.c_str());
    TEST_ASSERT_EQUAL_STRING("M28 /WRP-ESP32_S3_SUPERMINI_BOX.GCO\n", transport.writes.back().c_str());
    TEST_ASSERT_TRUE(client.finishUpload());

    transport.listing = "Begin file list\nWRP-ES~1.GCO 123 wrp-esp32_s3_supermini_box.gco\n"
                        "End file list\nok\n";
    TEST_ASSERT_FALSE(client.beginUpload("WRP-ESP32_S3_SUPERMINI_BOX.gcode", target, true));
    TEST_ASSERT_TRUE(client.error().find("already exists") != std::string::npos);
    transport.listing = "Begin file list\nWRP-ES~1.GCO 123\nEnd file list\nok\n";
    TEST_ASSERT_FALSE(client.beginUpload("WRP-OTHER.gcode", target, true));
    TEST_ASSERT_TRUE(client.error().find("Cannot safely check") != std::string::npos);

    const std::string valid = "WRP-" + std::string(81, 'A') + ".gcode";
    transport.listing = "Begin file list\nEnd file list\nok\n";
    TEST_ASSERT_TRUE(client.beginUpload(valid, target, true));
    TEST_ASSERT_TRUE(client.finishUpload());
    transport.showLongUpload = false;
    transport.listing = "Begin file list\nEnd file list\nok\n";
    TEST_ASSERT_TRUE(client.beginUpload("WRP-MISSING.gcode", target, true));
    TEST_ASSERT_FALSE(client.finishUpload());
    TEST_ASSERT_TRUE(client.error().find("did not confirm") != std::string::npos);
    const size_t before = transport.writes.size();
    TEST_ASSERT_FALSE(client.beginUpload("WRP-" + std::string(82, 'A') + ".gcode", target, true));
    TEST_ASSERT_FALSE(client.beginUpload("WRP-abc.gcode", target, true));
    TEST_ASSERT_FALSE(client.beginUpload("WRP-A/FILE.gcode", target, true));
    TEST_ASSERT_FALSE(client.beginUpload("WRP-.gcode", target, true));
    TEST_ASSERT_EQUAL_UINT(before, transport.writes.size());
}

void testExampleAndSequentialCheckedLinesAcrossUploads() {
    StrictUploadTransport transport;
    MarlinClient client(transport);
    std::string target;
    TEST_ASSERT_TRUE(client.beginUpload("ttt.gcode", target));
    TEST_ASSERT_TRUE(client.uploadLine("M117 TRANSFER TEST"));
    TEST_ASSERT_TRUE(client.uploadLine("; ignored"));
    TEST_ASSERT_TRUE(client.uploadLine("  G1 X10 ; comment\r"));
    for (int i = 0; i < 10; ++i) TEST_ASSERT_TRUE(client.uploadLine("G1 X1"));
    TEST_ASSERT_TRUE(client.finishUpload());
    TEST_ASSERT_EQUAL_STRING("M28 /TTT.GCO\n", transport.writes[6].c_str());
    TEST_ASSERT_EQUAL_STRING("N1 M117 TRANSFER TEST*56\n", transport.writes[7].c_str());
    TEST_ASSERT_EQUAL_STRING("N2 G1 X10*83\n", transport.writes[8].c_str());
    TEST_ASSERT_EQUAL_UINT32(13, transport.nextLine);
    TEST_ASSERT_FALSE(transport.protocolError);

    TEST_ASSERT_TRUE(client.beginUpload("other.gcode", target));
    TEST_ASSERT_TRUE(client.uploadLine("M117 TRANSFER TEST"));
    TEST_ASSERT_EQUAL_STRING("N1 M117 TRANSFER TEST*56\n", transport.writes.back().c_str());
    TEST_ASSERT_TRUE(client.finishUpload());
    TEST_ASSERT_FALSE(transport.protocolError);
}

void testRejectsBusyAndUnconfirmedStatus() {
    FakeTransport transport;
    MarlinClient client(transport);
    std::string target;
    transport.status = "SD printing byte 10/100\nok\n";
    TEST_ASSERT_FALSE(client.beginUpload("test.gcode", target));
    TEST_ASSERT_EQUAL_UINT(2, transport.writes.size());
    transport.status = "ok\n";
    TEST_ASSERT_FALSE(client.beginUpload("test.gcode", target));
    transport.status = "Not SD printing\nSD printing byte 1/100\nok\n";
    TEST_ASSERT_FALSE(client.beginUpload("test.gcode", target));
}

void testRejectsCollisionsAndIncompleteFileLists() {
    FakeTransport transport;
    MarlinClient client(transport);
    std::string target;
    transport.listing = "Begin file list\ntest_pri.gco 123\nEnd file list\nok\n";
    TEST_ASSERT_FALSE(client.beginUpload("test print.gcode", target));
    TEST_ASSERT_EQUAL_UINT(4, transport.writes.size());
    transport.listing = "Begin file list\nok\n";
    TEST_ASSERT_FALSE(client.beginUpload("other.gcode", target));
}

void testRequiresOpenAndCloseConfirmation() {
    FakeTransport transport;
    MarlinClient client(transport);
    std::string target;
    transport.opening = "ok\n";
    TEST_ASSERT_FALSE(client.beginUpload("test.gcode", target));
    TEST_ASSERT_FALSE(client.beginUpload("test.gcode", target));

    FakeTransport wrong;
    MarlinClient wrongClient(wrong);
    wrong.opening = "Writing to file: OTHER.GCO\nok\n";
    TEST_ASSERT_FALSE(wrongClient.beginUpload("test.gcode", target));

    FakeTransport other;
    MarlinClient otherClient(other);
    TEST_ASSERT_TRUE(otherClient.beginUpload("test.gcode", target));
    other.closing = "ok\n";
    TEST_ASSERT_FALSE(otherClient.finishUpload());
    TEST_ASSERT_FALSE(otherClient.beginUpload("other.gcode", target));
}

void testRejectsDangerousAndOversizedLines() {
    FakeTransport transport;
    MarlinClient client(transport);
    std::string target;
    TEST_ASSERT_TRUE(client.beginUpload("test.gcode", target));
    const size_t before = transport.writes.size();
    TEST_ASSERT_FALSE(client.uploadLine("M29"));
    TEST_ASSERT_FALSE(client.uploadLine("M110 N0"));
    TEST_ASSERT_FALSE(client.uploadLine("G1 X1*12"));
    TEST_ASSERT_FALSE(client.uploadLine(std::string(100, 'X')));
    TEST_ASSERT_EQUAL_UINT(before, transport.writes.size());
    client.abortUpload();
    TEST_ASSERT_EQUAL_STRING("M29\n", transport.writes.back().c_str());
}

void testPrinterErrorsAreNotAcknowledgements() {
    FakeTransport transport;
    MarlinClient client(transport);
    std::string target;
    TEST_ASSERT_TRUE(client.beginUpload("test.gcode", target));
    transport.dataResponse = "Error:checksum mismatch\nResend:1\nok\n";
    TEST_ASSERT_FALSE(client.uploadLine("G1 X1"));
    TEST_ASSERT_EQUAL_STRING("Printer reported: Error:checksum mismatch", client.error().c_str());
    const size_t afterError = transport.writes.size();
    TEST_ASSERT_FALSE(client.uploadLine("G1 X2"));
    TEST_ASSERT_FALSE(client.finishUpload());
    TEST_ASSERT_EQUAL_UINT(afterError, transport.writes.size());
    TEST_ASSERT_TRUE(client.uploading());
    client.abortUpload();
}

void testReconnectDuringUploadDoesNotSendToAnotherPrinter() {
    FakeTransport transport;
    MarlinClient client(transport);
    std::string target;
    TEST_ASSERT_TRUE(client.beginUpload("test.gcode", target));
    const size_t before = transport.writes.size();
    ++transport.generation;
    TEST_ASSERT_FALSE(client.uploadLine("G1 X1"));
    client.abortUpload();
    TEST_ASSERT_EQUAL_UINT(before, transport.writes.size());
    TEST_ASSERT_FALSE(client.beginUpload("other.gcode", target));
}

void testMalformedListFailsClosed() {
    FakeTransport transport;
    MarlinClient client(transport);
    std::vector<MarlinFile> files;
    transport.listing = "Begin file list\nfile.gco bad\nEnd file list\nok\n";
    TEST_ASSERT_FALSE(client.listFiles(files));
    const size_t before = transport.writes.size();
    TEST_ASSERT_FALSE(client.listFiles(files));
    TEST_ASSERT_EQUAL_UINT(before, transport.writes.size());
}

void testLongNamesAndShortNameFallback() {
    FakeTransport transport;
    MarlinClient client(transport);
    std::vector<MarlinFile> files;
    transport.listing = "Begin file list\nDIR/FILE~1.GCO 123 Models/My print \xc3\xa9.gcode\n"
                        "OLD.GCO 42\nEnd file list\nok\n";
    TEST_ASSERT_TRUE(client.listFiles(files));
    TEST_ASSERT_EQUAL_UINT(2, files.size());
    TEST_ASSERT_EQUAL_STRING("Models/My print \xc3\xa9.gcode", files[0].name.c_str());
    TEST_ASSERT_EQUAL_STRING("DIR/FILE~1.GCO", files[0].path.c_str());
    TEST_ASSERT_EQUAL_STRING("OLD.GCO", files[1].name.c_str());
    TEST_ASSERT_EQUAL_STRING("OLD.GCO", files[1].path.c_str());
}

void testDeletesOnlyListedFileWhenIdleAndConfirmed() {
    FakeTransport transport;
    transport.listing = "Begin file list\nTEST.GCO 12 Friendly name.gcode\nEnd file list\nok\n";
    MarlinClient client(transport);
    TEST_ASSERT_FALSE(client.deleteFile("../TEST.GCO"));
    TEST_ASSERT_TRUE(transport.writes.empty());
    TEST_ASSERT_FALSE(client.deleteFile("OTHER.GCO"));
    TEST_ASSERT_TRUE(client.deleteFile("TEST.GCO"));
    TEST_ASSERT_EQUAL_STRING("M30 /TEST.GCO\n", transport.writes.back().c_str());
    transport.deletion = "Deletion failed, File: TEST.GCO.\nok\n";
    TEST_ASSERT_FALSE(client.deleteFile("TEST.GCO"));
    transport.deletion = "File deleted: TEST.GCO.OTHER\nok\n";
    TEST_ASSERT_FALSE(client.deleteFile("TEST.GCO"));
    transport.status = "SD printing byte 1/100\nok\n";
    const size_t before = transport.writes.size();
    TEST_ASSERT_FALSE(client.deleteFile("TEST.GCO"));
    TEST_ASSERT_EQUAL_UINT(before + 2, transport.writes.size());
}

void testAllowsM290ButRejectsM29() {
    FakeTransport transport;
    MarlinClient client(transport);
    std::string target;
    TEST_ASSERT_TRUE(client.beginUpload("test.gcode", target));
    TEST_ASSERT_TRUE(client.uploadLine("M290 Z0.05"));
    TEST_ASSERT_FALSE(client.uploadLine("M29 S1"));
    TEST_ASSERT_FALSE(client.uploadLine("M290 M29"));
    client.abortUpload();
}

bool logsContain(const std::vector<std::string> &logs, const std::string &text) {
    for (const std::string &line : logs) {
        if (line.find(text) != std::string::npos) return true;
    }
    return false;
}

void testPrintDiagnosticsIdentifyEachIdleCheck() {
    const char *phases[] = {"before_listing", "before_selection"};
    for (size_t busyAt = 0; busyAt < 2; ++busyAt) {
        FakeTransport transport;
        transport.listing = "Begin file list\nTEST.GCO 123\nEnd file list\nok\n";
        for (size_t i = 0; i < busyAt; ++i) transport.statusResponses.push_back("Not SD printing\nok\n");
        transport.statusResponses.push_back("SD printing byte 0/123\nok\n");
        MarlinClient client(transport);
        std::vector<std::string> logs;
        client.setDiagnosticLogger([&logs](const std::string &line) { logs.push_back(line); });
        TEST_ASSERT_FALSE(client.startPrint("TEST.GCO"));
        TEST_ASSERT_TRUE(logsContain(logs, "print_begin path=TEST.GCO session=1"));
        TEST_ASSERT_TRUE(logsContain(logs, std::string("print_reply phase=") + phases[busyAt] +
                                           " command=M27 text=SD printing byte 0/123"));
        TEST_ASSERT_TRUE(logsContain(logs, std::string("print_idle phase=") + phases[busyAt] +
                                           " result=failed detail=Printer did not confirm an idle SD state."));
        TEST_ASSERT_TRUE(logsContain(logs, std::string("print_failed phase=") + phases[busyAt]));
        TEST_ASSERT_FALSE(logsContain(logs, "command=M24 result=sending"));
    }
}

void testPrintDiagnosticsSelectionFailureAndSanitization() {
    FakeTransport transport;
    transport.listing = "Begin file list\nTEST.GCO 123\nEnd file list\nok\n";
    transport.selection = "File opened: OTHER.GCO Size: 123\nunexpected\t" +
                          std::string(100, 'x') + "\nFile selected\nok\n";
    MarlinClient client(transport);
    std::vector<std::string> logs;
    client.setDiagnosticLogger([&logs](const std::string &line) { logs.push_back(line); });
    TEST_ASSERT_FALSE(client.startPrint("TEST.GCO"));
    TEST_ASSERT_TRUE(logsContain(logs, "print_reply phase=selection command=M23 text=File opened: OTHER.GCO Size: 123"));
    TEST_ASSERT_TRUE(logsContain(logs, "command=M23 text=unexpected?"));
    TEST_ASSERT_TRUE(logsContain(logs, "..."));
    TEST_ASSERT_TRUE(logsContain(logs, "print_selection open=no selected=yes"));
    TEST_ASSERT_TRUE(logsContain(logs, "print_failed phase=selection_confirmation"));
    TEST_ASSERT_FALSE(logsContain(logs, "command=M24 result=sending"));
}

void testPrintDiagnosticsStartAcknowledgementAndTimeout() {
    FakeTransport transport;
    transport.listing = "Begin file list\nTEST.GCO 123\nEnd file list\nok\n";
    MarlinClient client(transport);
    std::vector<std::string> logs;
    client.setDiagnosticLogger([&logs](const std::string &line) { logs.push_back(line); });
    TEST_ASSERT_TRUE(client.startPrint("TEST.GCO"));
    TEST_ASSERT_TRUE(logsContain(logs, "print_idle phase=before_selection result=idle"));
    TEST_ASSERT_TRUE(logsContain(logs, "print_selection open=yes selected=yes"));
    TEST_ASSERT_TRUE(logsContain(logs, "print_command command=M24 result=sending"));
    TEST_ASSERT_TRUE(logsContain(logs, "print_command command=M24 result=acknowledged"));
    logs.clear();
    transport.start = "";
    TEST_ASSERT_FALSE(client.startPrint("TEST.GCO"));
    TEST_ASSERT_TRUE(logsContain(logs, "print_failed phase=start detail=Print may have started."));
    TEST_ASSERT_TRUE(logsContain(logs, "uncertain=yes"));
    TEST_ASSERT_FALSE(logsContain(logs, "result=acknowledged"));
}

void testPrintDiagnosticsReportEarlyAndSelectionFailures() {
    FakeTransport transport;
    MarlinClient client(transport);
    std::vector<std::string> logs;
    client.setDiagnosticLogger([&logs](const std::string &line) { logs.push_back(line); });
    TEST_ASSERT_FALSE(client.startPrint("BAD\nPATH"));
    TEST_ASSERT_TRUE(logsContain(logs, "print_begin path=BAD?PATH"));
    TEST_ASSERT_TRUE(logsContain(logs, "print_failed phase=preflight"));
    logs.clear();
    transport.online = false;
    TEST_ASSERT_FALSE(client.startPrint("TEST.GCO"));
    TEST_ASSERT_TRUE(logsContain(logs, "print_failed phase=synchronize detail=Printer is not connected."));
    logs.clear();
    transport.online = true;
    transport.listing = "Begin file list\nTEST.GCO 123\nEnd file list\nok\n";
    transport.selection = "File opened: TEST.GCO Size: 123";
    TEST_ASSERT_FALSE(client.startPrint("TEST.GCO"));
    TEST_ASSERT_TRUE(logsContain(logs, "command=M23 incomplete=File opened: TEST.GCO Size: 123"));
    TEST_ASSERT_TRUE(logsContain(logs, "print_failed phase=selection_response detail=Printer response timed out."));
    TEST_ASSERT_FALSE(logsContain(logs, "command=M24 result=sending"));
}

void testUploadTimeoutDiagnosticsAndAbortFailure() {
    FakeTransport transport;
    MarlinClient client(transport);
    std::vector<std::string> logs;
    client.setDiagnosticLogger([&logs](const std::string &line) { logs.push_back(line); });
    std::string target;
    TEST_ASSERT_TRUE(client.beginUpload("test.gcode", target));
    transport.dataResponse = "start\nbooting\n";
    transport.closing = "";
    TEST_ASSERT_FALSE(client.uploadLine("G1 X12345"));
    client.abortUpload();

    TEST_ASSERT_TRUE(logsContain(logs, "upload_open target=TEST.GCO"));
    TEST_ASSERT_TRUE(logsContain(logs, "reply command=numbered:G1 line=1 text=start"));
    TEST_ASSERT_TRUE(logsContain(logs, "wait_failed command=numbered:G1 line=1"));
    TEST_ASSERT_TRUE(logsContain(logs, "reason=Printer response timed out. last=booting"));
    TEST_ASSERT_TRUE(logsContain(logs, "upload_abort_result closed=no"));
    TEST_ASSERT_FALSE(logsContain(logs, "X12345"));
    TEST_ASSERT_EQUAL_STRING("Printer response timed out.", client.error().c_str());
    std::vector<MarlinFile> files;
    TEST_ASSERT_FALSE(client.listFiles(files));
}

void testUploadAbortSuccessAndSanitizedResponses() {
    FakeTransport transport;
    MarlinClient client(transport);
    std::vector<std::string> logs;
    client.setDiagnosticLogger([&logs](const std::string &line) { logs.push_back(line); });
    std::string target;
    TEST_ASSERT_TRUE(client.beginUpload("test.gcode", target));
    transport.dataResponse = std::string("Error:bad\t") + std::string(100, 'x') + "\n";
    TEST_ASSERT_FALSE(client.uploadLine("G1 X1"));
    client.abortUpload();
    TEST_ASSERT_TRUE(logsContain(logs, "rejected command=numbered:G1 line=1 text=Error:bad?"));
    TEST_ASSERT_TRUE(logsContain(logs, "upload_abort_result closed=yes detail=ok"));
    std::vector<MarlinFile> files;
    TEST_ASSERT_TRUE(client.listFiles(files));
}

void testCloseTimeoutDiagnosticsAndReplyLimit() {
    FakeTransport transport;
    MarlinClient client(transport);
    std::vector<std::string> logs;
    client.setDiagnosticLogger([&logs](const std::string &line) { logs.push_back(line); });
    std::string target;
    TEST_ASSERT_TRUE(client.beginUpload("test.gcode", target));
    transport.dataResponse = "busy: processing\nok\n";
    for (int i = 0; i < 12; ++i) TEST_ASSERT_TRUE(client.uploadLine("G1 X1"));
    size_t replies = 0;
    for (const std::string &line : logs) {
        if (line.find("reply command=numbered:G1") != std::string::npos) ++replies;
    }
    TEST_ASSERT_EQUAL_UINT(8, replies);
    transport.closing = "saving\n";
    TEST_ASSERT_FALSE(client.finishUpload());
    TEST_ASSERT_TRUE(logsContain(logs, "upload_close line=13"));
    TEST_ASSERT_TRUE(logsContain(logs, "wait_failed command=M29 line=13"));
    TEST_ASSERT_TRUE(logsContain(logs, "last=saving"));
    std::vector<MarlinFile> files;
    TEST_ASSERT_FALSE(client.listFiles(files));
}

void setUp() {}
void tearDown() {}
int main() {
    UNITY_BEGIN();
    RUN_TEST(testPrinterStatusAndTemperatureResponses);
    RUN_TEST(testStatusTimeoutRecoversWithoutAcceptingStaleReplies);
    RUN_TEST(testStatusRecoveryWaitsForMarkerAcknowledgement);
    RUN_TEST(testStatusRecoveryRetriesMarkerDroppedByAbort);
    RUN_TEST(testStatusTimeoutRecoveryForSdAndTemperature);
    RUN_TEST(testStatusRecoveryRejectsChangedSessionAndUnsafeOperations);
    RUN_TEST(testStartPrintRequiresIdleListedAndConfirmedFile);
    RUN_TEST(testStartPrintStopsIfStatusChanges);
    RUN_TEST(testStartPrintWhenSelectedFileReportsBusy);
    RUN_TEST(testStartPrintTimeoutIsUncertain);
    RUN_TEST(testPrintDiagnosticsIdentifyEachIdleCheck);
    RUN_TEST(testPrintDiagnosticsSelectionFailureAndSanitization);
    RUN_TEST(testPrintDiagnosticsStartAcknowledgementAndTimeout);
    RUN_TEST(testPrintDiagnosticsReportEarlyAndSelectionFailures);
    RUN_TEST(testSuccessfulTransferUsesCheckedLines);
    RUN_TEST(testLongUploadNameAndCollisions);
    RUN_TEST(testExampleAndSequentialCheckedLinesAcrossUploads);
    RUN_TEST(testRejectsBusyAndUnconfirmedStatus);
    RUN_TEST(testRejectsCollisionsAndIncompleteFileLists);
    RUN_TEST(testRequiresOpenAndCloseConfirmation);
    RUN_TEST(testRejectsDangerousAndOversizedLines);
    RUN_TEST(testPrinterErrorsAreNotAcknowledgements);
    RUN_TEST(testReconnectDuringUploadDoesNotSendToAnotherPrinter);
    RUN_TEST(testMalformedListFailsClosed);
    RUN_TEST(testLongNamesAndShortNameFallback);
    RUN_TEST(testDeletesOnlyListedFileWhenIdleAndConfirmed);
    RUN_TEST(testAllowsM290ButRejectsM29);
    RUN_TEST(testUploadTimeoutDiagnosticsAndAbortFailure);
    RUN_TEST(testUploadAbortSuccessAndSanitizedResponses);
    RUN_TEST(testCloseTimeoutDiagnosticsAndReplyLimit);
    return UNITY_END();
}
