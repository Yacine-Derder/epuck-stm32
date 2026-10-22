#include "uwb_config.h"
#include <assert.h>
#include <math.h>
#include <stdio.h>
#include <string.h>
static uint8_t flash[2*UWB_CONFIG_SECTOR_SIZE],backup[sizeof(flash)];
static int budget=-1;static unsigned writes,erases;
static bool read_mem(void *ctx,uint32_t at,uint8_t *out,size_t n) {
 (void)ctx;assert(at<=sizeof(flash)&&n<=sizeof(flash)-at);memcpy(out,flash+at,n);return true;
}
static bool program(void *ctx,uint32_t at,uint32_t word) {
 (void)ctx;assert(at%4==0 && at+4<=sizeof(flash));
 if(budget==0) return false;if(budget>0)budget--;
 for(unsigned i=0;i<4;i++) {uint8_t v=(uint8_t)(word>>(8*i));assert((flash[at+i]&v)==v);flash[at+i]&=v;}writes++;return true;
}
static bool erase(void *ctx,unsigned bank) {
 (void)ctx;assert(bank<2);if(budget==0) {memset(flash+bank*UWB_CONFIG_SECTOR_SIZE,255,123);return false;}
 if(budget>0)budget--;memset(flash+bank*UWB_CONFIG_SECTOR_SIZE,255,UWB_CONFIG_SECTOR_SIZE);erases++;return true;
}
static const uwb_config_store_t store={NULL,read_mem,program,erase};
int main(void) {
 uwb_config_t a,b,c;uwb_config_defaults(&a);assert(!uwb_config_validate(&a));uint32_t gen=0;
 assert(uwb_config_crc((const uint8_t *)"123456789",9)==0xCBF43926);
 uint8_t record[128];uwb_config_encode(record,&a,UINT32_MAX);
 assert(uwb_config_decode(record,&b,&gen)&&gen==UINT32_MAX&&uwb_config_equal(&a,&b));
 for(unsigned i=0;i<sizeof(record);i++) {record[i]^=1;assert(!uwb_config_decode(record,&b,&gen));record[i]^=1;}
 const char *bad[]={"","-1","+132","0x","0x100","0x86junk","4294967296"," 132","132 ","1e2"};
 for(unsigned i=0;i<sizeof(bad)/sizeof(*bad);i++){b=a;assert(uwb_config_set(&b,"node",bad[i]));assert(uwb_config_equal(&b,&a));}
 b=a;assert(uwb_config_set(&b,"peer","0x00"));assert(uwb_config_set(&b,"mode","6"));assert(uwb_config_set(&b,"phy","1"));
 assert(uwb_config_set(&b,"tdma","1"));assert(!uwb_config_set(&b,"node","0x85"));assert(!uwb_config_set(&b,"identity","1"));assert(!uwb_config_set(&b,"role","1"));
 for(unsigned node=0;node<256;node++) for(unsigned peer=0;peer<256;peer++) {
   uwb_config_t full=a;full.node=node;full.peer=peer;full.peers[0].address=(uint8_t)peer;
   assert((uwb_config_validate(&full)==NULL)==(node!=peer));
 }
 b=a;assert(!uwb_config_set(&b,"ADDRESS","0XFF")&&b.node==255);
 assert(!uwb_config_set(&b,"PEER","0")&&b.peer==0);
 assert(!uwb_config_set(&b,"mode","TwR")&&b.mode==1);
 assert(!uwb_config_set(&b,"role","INITIATOR")&&b.role==1);
 assert(!uwb_config_set(&b,"identity","AlR")&&b.identity==1);
 assert(!uwb_config_set(&b,"telemetry","ON")&&b.telemetry==1);
 assert(!uwb_config_set(&b,"calibration","OFF")&&!b.calibration);
 assert(!uwb_config_set(&b,"verbosity","summary")&&b.verbosity==1);
 /* Existing v1 records retain old values; defaults change without a migration write. */
 c=a;c.node=0x86;c.peer=0x84;uwb_config_encode(record,&c,42);record[4]=1;memset(record+76,0,44);
 uint32_t legacyCrc=uwb_config_crc(record,120);for(unsigned i=0;i<4;i++)record[120+i]=(uint8_t)(legacyCrc>>(8*i));
 assert(uwb_config_decode(record,&b,&gen)&&gen==42&&b.node==0x86&&b.peer==0x84);
 double derived;assert(uwb_config_range(&a,1.5,true,&derived)&&derived==1.5);
 assert(!uwb_config_set(&a,"correction_mm","-500"));assert(uwb_config_range(&a,1.5,true,&derived)&&derived==1.5);
 a.calibration=1;assert(uwb_config_range(&a,1.5,true,&derived)&&derived==1);
 assert(!uwb_config_range(&a,-.1,false,&derived)&&derived==-.6);
 a.correction_mm=500;assert(!uwb_config_range(&a,-.1,false,&derived)&&derived==.4);
 a.correction_mm=-500;assert(!uwb_config_range(&a,.1,true,&derived)&&derived<0);
 assert(!uwb_config_range(&a,NAN,true,&derived));
 a.calibration=1;assert(!uwb_config_set(&a,"tx_ant_dtu","16381")&&!a.calibration);
 a.calibration=1;assert(!uwb_config_set(&a,"rx_ant_dtu","16382")&&!a.calibration);
 memset(flash,255,sizeof(flash));assert(!uwb_config_load(&store,&c,&gen));assert(uwb_config_save(&store,&a,&gen)&&gen==1);
 assert(uwb_config_load(&store,&c,&gen)&&uwb_config_equal(&a,&c));unsigned old=writes;
 assert(uwb_config_save(&store,&a,&gen)&&old==writes);
 memcpy(backup,flash,sizeof(flash));
 for(int fail=0;fail<=32;fail++) {
  memcpy(flash,backup,sizeof(flash));budget=fail;bool ok=uwb_config_save(&store,&b,&gen);budget=-1;
  assert(uwb_config_load(&store,&c,&gen));assert(uwb_config_equal(&c,ok?&b:&a));
 }
 memcpy(flash,backup,sizeof(flash));
 /* Force rollover; sector0 full of the same valid record, sector1 dirty. */
 for(unsigned at=128;at<UWB_CONFIG_SECTOR_SIZE;at+=128)memcpy(flash+at,flash,128);
 memset(flash+UWB_CONFIG_SECTOR_SIZE,0,UWB_CONFIG_SECTOR_SIZE);memcpy(backup,flash,sizeof(flash));
 for(int fail=0;fail<=33;fail++) {
  memcpy(flash,backup,sizeof(flash));budget=fail;bool ok=uwb_config_save(&store,&b,&gen);budget=-1;
  assert(uwb_config_load(&store,&c,&gen));assert(uwb_config_equal(&c,ok?&b:&a));
 }
 /* A complete first save supersedes UINT32_MAX across generation wrap. */
 memset(flash,255,sizeof(flash));uwb_config_encode(flash,&a,UINT32_MAX);
 assert(uwb_config_save(&store,&b,&gen)&&gen==0);assert(uwb_config_load(&store,&c,&gen)&&gen==0&&uwb_config_equal(&c,&b));
 /* Unsupported version with a valid CRC is still rejected. */
 flash[128+4]=3;uint32_t crc=uwb_config_crc(flash+128,120);
 for(unsigned i=0;i<4;i++)flash[128+120+i]=(uint8_t)(crc>>(8*i));
 assert(uwb_config_load(&store,&c,&gen)&&gen==UINT32_MAX&&uwb_config_equal(&c,&a));
 /* Last slot appends without erase; only the next save rotates/erases.
  * This models 1024 records without a slow repeated full-sector scan loop. */
 memset(flash,255,sizeof(flash));
 for(unsigned at=0;at<UWB_CONFIG_SECTOR_SIZE-128;at+=128)uwb_config_encode(flash+at,&a,at/128+1);
 unsigned eraseBefore=erases;
 assert(uwb_config_save(&store,&b,&gen)&&gen==1024&&erases==eraseBefore);
 assert(uwb_config_save(&store,&a,&gen)&&gen==1025&&erases==eraseBefore+1);
 assert(uwb_config_load(&store,&c,&gen)&&uwb_config_equal(&c,&a));
 printf("configuration CRC/schema/validation/calibration/journal/power-cut tests passed (%u erases)\n",erases);
}
