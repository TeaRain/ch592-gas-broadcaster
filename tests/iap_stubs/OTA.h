#ifndef TEST_OTA_H
#define TEST_OTA_H

#include <stdint.h>

#define IMAGE_A_START_ADD 0x1000
#define IMAGE_B_START_ADD 0x37000
#define IMAGE_A_SIZE 4096
#define IMAGE_A_FLAG 0x01
#define IMAGE_B_FLAG 0x02
#define IMAGE_IAP_FLAG 0x03
#define OTA_IMAGE_MAGIC 0x35415347
#define OTA_DATAFLASH_ADD 0x6000

typedef struct {
    unsigned char ImageFlag;
    unsigned char Revd[3];
} OTADataFlashInfo_t;

#endif
