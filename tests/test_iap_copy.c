#include <stdint.h>
#include <stdio.h>
#include <string.h>

#define main iap_firmware_main
#include "../OTA_Image/IAP/main.c"
#undef main

static uint8_t image_a[IMAGE_A_SIZE];
static uint8_t image_b[IMAGE_A_SIZE];
static int fail_erase;
static int fail_write_block = -1;
static int corrupt_write_block = -1;
static int erase_b_calls;
static int write_calls;

void FLASH_ROM_READ(uint32_t address, void *buffer, uint32_t length)
{
    if (address >= IMAGE_A_START_ADD && address < IMAGE_A_START_ADD + IMAGE_A_SIZE) {
        memcpy(buffer, image_a + address - IMAGE_A_START_ADD, length);
    } else if (address >= IMAGE_B_START_ADD && address < IMAGE_B_START_ADD + IMAGE_A_SIZE) {
        memcpy(buffer, image_b + address - IMAGE_B_START_ADD, length);
    } else {
        memset(buffer, 0xFF, length);
    }
}

int FLASH_ROM_ERASE(uint32_t address, uint32_t length)
{
    if (fail_erase) return 1;
    if (address == IMAGE_A_START_ADD) {
        memset(image_a, 0xFF, length);
    } else if (address == IMAGE_B_START_ADD) {
        erase_b_calls++;
        memset(image_b, 0xFF, length);
    }
    return 0;
}

int FLASH_ROM_WRITE(uint32_t address, const void *buffer, uint32_t length)
{
    int block = (int)((address - IMAGE_A_START_ADD) / 1024);
    write_calls++;
    if (block == fail_write_block) return 1;
    memcpy(image_a + address - IMAGE_A_START_ADD, buffer, length);
    if (block == corrupt_write_block) image_a[address - IMAGE_A_START_ADD] ^= 0x80;
    return 0;
}

int EEPROM_ERASE(uint32_t address, uint32_t length)
{
    (void)address; (void)length;
    return 0;
}

int EEPROM_WRITE(uint32_t address, void *buffer, uint32_t length)
{
    (void)address; (void)buffer; (void)length;
    return 0;
}

void EEPROM_READ(uint32_t address, void *buffer, uint32_t length)
{
    (void)address;
    memset(buffer, 0, length);
}

static void reset_fixture(void)
{
    int i;
    for (i = 0; i < IMAGE_A_SIZE; i++) image_b[i] = (uint8_t)(i * 13 + 7);
    memset(image_a, 0, sizeof(image_a));
    fail_erase = 0;
    fail_write_block = -1;
    corrupt_write_block = -1;
    erase_b_calls = 0;
    write_calls = 0;
}

static int expect(int condition, const char *message)
{
    if (!condition) {
        printf("FAIL: %s\n", message);
        return 1;
    }
    return 0;
}

int main(void)
{
    int failures = 0;

    reset_fixture();
    failures += expect(IAP_CopyBackupImage() == 1, "valid copy must succeed");
    failures += expect(memcmp(image_a, image_b, IMAGE_A_SIZE) == 0, "A must equal B");
    failures += expect(write_calls == IMAGE_A_SIZE / 1024, "all blocks must be written");

    reset_fixture();
    fail_erase = 1;
    failures += expect(IAP_CopyBackupImage() == 0, "erase failure must be reported");
    failures += expect(write_calls == 0, "erase failure must prevent writes");

    reset_fixture();
    fail_write_block = 2;
    failures += expect(IAP_CopyBackupImage() == 0, "write failure must be reported");
    failures += expect(erase_b_calls == 0, "write failure must preserve B");

    reset_fixture();
    corrupt_write_block = 1;
    failures += expect(IAP_CopyBackupImage() == 0, "post-copy mismatch must be reported");
    failures += expect(erase_b_calls == 0, "mismatch must preserve B");

    if (failures) return 1;
    puts("IAP copy fault-injection checks passed");
    return 0;
}
