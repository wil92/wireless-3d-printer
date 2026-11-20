#include <unity.h>

#include "../../../include/hardware_pins.h"

void testNativeUsbPinsDoNotConflictWithResetButton() {
    TEST_ASSERT_EQUAL_INT(19, HardwarePins::usbDataMinus);
    TEST_ASSERT_EQUAL_INT(20, HardwarePins::usbDataPlus);
    TEST_ASSERT_NOT_EQUAL(HardwarePins::resetConfig, HardwarePins::usbDataMinus);
    TEST_ASSERT_NOT_EQUAL(HardwarePins::resetConfig, HardwarePins::usbDataPlus);
}

void setUp() {}
void tearDown() {}

int main() {
    UNITY_BEGIN();
    RUN_TEST(testNativeUsbPinsDoNotConflictWithResetButton);
    return UNITY_END();
}
