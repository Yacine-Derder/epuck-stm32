#ifndef RUNTIME_FLASH_TEST_MAIN_H
#define RUNTIME_FLASH_TEST_MAIN_H
#include <stdint.h>
#include <stddef.h>
extern uint16_t flashSize;
#define FLASHSIZE_BASE ((uintptr_t)&flashSize)
#define HAL_OK 0
#define FLASH_FLAG_EOP 1
#define FLASH_FLAG_OPERR 2
#define FLASH_FLAG_WRPERR 4
#define FLASH_FLAG_PGAERR 8
#define FLASH_FLAG_PGPERR 16
#define FLASH_FLAG_PGSERR 32
#define FLASH_TYPEPROGRAM_WORD 2
#define FLASH_TYPEERASE_SECTORS 1
#define FLASH_BANK_1 1
#define FLASH_SECTOR_10 10
#define FLASH_SECTOR_11 11
#define FLASH_VOLTAGE_RANGE_3 3
#define __HAL_FLASH_DATA_CACHE_DISABLE() ((void)0)
#define __HAL_FLASH_DATA_CACHE_RESET() cache_resets++
#define __HAL_FLASH_DATA_CACHE_ENABLE() ((void)0)
#define __HAL_FLASH_CLEAR_FLAG(flags) ((void)(flags))
#define __HAL_RCC_RNG_CLK_ENABLE() ((void)0)
#define RNG_CR_RNGEN 4u
#define RNG_SR_DRDY 1u
#define RNG_SR_CECS 2u
#define RNG_SR_SECS 4u
typedef struct {uint32_t CR,SR,DR;} rng_t;
extern rng_t rng;
#define RNG (&rng)
extern unsigned cache_resets;
typedef struct {uint32_t TypeErase,Banks,Sector,NbSectors,VoltageRange;} FLASH_EraseInitTypeDef;
int HAL_FLASH_Unlock(void);
int HAL_FLASH_Lock(void);
int HAL_FLASH_Program(uint32_t type,uint32_t address,uint64_t data);
int HAL_FLASHEx_Erase(FLASH_EraseInitTypeDef *erase,uint32_t *error);
#endif
