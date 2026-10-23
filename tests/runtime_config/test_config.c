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
 assert(a.slot_ms==10);
 const char *slots[]={"10","50","500"};
 for(unsigned i=0;i<3;i++) {
  b=a;assert(!uwb_config_set(&b,"slot_ms",slots[i]));uwb_config_encode(record,&b,3);
  assert(uwb_config_decode(record,&c,&gen)&&uwb_config_equal(&b,&c));
 }
 b=a;assert(uwb_config_set(&b,"slot_ms","20")&&uwb_config_equal(&b,&a));
 /* Schema4 migration preserves the old500ms timing and unrelated values. */
 b=a;b.slot_ms=500;uwb_config_encode(record,&b,4);record[4]=4;record[19]&=7;
 uint32_t crc4=uwb_config_crc(record,120);for(unsigned i=0;i<4;i++)record[120+i]=(uint8_t)(crc4>>(8*i));
 assert(uwb_config_decode(record,&c,&gen)&&uwb_config_equal(&b,&c));
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
 c=a;c.node=0x86;c.peer=0x84;uwb_config_encode(record,&c,42);record[4]=1;record[17]=record[18]=record[19]=0;memset(record+76,0,44);
 uint32_t legacyCrc=uwb_config_crc(record,120);for(unsigned i=0;i<4;i++)record[120+i]=(uint8_t)(legacyCrc>>(8*i));
 assert(uwb_config_decode(record,&b,&gen)&&gen==42&&b.node==0x86&&b.peer==0x84);
 /* Genuine v2 node word has no packed roster bytes. Loading is read-only. */
 c=a;c.slot_ms=500;c.wifi=1;c.calibration=1;c.correction_mm=-4321;
 uwb_config_encode(record,&c,43);record[4]=2;record[17]=record[18]=record[19]=0;
 legacyCrc=uwb_config_crc(record,120);for(unsigned i=0;i<4;i++)record[120+i]=(uint8_t)(legacyCrc>>(8*i));
 assert(uwb_config_decode(record,&b,&gen)&&gen==43&&uwb_config_equal(&b,&c));
 assert(b.reference==0x84&&b.follower==0x85);
 assert(!uwb_config_set(&b,"reference","0x42")&&!uwb_config_set(&b,"follower","0x23"));
 assert(b.calibration&&b.correction_mm==-4321);uwb_config_encode(record,&b,44);
 assert(uwb_config_decode(record,&c,&gen)&&gen==44&&uwb_config_equal(&b,&c));
 for(unsigned i=0;i<sizeof(bad)/sizeof(*bad);i++){c=b;assert(uwb_config_set(&c,"reference",bad[i]));assert(uwb_config_equal(&c,&b));}
 c=b;assert(uwb_config_set(&c,"reference","0x23")&&uwb_config_set(&c,"follower","255"));
 /* Unknown packed bits are rejected even with an otherwise valid CRC. */
 record[19]=128;legacyCrc=uwb_config_crc(record,120);for(unsigned i=0;i<4;i++)record[120+i]=(uint8_t)(legacyCrc>>(8*i));
 assert(!uwb_config_decode(record,&c,&gen));
 /* Schema3 active records infer RF/role without changing their roster. */
 uwb_config_t old3=a;old3.slot_ms=500;old3.mode=1;old3.rf_enabled=1;old3.reference=0x42;old3.follower=0x23;
 uwb_config_encode(record,&old3,45);record[4]=3;record[19]=0;
 legacyCrc=uwb_config_crc(record,120);for(unsigned i=0;i<4;i++)record[120+i]=(uint8_t)(legacyCrc>>(8*i));
 uwb_config_t migrated;assert(uwb_config_decode(record,&migrated,&gen)&&gen==45&&uwb_config_equal(&old3,&migrated));
 old3.rf_enabled=0;uwb_config_encode(record,&old3,46);assert(uwb_config_decode(record,&migrated,&gen)&&uwb_config_equal(&old3,&migrated));
 for(unsigned flags=2;flags<=4;flags+=2) {
   record[19]=flags;legacyCrc=uwb_config_crc(record,120);for(unsigned i=0;i<4;i++)record[120+i]=(uint8_t)(legacyCrc>>(8*i));
   assert(!uwb_config_decode(record,&migrated,&gen)); /* Unsupported protocol ref/follower. */
 }
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
 /* Exercise real v2->v4 migration at every append/rotation power-cut point. */
 assert(b.reference==0x42&&b.follower==0x23);b.mode=1;b.rf_enabled=1;
 a.slot_ms=500;flash[4]=2;flash[17]=flash[18]=flash[19]=0;legacyCrc=uwb_config_crc(flash,120);
 for(unsigned i=0;i<4;i++)flash[120+i]=(uint8_t)(legacyCrc>>(8*i));
 assert(uwb_config_load(&store,&c,&gen)&&uwb_config_equal(&c,&a));
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
 flash[128+4]=6;uint32_t crc=uwb_config_crc(flash+128,120);
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
