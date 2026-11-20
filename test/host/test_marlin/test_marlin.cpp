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
        else if (command == "M20\n") reply = listing;
        else if (command.find("M28 ") == 0) {
            reply = opening.empty() ? "Writing to file: " + command.substr(5) + "ok\n" : opening;
        }
        else if (command == "M29\n") reply = closing;
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
    return UNITY_END();
}
