#ifndef TEST_CH59X_COMMON_H
#define TEST_CH59X_COMMON_H

#include <stdint.h>
#include <string.h>

#define EEPROM_PAGE_SIZE 4096
#define EEPROM_BLOCK_SIZE 4096
#define CLK_SOURCE_PLL_60MHz 0
#define GPIO_Pin_All 0
#define GPIO_ModeIN_PU 0
#define PRINT(...)

void FLASH_ROM_READ(uint32_t address, void *buffer, uint32_t length);
int FLASH_ROM_ERASE(uint32_t address, uint32_t length);
int FLASH_ROM_WRITE(uint32_t address, const void *buffer, uint32_t length);
int EEPROM_ERASE(uint32_t address, uint32_t length);
int EEPROM_WRITE(uint32_t address, void *buffer, uint32_t length);
void EEPROM_READ(uint32_t address, void *buffer, uint32_t length);

static inline void PWR_DCDCCfg(int enabled) { (void)enabled; }
static inline void SetSysClock(int source) { (void)source; }
static inline void GPIOA_ModeCfg(int pins, int mode) { (void)pins; (void)mode; }
static inline void GPIOB_ModeCfg(int pins, int mode) { (void)pins; (void)mode; }
static inline void SYS_ResetExecute(void) { }

#endif
