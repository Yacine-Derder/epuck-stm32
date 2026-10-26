#define main legacy_config_tests
#include "test_config.c"
#undef main
static mulRangingPlan_t plan(void) {
 mulRangingPlan_t p={.replyMs=5,.maxUtilization=70,.enabled=MUL_PLAN_AA,.aaMilliHz=500,.arMilliHz=2000,.rlrCount=1,.rlrs={141}};
 mulScheduleLegacy(&p.broadcast,32,33,10);mulScheduleExperimental(&p.broadcast);
 p.broadcast.count=4;p.broadcast.ids[2]=34;p.broadcast.ids[3]=35;
 p.broadcast.rangingFlags=0;p.broadcast.slotMs=3;p.broadcast.periodMs=200;
 p.broadcast.txLeadUs=1000;p.broadcast.rxBudgetUs=500;assert(!mulRangingPlanValidate(&p,NULL));return p;
}
static void outer_crc(uint8_t *b) {uint32_t crc=uwb_config_crc(b,248);for(unsigned i=0;i<4;i++)b[248+i]=(uint8_t)(crc>>(8*i));}
static void selected_enabled_reboot(void) {
 uwb_config_t old,c,loaded;uwb_config_defaults(&old);old.node=141;old.mode=4;old.protocol_role=UWB_PROTOCOL_RLR;
 c=old;mulRangingPlan_t p=plan();assert(!uwb_config_plan_set(&c,&p,true));assert(!uwb_config_set(&c,"rf_enabled","on"));
 // This plan's ALRs32..35 intentionally differ from retained legacy132/133.
 // Decoding must bind selection before validating RF-ON roster eligibility.
 memset(flash,255,sizeof(flash));uint32_t gen=0;assert(uwb_config_save(&store,&old,&gen));assert(uwb_config_save(&store,&c,&gen));
 assert(uwb_config_load(&store,&loaded,&gen)&&uwb_config_equal(&loaded,&c)&&loaded.rf_enabled&&loaded.plan_selected);
}
int main(void) {
 selected_enabled_reboot();
 uwb_config_t ar;uwb_config_defaults(&ar);ar.node=142;ar.mode=5;ar.protocol_role=UWB_PROTOCOL_RLR;
 mulRangingPlan_t assigned=plan();assigned.enabled|=MUL_PLAN_AR;
 assert(!uwb_config_plan_set(&ar,&assigned,true));uwb_config_t beforeAr=ar;
 assert(uwb_config_set(&ar,"rf_enabled","on")&&uwb_config_equal(&ar,&beforeAr));
 ar.node=141;assert(!uwb_config_set(&ar,"rf_enabled","on"));
 for(unsigned profile=0;profile<3;profile++)for(unsigned selected=0;selected<2;selected++) {
  uwb_config_t old,c,loaded;uwb_config_defaults(&old);old.node=141;old.mode=4;old.protocol_role=UWB_PROTOCOL_RLR;
  if(profile)assert(!uwb_config_set(&old,"clock_estimator","regression"));
  if(profile==2) {
   assert(!uwb_config_set(&old,"profile","experimental"));assert(!uwb_config_set(&old,"tx_lead_us","1000"));
   assert(!uwb_config_set(&old,"rx_budget_us","500"));assert(!uwb_config_set(&old,"slot_ms","17"));assert(!uwb_config_set(&old,"alr_alr_twr","off"));assert(!uwb_config_set(&old,"cycle_ms","500"));
  }
  c=old;mulRangingPlan_t p=plan();if(profile==2){p.policy=1;p.aaPriority=200;p.arPriority=1;}assert(!uwb_config_plan_set(&c,&p,selected));
  assert(c.reference==old.reference&&c.follower==old.follower&&c.slot_ms==old.slot_ms);
  assert(!memcmp(&c.schedule,&old.schedule,sizeof(c.schedule))&&!memcmp(&c.clock,&old.clock,sizeof(c.clock)));
  mulSchedule_t effective,retained;uwb_config_schedule(&c,&retained);uwb_config_effective_schedule(&c,&effective);
  assert(effective.slotMs==(selected?3:old.slot_ms));assert(effective.periodMs==(selected?200:profile==2?500:1000));
  mulClockFitConfig_t clock;uwb_config_effective_clock(&c,&clock);assert(clock.periodMs==effective.periodMs);
  assert(effective.ids[0]==(selected?32:132));
  uwb_config_t before=c;
  if(selected){assert(!uwb_config_set(&c,"rf_enabled","on"));c=before;} // Passive RF4 now supported.
  c.rf_enabled=1;assert(uwb_config_plan_set(&c,&p,selected));c=before;
  p.rlrs[0]=32;assert(uwb_config_plan_set(&c,&p,selected)&&uwb_config_equal(&c,&before));p=plan();
  memset(flash,255,sizeof(flash));uint32_t gen=0;assert(uwb_config_save(&store,&old,&gen));memcpy(backup,flash,sizeof(flash));
  for(int cut=0;cut<=64;cut++) {
   memcpy(flash,backup,sizeof(flash));budget=cut;bool ok=uwb_config_save(&store,&c,&gen);budget=-1;
   assert(uwb_config_load(&store,&loaded,&gen)&&uwb_config_equal(&loaded,ok?&c:&old));
  }
  memcpy(flash,backup,sizeof(flash));assert(uwb_config_save(&store,&c,&gen));assert(flash[260]==9);
  assert(!uwb_config_decode(flash+256,&loaded,&gen));assert(uwb_config_load(&store,&loaded,&gen)&&uwb_config_equal(&loaded,&c));
  unsigned at=writes;assert(uwb_config_save(&store,&c,&gen)&&writes==at);
  // Outer CRC alone must not admit a malformed plan, selection or reserved field.
  const unsigned offsets[]={172,173,174,175,176,213,240,247};
  for(unsigned i=0;i<sizeof(offsets)/sizeof(offsets[0]);i++) {
   uint8_t original[256];memcpy(original,flash+256,256);flash[256+offsets[i]]=255;outer_crc(flash+256);
   assert(uwb_config_load(&store,&loaded,&gen)&&uwb_config_equal(&loaded,&old));memcpy(flash+256,original,256);
  }
  assert(!uwb_config_plan_set(&c,&p,!selected));assert(uwb_config_save(&store,&c,&gen));
  assert(uwb_config_load(&store,&loaded,&gen)&&uwb_config_equal(&loaded,&c));
  assert(!memcmp(&loaded.schedule,&old.schedule,sizeof(c.schedule))&&loaded.reference==old.reference);
  // Fill bank0 with valid legacy records; every erase/program cut during rotation
  // preserves the latest old record until the final schema9 commit word.
  memset(flash,255,sizeof(flash));uwb_config_t legacy;uwb_config_defaults(&legacy);legacy.node=141;
  for(unsigned pos=0;pos<UWB_CONFIG_SECTOR_SIZE;pos+=128)uwb_config_encode(flash+pos,&legacy,pos/128+1);
  memcpy(backup,flash,sizeof(flash));
  for(int cut=0;cut<=65;cut++) {
   memcpy(flash,backup,sizeof(flash));budget=cut;bool ok=uwb_config_save(&store,&c,&gen);budget=-1;
   assert(uwb_config_load(&store,&loaded,&gen)&&uwb_config_equal(&loaded,ok?&c:&legacy));
  }
 }
 puts("Plan schema9: independent retained profiles, effective clock/timing, selection, all64-word append/rotation cuts, mixed scan, nested CRC/reserved rejection, no-op and RF gate passed");return 0;
}
