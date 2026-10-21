#define _GNU_SOURCE
#include "../../Core/Src/uwb_config_flash.c"
#include <assert.h>
#include <sys/mman.h>
#include <stdio.h>
uint16_t flashSize=1024;
rng_t rng;
unsigned cache_resets;
static unsigned locks,unlocks,programs,erases;
static bool fail;
int HAL_FLASH_Unlock(void) {unlocks++;return 0;}
int HAL_FLASH_Lock(void) {locks++;return 0;}
int HAL_FLASH_Program(uint32_t type,uint32_t address,uint64_t data) {
 assert(type==FLASH_TYPEPROGRAM_WORD&&address>=UWB_CONFIG_FLASH_START&&address<=UWB_CONFIG_FLASH_END-4&&address%4==0);programs++;
 if(fail)return -1;uint32_t *p=(uint32_t *)(uintptr_t)address;assert((*p&(uint32_t)data)==(uint32_t)data);*p&=(uint32_t)data;return 0;
}
int HAL_FLASHEx_Erase(FLASH_EraseInitTypeDef *erase,uint32_t *error) {
 assert(erase->TypeErase==FLASH_TYPEERASE_SECTORS&&erase->Banks==FLASH_BANK_1&&erase->NbSectors==1&&erase->VoltageRange==FLASH_VOLTAGE_RANGE_3);
 assert(erase->Sector==10||erase->Sector==11);erases++;*error=0;if(fail)return -1;
 memset((void *)(uintptr_t)(UWB_CONFIG_FLASH_START+(erase->Sector-10)*UWB_CONFIG_SECTOR_SIZE),255,UWB_CONFIG_SECTOR_SIZE);return 0;
}
int main(void) {
 void *mapped=mmap((void *)(uintptr_t)UWB_CONFIG_FLASH_START,2*UWB_CONFIG_SECTOR_SIZE,PROT_READ|PROT_WRITE,MAP_PRIVATE|MAP_ANONYMOUS|MAP_FIXED_NOREPLACE,-1,0);
 assert(mapped==(void *)(uintptr_t)UWB_CONFIG_FLASH_START);memset(mapped,255,2*UWB_CONFIG_SECTOR_SIZE);
 const uwb_config_store_t *store=uwb_runtime_store();uint8_t out[8];
 assert(store->read(NULL,0,out,sizeof(out)));assert(!store->read(NULL,2*UWB_CONFIG_SECTOR_SIZE-4,out,8));
 assert(!store->program(NULL,1,0));assert(!store->program(NULL,2*UWB_CONFIG_SECTOR_SIZE,0));assert(!store->erase(NULL,2));assert(!programs&&!erases&&!unlocks);
 flashSize=512;assert(!store->read(NULL,0,out,8));assert(!store->program(NULL,0,0));assert(!store->erase(NULL,0));assert(!unlocks);flashSize=1024;
 assert(store->program(NULL,0,0x11223344));assert(store->read(NULL,0,out,4)&&out[0]==0x44&&out[3]==0x11);
 assert(store->erase(NULL,0));assert(*(uint32_t *)mapped==UINT32_MAX);
 assert(store->program(NULL,2*UWB_CONFIG_SECTOR_SIZE-4,0));assert(store->erase(NULL,1));
 fail=true;assert(!store->program(NULL,0,0));assert(!store->erase(NULL,1));assert(locks==unlocks&&locks==cache_resets);
 uint64_t session=0;rng.SR=0;assert(!uwb_runtime_session(&session));rng.SR=RNG_SR_CECS|RNG_SR_DRDY;assert(!uwb_runtime_session(&session));
 rng.SR=RNG_SR_DRDY;rng.DR=0;assert(!uwb_runtime_session(&session));rng.DR=123;assert(!uwb_runtime_session(&session));assert(!rng.CR);
 assert(munmap(mapped,2*UWB_CONFIG_SECTOR_SIZE)==0);puts("actual flash backend geometry/bounds/sector/cache/locking/RNG failure checks passed");
}
