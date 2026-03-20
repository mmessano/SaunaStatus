#include <unity.h>
#include "auth_logic.h"

void setUp(void) {}
void tearDown(void) {}

void test_auth_logic_header_compiles(void) {
    // Verify struct is at least as large as its members (padding may make it larger)
    TEST_ASSERT_TRUE(sizeof(AuthSession) >= 65 + 33 + 17 + sizeof(uint32_t) + sizeof(bool));
    TEST_ASSERT_EQUAL(0, AUTH_SRC_NVS);
}

int main(int argc, char **argv) {
    UNITY_BEGIN();
    RUN_TEST(test_auth_logic_header_compiles);
    return UNITY_END();
}
