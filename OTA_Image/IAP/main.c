/********************************** (C) COPYRIGHT *******************************
 * File Name          : main.c
 * Author             : WCH
 * Version            : V1.1
 * Date               : 2019/11/05
 * Description        : 判断标志以及搬运代码到APP代码区
 *********************************************************************************
 * Copyright (c) 2021 Nanjing Qinheng Microelectronics Co., Ltd.
 * Attention: This software (modified or not) and binary are used for 
 * microcontroller manufactured by Nanjing Qinheng Microelectronics.
 *******************************************************************************/

/******************************************************************************/
/* 头文件包含 */
#include "CH59x_common.h"
#include "OTA.h"

/* 记录当前的Image */
unsigned char CurrImageFlag = 0xff;

/* flash的数据临时存储 */
__attribute__((aligned(8))) uint8_t block_buf[16];

#define jumpApp    ((void (*)(void))((int *)IMAGE_A_START_ADD))

/*********************************************************************
 * GLOBAL TYPEDEFS
 */

/*********************************************************************
 * @fn      SwitchImageFlag
 *
 * @brief   切换dataflash里的ImageFlag
 *
 * @param   new_flag    - 切换的ImageFlag
 *
 * @return  none
 */
void SwitchImageFlag(uint8_t new_flag)
{
    uint16_t i;
    uint32_t ver_flag;

    /* 读取第一块 */
    EEPROM_READ(OTA_DATAFLASH_ADD, (uint32_t *)&block_buf[0], 4);

    /* 擦除第一块 */
    EEPROM_ERASE(OTA_DATAFLASH_ADD, EEPROM_PAGE_SIZE);

    /* 更新Image信息 */
    block_buf[0] = new_flag;

    /* 编程DataFlash */
    EEPROM_WRITE(OTA_DATAFLASH_ADD, (uint32_t *)&block_buf[0], 4);
}

uint8_t IAP_CopyBackupImage(void)
{
    __attribute__((aligned(8))) uint8_t flash_data[1024];
    __attribute__((aligned(8))) uint8_t check_a[256];
    __attribute__((aligned(8))) uint8_t check_b[256];
    uint32_t offset;
    uint16_t i;
    uint8_t block;

    if(FLASH_ROM_ERASE(IMAGE_A_START_ADD, IMAGE_A_SIZE) != 0)
    {
        return 0;
    }

    for(block = 0; block < IMAGE_A_SIZE / 1024; block++)
    {
        FLASH_ROM_READ(IMAGE_B_START_ADD + ((uint32_t)block * 1024), flash_data, 1024);
        if(FLASH_ROM_WRITE(IMAGE_A_START_ADD + ((uint32_t)block * 1024), flash_data, 1024) != 0)
        {
            return 0;
        }
    }

    for(offset = 0; offset < IMAGE_A_SIZE; offset += 256)
    {
        FLASH_ROM_READ(IMAGE_A_START_ADD + offset, check_a, 256);
        FLASH_ROM_READ(IMAGE_B_START_ADD + offset, check_b, 256);
        for(i = 0; i < 256; i++)
        {
            if(check_a[i] != check_b[i])
            {
                return 0;
            }
        }
    }
    return 1;
}

/*********************************************************************
 * @fn      jump_APP
 *
 * @brief   切换APP程序
 *
 * @return  none
 */
void jump_APP(void)
{
    uint32_t first_word = 0;

    if(CurrImageFlag == IMAGE_IAP_FLAG)
    {
        /* Image B 有效性检查：末尾 16 字节由 OTA 客户端写入镜像标记 */
        FLASH_ROM_READ(IMAGE_B_START_ADD + IMAGE_A_SIZE - 16, &first_word, 4);
        if(first_word == OTA_IMAGE_MAGIC)
        {
            if(!IAP_CopyBackupImage())
            {
                /* Keep IMAGE_IAP_FLAG and Image B so the next boot retries. */
                SYS_ResetExecute();
                return;
            }
            SwitchImageFlag(IMAGE_A_FLAG);
            /* 拷贝完成后擦除备份区 */
            FLASH_ROM_ERASE(IMAGE_B_START_ADD, IMAGE_A_SIZE);
        }
        else
        {
            /* Image B 不是有效镜像，放弃拷贝，继续运行 Image A */
            SwitchImageFlag(IMAGE_A_FLAG);
        }
    }
    jumpApp();
}

/*********************************************************************
 * @fn      ReadImageFlag
 *
 * @brief   读取当前的程序的Image标志，DataFlash如果为空，就默认是ImageA
 *
 * @return  none
 */
void ReadImageFlag(void)
{
    OTADataFlashInfo_t p_image_flash;

    EEPROM_READ(OTA_DATAFLASH_ADD, &p_image_flash, 4);
    CurrImageFlag = p_image_flash.ImageFlag;

    /* 程序第一次执行，或者没有更新过，以后更新后在擦除DataFlash */
    if((CurrImageFlag != IMAGE_A_FLAG) && (CurrImageFlag != IMAGE_B_FLAG) && (CurrImageFlag != IMAGE_IAP_FLAG))
    {
        CurrImageFlag = IMAGE_A_FLAG;
    }

    PRINT("Image Flag %02x\n", CurrImageFlag);
}

/*********************************************************************
 * @fn      main
 *
 * @brief   主函数
 *
 * @return  none
 */
int main(void)
{
#if(defined(DCDC_ENABLE)) && (DCDC_ENABLE == TRUE)
    PWR_DCDCCfg(ENABLE);
#endif
    SetSysClock(CLK_SOURCE_PLL_60MHz);
#if(defined(HAL_SLEEP)) && (HAL_SLEEP == TRUE)
    GPIOA_ModeCfg(GPIO_Pin_All, GPIO_ModeIN_PU);
    GPIOB_ModeCfg(GPIO_Pin_All, GPIO_ModeIN_PU);
#endif
#ifdef DEBUG
    GPIOA_SetBits(bTXD1);
    GPIOA_ModeCfg(bTXD1, GPIO_ModeOut_PP_5mA);
    UART1_DefInit();
#endif
    ReadImageFlag();
    jump_APP();
}

/******************************** endfile @ main ******************************/
