#include <unity.h>
#include <string>

#include "../../../lib/debug_log/debug_address.h"

void testValidAddresses() {
    std::string url;
    TEST_ASSERT_TRUE(debugAddressUrl("192.168.1.10", url));
    TEST_ASSERT_EQUAL_STRING("http://192.168.1.10:8080/", url.c_str());
    TEST_ASSERT_TRUE(debugAddressUrl("logs.local:9000", url));
    TEST_ASSERT_EQUAL_STRING("http://logs.local:9000/", url.c_str());
    TEST_ASSERT_TRUE(debugAddressUrl("HOST:65535", url));
    TEST_ASSERT_EQUAL_STRING("http://HOST:65535/", url.c_str());
}

void testInvalidAddresses() {
    const std::string invalid[] = {
        "", ":8080", "host:", "host:0", "host:65536", "host:123456",
        "http://host", "host/path", "host?x", "user@host", "host\n",
        "host:12:34", std::string(129, 'a')
    };
    for (const auto &address : invalid) {
        std::string url;
        TEST_ASSERT_FALSE(debugAddressUrl(address, url));
    }
}

void setUp() {}
void tearDown() {}

int main() {
    UNITY_BEGIN();
    RUN_TEST(testValidAddresses);
    RUN_TEST(testInvalidAddresses);
    return UNITY_END();
}
