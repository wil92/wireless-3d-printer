#include <unity.h>

#include <deque>
#include <string>
#include <vector>

#include "../../../lib/marlin/marlin_client.h"

class FakeTransport : public MarlinTransport {
public:
    std::deque<char> response;
    std::vector<std::string> writes;
    std::string status = "Not SD printing\nok\n";
    std::string listing = "Begin file list\nEnd file list\nok\n";
    std::string opening;
    std::string closing = "Done saving file\nok\n";
    std::string deletion = "File deleted: TEST.GCO\nok\n";
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
        else if (command == "M27\n") reply = status;
        else if (command == "M20 L\n") reply = listing;
        else if (command.find("M28 ") == 0) {
            reply = opening.empty() ? "Writing to file: " + command.substr(5) + "ok\n" : opening;
        }
        else if (command == "M29\n") reply = closing;
        else if (command.find("M30 ") == 0) reply = deletion;
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
    RUN_TEST(testSuccessfulTransferUsesCheckedLines);
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
