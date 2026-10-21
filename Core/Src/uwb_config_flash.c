/* SPDX-License-Identifier: MIT */
#include "uwb_runtime.h"
#if MUL_UNIVERSAL
#include "main.h"
#include <string.h>
extern const uint8_t __uwb_config_start__,__uwb_config_end__;
static bool geometry(void) {
  return *(volatile const uint16_t *)FLASHSIZE_BASE==1024 &&
    (uintptr_t)&__uwb_config_start__==UWB_CONFIG_FLASH_START &&
    (uintptr_t)&__uwb_config_end__==UWB_CONFIG_FLASH_END;
}
static bool read_flash(void *context,uint32_t offset,uint8_t *out,size_t n) {
  (void)context;
  if(!geometry()||offset>2*UWB_CONFIG_SECTOR_SIZE||n>2*UWB_CONFIG_SECTOR_SIZE-offset) return false;
  memcpy(out,(const void *)(uintptr_t)(UWB_CONFIG_FLASH_START+offset),n);return true;
}
static void invalidate_cache(void) {
  /* Cortex-M4 has no D-cache; invalidate the FLASH accelerator data cache. */
  __HAL_FLASH_DATA_CACHE_DISABLE();__HAL_FLASH_DATA_CACHE_RESET();__HAL_FLASH_DATA_CACHE_ENABLE();
}
static bool program_flash(void *context,uint32_t offset,uint32_t word) {
  (void)context;
  if(!geometry()||offset%4||offset>2*UWB_CONFIG_SECTOR_SIZE-4) return false;
  if(HAL_FLASH_Unlock()!=HAL_OK) return false;
  __HAL_FLASH_CLEAR_FLAG(FLASH_FLAG_EOP|FLASH_FLAG_OPERR|FLASH_FLAG_WRPERR|FLASH_FLAG_PGAERR|FLASH_FLAG_PGPERR|FLASH_FLAG_PGSERR);
  bool ok=HAL_FLASH_Program(FLASH_TYPEPROGRAM_WORD,UWB_CONFIG_FLASH_START+offset,word)==HAL_OK;
  HAL_FLASH_Lock();invalidate_cache();return ok;
}
static bool erase_flash(void *context,unsigned bank) {
  (void)context;if(!geometry()||bank>1) return false;
  if(HAL_FLASH_Unlock()!=HAL_OK) return false;
  FLASH_EraseInitTypeDef erase={.TypeErase=FLASH_TYPEERASE_SECTORS,.Banks=FLASH_BANK_1,
    .Sector=bank?FLASH_SECTOR_11:FLASH_SECTOR_10,.NbSectors=1,.VoltageRange=FLASH_VOLTAGE_RANGE_3};
  uint32_t error;
  __HAL_FLASH_CLEAR_FLAG(FLASH_FLAG_EOP|FLASH_FLAG_OPERR|FLASH_FLAG_WRPERR|FLASH_FLAG_PGAERR|FLASH_FLAG_PGPERR|FLASH_FLAG_PGSERR);
  bool ok=HAL_FLASHEx_Erase(&erase,&error)==HAL_OK;
  HAL_FLASH_Lock();invalidate_cache();return ok;
}
const uwb_config_store_t *uwb_runtime_store(void) {
  static const uwb_config_store_t store={NULL,read_flash,program_flash,erase_flash};return &store;
}
bool uwb_runtime_session(uint64_t *session) {
  /* Hardware entropy, no per-boot flash writes and no reusable compiled nonce.
   * Fail closed on clock/seed errors or stalled RNG. Run before radio startup. */
  __HAL_RCC_RNG_CLK_ENABLE();RNG->CR=RNG_CR_RNGEN;uint32_t words[2];
  for(unsigned i=0;i<2;i++) {
    unsigned spin=0;while(!(RNG->SR&RNG_SR_DRDY)&&spin<100000) spin++;
    if(spin==100000||(RNG->SR&(RNG_SR_CECS|RNG_SR_SECS))) {RNG->CR=0;return false;}
    words[i]=RNG->DR;
  }
  RNG->CR=0;*session=((uint64_t)words[0]<<32)|words[1];return *session!=0 && words[0]!=words[1];
}
#endif
