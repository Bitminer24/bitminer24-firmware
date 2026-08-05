#include <stdint.h>
#include <string.h>
#include <unity.h>

#include "../../components/bm24_identity/bm24_identity.c"

void setUp(void) {}
void tearDown(void) {}

static void test_uuid_format_is_stable(void)
{
    const uint8_t bytes[16] = {
        0x12, 0x34, 0x56, 0x78, 0x9a, 0xbc, 0x4d, 0xef,
        0x8a, 0xbc, 0xde, 0xf0, 0x12, 0x34, 0x56, 0x78
    };
    char uuid[BM24_DEVICE_ID_LENGTH + 1];
    TEST_ASSERT_TRUE(bm24_identity_format_uuid(bytes, uuid, sizeof(uuid)));
    TEST_ASSERT_EQUAL_STRING("12345678-9abc-4def-8abc-def012345678", uuid);
}

static void test_uuid_format_rejects_small_buffer(void)
{
    const uint8_t bytes[16] = {0};
    char short_buffer[BM24_DEVICE_ID_LENGTH];
    TEST_ASSERT_FALSE(bm24_identity_format_uuid(
        bytes, short_buffer, sizeof(short_buffer)));
}

int main(void)
{
    UNITY_BEGIN();
    RUN_TEST(test_uuid_format_is_stable);
    RUN_TEST(test_uuid_format_rejects_small_buffer);
    return UNITY_END();
}
