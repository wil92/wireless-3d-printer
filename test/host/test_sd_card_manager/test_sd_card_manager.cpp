#include <ArduinoFake.h>
#include <unity.h>

#include "../../../include/hardware_pins.h"
#include "../../../lib/sdcard/sd_card_manager.h"

using namespace fakeit;

void setUp() {
    ArduinoFakeReset();
    SD_MMC = SDLib();
    When(OverloadedMethod(ArduinoFake(Serial), println, size_t(const char *)))
        .AlwaysReturn(0);
}

void tearDown() {}

void testFourBitMountUsesExpectedPinsAndDoesNotFormat() {
    SDCardManager manager;
    TEST_ASSERT_TRUE(manager.begin());
    const int expected[] = {1, 2, 4, 5, 6, 7};
    TEST_ASSERT_EQUAL_INT_ARRAY(expected, SD_MMC.pins, 6);
    TEST_ASSERT_EQUAL_STRING("/sdcard", SD_MMC.mountpoint.c_str());
    TEST_ASSERT_FALSE(SD_MMC.mode1bit);
    TEST_ASSERT_FALSE(SD_MMC.format);
    TEST_ASSERT_EQUAL(20000, SD_MMC.frequency);
    TEST_ASSERT_TRUE(manager.begin());
    TEST_ASSERT_EQUAL(1, SD_MMC.beginCalls);
    manager.end();
    manager.end();
    TEST_ASSERT_EQUAL(1, SD_MMC.endCalls);
    TEST_ASSERT_TRUE(manager.begin());
    TEST_ASSERT_EQUAL(2, SD_MMC.beginCalls);
    manager.end();
}

void testMountFailuresStopAccessAndAllowRetry() {
    SDCardManager manager;
    SD_MMC.pinsResult = false;
    TEST_ASSERT_FALSE(manager.begin());
    TEST_ASSERT_EQUAL(0, SD_MMC.beginCalls);
    SD_MMC.pinsResult = true;
    SD_MMC.beginResult = false;
    TEST_ASSERT_TRUE(manager.listFiles(true).empty());
    manager.removeFile("test", true);
    TEST_ASSERT_EQUAL(0, SD_MMC.openCalls);
    TEST_ASSERT_EQUAL(0, SD_MMC.removeCalls);
    TEST_ASSERT_EQUAL(0, SD_MMC.endCalls);
    SD_MMC.beginResult = true;
    TEST_ASSERT_TRUE(manager.begin());
    manager.end();
    TEST_ASSERT_EQUAL(1, SD_MMC.endCalls);
}

void testListingUnmountsAfterErrorsAndEnumeratesAllFiles() {
    SDCardManager manager;
    SD_MMC.rootValid = false;
    TEST_ASSERT_TRUE(manager.listFiles(true).empty());
    TEST_ASSERT_EQUAL(1, SD_MMC.endCalls);
    SD_MMC.rootValid = true;
    SD_MMC.rootDirectory = false;
    TEST_ASSERT_TRUE(manager.listFiles(true).empty());
    TEST_ASSERT_EQUAL(2, SD_MMC.endCalls);
    SD_MMC.rootDirectory = true;
    SD_MMC.rootEntries = 2;
    const std::vector<FileInfo> files = manager.listFiles(true);
    TEST_ASSERT_EQUAL(2, files.size());
    TEST_ASSERT_EQUAL_STRING("file.gcode", files[0].name.c_str());
    TEST_ASSERT_EQUAL(42, files[0].size);
    TEST_ASSERT_EQUAL(3, SD_MMC.endCalls);
}

void testControlPins() {
    TEST_ASSERT_EQUAL(8, HardwarePins::resetConfig);
    TEST_ASSERT_EQUAL(9, HardwarePins::switchSd);
}

int main() {
    UNITY_BEGIN();
    RUN_TEST(testFourBitMountUsesExpectedPinsAndDoesNotFormat);
    RUN_TEST(testMountFailuresStopAccessAndAllowRetry);
    RUN_TEST(testListingUnmountsAfterErrorsAndEnumeratesAllFiles);
    RUN_TEST(testControlPins);
    return UNITY_END();
}
