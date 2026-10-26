#define main legacy_config_tests
#include "test_config.c"
#undef main
int main(void) {
 const unsigned counts[]={2,3,4,8};
 for(unsigned n=0;n<4;n++) {
  uwb_config_t old,c,loaded;uwb_config_defaults(&old);old.node=141;
  uint8_t ids[8]={132,133,134,135,136,137,138,139};
  memset(flash,255,sizeof(flash));uint32_t gen=0;
  assert(uwb_config_save(&store,&old,&gen)&&gen==1);
  c=old;assert(!uwb_config_roster(&c,ids,counts[n]));
  assert(c.peer==old.peer&&c.calibration==old.calibration&&c.slot_ms==10);
  if(counts[n]==2){assert(uwb_config_equal(&c,&old));assert(!uwb_config_set(&c,"profile","multi"));}
  assert(c.schedule.profile==MUL_PROFILE_MULTI&&c.schedule.count==counts[n]);
  if(n%2)assert(!uwb_config_set(&c,"clock_estimator","regression"));
  memcpy(backup,flash,sizeof(flash));
  for(int fail=0;fail<=64;fail++) {
   memcpy(flash,backup,sizeof(flash));budget=fail;bool ok=uwb_config_save(&store,&c,&gen);budget=-1;
   assert(uwb_config_load(&store,&loaded,&gen));assert(uwb_config_equal(&loaded,ok?&c:&old));
  }
  memcpy(flash,backup,sizeof(flash));assert(uwb_config_save(&store,&c,&gen));
  assert(uwb_config_load(&store,&loaded,&gen)&&uwb_config_equal(&loaded,&c));
  unsigned before=writes;assert(uwb_config_save(&store,&c,&gen)&&writes==before);
  uwb_config_t bad=c;ids[1]=ids[0];assert(uwb_config_roster(&bad,ids,counts[n])&&uwb_config_equal(&bad,&c));
  ids[1]=133;assert(uwb_config_set(&bad,"slot_ms","20")&&uwb_config_equal(&bad,&c));
  assert(uwb_config_save(&store,&old,&gen)&&uwb_config_load(&store,&loaded,&gen)&&uwb_config_equal(&old,&loaded));
  // Fresh sector transition with legacy slots occupying both256-byte halves.
  memset(flash,255,sizeof(flash));for(unsigned at=0;at<UWB_CONFIG_SECTOR_SIZE;at+=128)uwb_config_encode(flash+at,&old,at/128+1);
  memcpy(backup,flash,sizeof(flash));
  for(int fail=0;fail<=65;fail++) {
   memcpy(flash,backup,sizeof(flash));budget=fail;bool ok=uwb_config_save(&store,&c,&gen);budget=-1;
   assert(uwb_config_load(&store,&loaded,&gen)&&uwb_config_equal(&loaded,ok?&c:&old));
  }
 }
 // Schema7 also carries explicit clock settings for an implicit legacy roster.
 uwb_config_t old,c,loaded;uwb_config_defaults(&old);old.node=141;c=old;
 assert(!uwb_config_set(&c,"clock_estimator","regression"));assert(!uwb_config_set(&c,"clock_window","30"));
 memset(flash,255,sizeof(flash));uint32_t gen=0;assert(uwb_config_save(&store,&old,&gen));memcpy(backup,flash,sizeof(flash));
 for(int fail=0;fail<=64;fail++) {memcpy(flash,backup,sizeof(flash));budget=fail;bool ok=uwb_config_save(&store,&c,&gen);budget=-1;assert(uwb_config_load(&store,&loaded,&gen));assert(uwb_config_equal(&loaded,ok?&c:&old));}
 memcpy(flash,backup,sizeof(flash));assert(uwb_config_save(&store,&c,&gen));assert(flash[260]==7);
 assert(uwb_config_load(&store,&loaded,&gen)&&!loaded.schedule.profile&&loaded.clock.window==30);
 flash[416]=2;uint32_t crc=uwb_config_crc(flash+256,248);for(unsigned i=0;i<4;i++)flash[504+i]=(uint8_t)(crc>>(8*i));
 assert(uwb_config_load(&store,&loaded,&gen)&&uwb_config_equal(&loaded,&old)); // CRC-valid unknown clock version falls back.
 puts("Multi schedule journal: N2/3/4/8, legacy128/new256 mixed scan, all64-word append/rotation cuts, reboot/no-op, invalid roster and rollback passed");return 0;
}
