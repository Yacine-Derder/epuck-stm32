#define main legacy_config_tests
#include "test_config.c"
#undef main
static void experimental_journal(void) {
 const unsigned counts[]={2,4,8};
 for(unsigned k=0;k<3;k++)for(unsigned explicit=0;explicit<2;explicit++) {
  uwb_config_t old,c,loaded;uwb_config_defaults(&old);old.node=141;c=old;
  uint8_t ids[8]={132,133,134,135,136,137,138,139};
  assert(!uwb_config_roster(&c,ids,counts[k]));
  assert(!uwb_config_set(&c,"profile","experimental"));
  uwb_config_t before=c;assert(uwb_config_set(&c,"slot_ms","3")&&uwb_config_equal(&before,&c));
  assert(!uwb_config_set(&c,"tx_lead_us","1000"));assert(!uwb_config_set(&c,"rx_budget_us","500"));
  assert(!uwb_config_set(&c,"slot_ms","3"));assert(!uwb_config_set(&c,"twr_reply_ms","5"));
  assert(mulScheduleReplyMs(&c.schedule)==5);assert(!uwb_config_set(&c,"alr_alr_twr","off"));assert(mulScheduleReplyMs(&c.schedule)==5);
  if(explicit)assert(!uwb_config_set(&c,"clock_estimator","kalman"));
  assert(!uwb_config_set(&c,"cycle_ms","200"));
  mulClockFitConfig_t clock;uwb_config_clock(&c,&clock);assert(clock.periodMs==200);
  assert((c.clock.periodMs!=0)==(explicit!=0));
  assert(!uwb_config_roster(&c,ids,counts[k])&&c.schedule.profile==3);
  before=c;
  const char *fields[]={"slot_ms","cycle_ms","guard_us","tx_lead_us","rx_budget_us","airtime_us"};
  const char *bad[]={"65536","201","101","499","99","399"};
  for(unsigned i=0;i<6;i++)assert(uwb_config_set(&c,fields[i],bad[i])&&uwb_config_equal(&c,&before));
  assert(uwb_config_set(&c,"profile","multi")&&uwb_config_equal(&c,&before));
  memset(flash,255,sizeof(flash));uint32_t gen=0;assert(uwb_config_save(&store,&old,&gen));memcpy(backup,flash,sizeof(flash));
  for(int fail=0;fail<=64;fail++) {
   memcpy(flash,backup,sizeof(flash));budget=fail;bool ok=uwb_config_save(&store,&c,&gen);budget=-1;
   assert(uwb_config_load(&store,&loaded,&gen)&&uwb_config_equal(&loaded,ok?&c:&old));
  }
  memcpy(flash,backup,sizeof(flash));assert(uwb_config_save(&store,&c,&gen));assert(flash[260]==8);
  assert(!uwb_config_decode(flash+256,&loaded,&gen));
  assert(uwb_config_load(&store,&loaded,&gen)&&uwb_config_equal(&loaded,&c));
  unsigned previous=writes;assert(uwb_config_save(&store,&c,&gen)&&previous==writes);
  // CRC-valid unknown marker must roll back, including implicit vs explicit semantics.
  flash[256+172]=2;uint32_t crc=uwb_config_crc(flash+256,248);
  for(unsigned i=0;i<4;i++)flash[256+248+i]=(uint8_t)(crc>>(8*i));
  assert(uwb_config_load(&store,&loaded,&gen)&&uwb_config_equal(&loaded,&old));
  // All word cuts during sector rotation preserve the previously committed state.
  memset(flash,255,sizeof(flash));for(unsigned at=0;at<UWB_CONFIG_SECTOR_SIZE;at+=128)uwb_config_encode(flash+at,&old,at/128+1);
  memcpy(backup,flash,sizeof(flash));
  for(int fail=0;fail<=65;fail++) {
   memcpy(flash,backup,sizeof(flash));budget=fail;bool ok=uwb_config_save(&store,&c,&gen);budget=-1;
   assert(uwb_config_load(&store,&loaded,&gen)&&uwb_config_equal(&loaded,ok?&c:&old));
  }
 }
}
int main(void) {
 experimental_journal();
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
  if(n%2)assert(!uwb_config_set(&c,"clock_estimator","kalman"));
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
 assert(!uwb_config_set(&c,"clock_estimator","kalman"));assert(uwb_config_set(&c,"clock_window","30"));
 memset(flash,255,sizeof(flash));uint32_t gen=0;assert(uwb_config_save(&store,&old,&gen));memcpy(backup,flash,sizeof(flash));
 for(int fail=0;fail<=64;fail++) {memcpy(flash,backup,sizeof(flash));budget=fail;bool ok=uwb_config_save(&store,&c,&gen);budget=-1;assert(uwb_config_load(&store,&loaded,&gen));assert(uwb_config_equal(&loaded,ok?&c:&old));}
 memcpy(flash,backup,sizeof(flash));assert(uwb_config_save(&store,&c,&gen));assert(flash[260]==7);
 assert(uwb_config_load(&store,&loaded,&gen)&&!loaded.schedule.profile&&loaded.clock.window==5);
 // Reboot from a CRC-valid old regression selector: migrate only clock choice.
 flash[416]=1;flash[417]=1;flash[418]=24;flash[419]=8;
 uint32_t migrationCrc=uwb_config_crc(flash+256,248);for(unsigned i=0;i<4;i++)flash[504+i]=(uint8_t)(migrationCrc>>(8*i));
 assert(uwb_config_load(&store,&loaded,&gen)&&loaded.node==141&&loaded.clock.estimator==0&&loaded.clock.window==5);
 flash[416]=2;flash[417]=255;flash[418]=flash[419]=5;
 migrationCrc=uwb_config_crc(flash+256,248);for(unsigned i=0;i<4;i++)flash[504+i]=(uint8_t)(migrationCrc>>(8*i));
 assert(uwb_config_load(&store,&loaded,&gen)&&loaded.node==141&&loaded.clock.estimator==0);
 flash[416]=255;uint32_t crc=uwb_config_crc(flash+256,248);for(unsigned i=0;i<4;i++)flash[504+i]=(uint8_t)(crc>>(8*i));
 assert(uwb_config_load(&store,&loaded,&gen)&&uwb_config_equal(&loaded,&old)); // CRC-valid unknown clock version falls back.
 puts("Multi schedule journal: N2/3/4/8, legacy128/new256 mixed scan, all64-word append/rotation cuts, reboot/no-op, invalid roster and rollback passed");return 0;
}
