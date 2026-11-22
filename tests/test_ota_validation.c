#include <stdint.h>
#include <stdio.h>

#define EEPROM_BLOCK_SIZE 4096
#include "ota_validation.h"

static int failures;

#define CHECK(condition) do { \
    if (!(condition)) { \
        printf("FAIL line %d: %s\n", __LINE__, #condition); \
        failures++; \
    } \
} while (0)

static void test_command_frames(void)
{
    CHECK(OTA_CommandFrameValid(CMD_IAP_PROM, 132, 128));
    CHECK(OTA_CommandFrameValid(CMD_IAP_VERIFY, 68, 64));
    CHECK(OTA_CommandFrameValid(CMD_IAP_ERASE, 6, 0));
    CHECK(OTA_CommandFrameValid(CMD_IAP_END, 2, 0));
    CHECK(OTA_CommandFrameValid(CMD_IAP_INFO, 2, 0));
    CHECK(OTA_CommandFrameValid(CMD_IAP_CRC32, 2, 0));
    CHECK(OTA_CommandFrameValid(CMD_IAP_SET_GAS, 8, 0));
    CHECK(OTA_CommandFrameValid(CMD_IAP_SET_THRESHOLD, 6, 0));

    CHECK(!OTA_CommandFrameValid(CMD_IAP_PROM, 0, 0));
    CHECK(!OTA_CommandFrameValid(CMD_IAP_PROM, 3, 0));
    CHECK(!OTA_CommandFrameValid(CMD_IAP_PROM, 4, 0));
    CHECK(!OTA_CommandFrameValid(CMD_IAP_PROM, 132, 127));
    CHECK(!OTA_CommandFrameValid(CMD_IAP_PROM, IAP_LEN + 1, IAP_LEN - 3));
    CHECK(!OTA_CommandFrameValid(CMD_IAP_VERIFY, 4, 0));
    CHECK(!OTA_CommandFrameValid(CMD_IAP_ERASE, 5, 0));
    CHECK(!OTA_CommandFrameValid(CMD_IAP_INFO, 2, 1));
    CHECK(!OTA_CommandFrameValid(CMD_IAP_SET_GAS, 6, 0));
    CHECK(!OTA_CommandFrameValid(CMD_IAP_SET_THRESHOLD, 8, 0));
    CHECK(!OTA_CommandFrameValid(0x7F, 2, 0));
}

static void test_image_ranges(void)
{
    CHECK(OTA_ImageRangeValid(IMAGE_B_START_ADD, 1));
    CHECK(OTA_ImageRangeValid(IMAGE_B_START_ADD, IMAGE_B_SIZE));
    CHECK(OTA_ImageRangeValid(IMAGE_IAP_START_ADD - 128, 128));
    CHECK(!OTA_ImageRangeValid(IMAGE_B_START_ADD, 0));
    CHECK(!OTA_ImageRangeValid(IMAGE_A_START_ADD, 16));
    CHECK(!OTA_ImageRangeValid(IMAGE_A_START_ADD + IMAGE_A_SIZE - 4096, 16));
    CHECK(!OTA_ImageRangeValid(IMAGE_IAP_START_ADD, 16));
    CHECK(!OTA_ImageRangeValid(IMAGE_IAP_START_ADD - 128, 129));
    CHECK(!OTA_ImageRangeValid(UINT32_MAX, 32));
}

static void test_erase_ranges(void)
{
    CHECK(OTA_EraseRangeValid(IMAGE_B_START_ADD, IMAGE_B_SIZE / FLASH_BLOCK_SIZE));
    CHECK(OTA_EraseRangeValid(IMAGE_IAP_START_ADD - FLASH_BLOCK_SIZE, 1));
    CHECK(!OTA_EraseRangeValid(IMAGE_B_START_ADD, 0));
    CHECK(!OTA_EraseRangeValid(IMAGE_B_START_ADD - FLASH_BLOCK_SIZE, 1));
    CHECK(!OTA_EraseRangeValid(IMAGE_IAP_START_ADD, 1));
    CHECK(!OTA_EraseRangeValid(IMAGE_B_START_ADD, IMAGE_B_SIZE / FLASH_BLOCK_SIZE + 1));
    CHECK(!OTA_EraseRangeValid(IMAGE_B_START_ADD + 16, 1));
}

static void test_threshold_magic_offset(void)
{
    uint8_t command[8] = {CMD_IAP_SET_THRESHOLD, 0, 100, 0, 0xA5, 0x5A, 0, 0};
    CHECK(OTA_ThresholdMagicValid(command, 6));
    command[4] = 0;
    command[5] = 0;
    command[6] = 0xA5;
    command[7] = 0x5A;
    CHECK(!OTA_ThresholdMagicValid(command, 6));
}

int main(void)
{
    test_command_frames();
    test_image_ranges();
    test_erase_ranges();
    test_threshold_magic_offset();
    if (failures) {
        printf("%d OTA validation checks failed\n", failures);
        return 1;
    }
    puts("All OTA validation checks passed");
    return 0;
}
