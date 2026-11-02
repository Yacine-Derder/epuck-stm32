#define MUL_RUNTIME_REUSE
#include "test_runtime.c"
/* Golden hashes calculated independently with the canonical LPS planner.
 * Explicit timing: revision1, lead1000/process2000/air1000/guard100us,
 * ALRs132/133/134, RLR141, 5ms slots, 1000ms cycle, AA/AR1000mHz,
 * reply20ms, fair, utilization70. No physical configuration is inferred. */
static void aa_delay_commands(void) {
 memset(nv,255,sizeof(nv));uwb_runtime_init();command("addr 141");
 command("mode hybrid_static");command("off");command("cfg set clock_estimator kalman");
 command("cfg set antenna_offset_mm -50");command("plan init");
 command("plan set lead_us 1000");command("plan set slot_ms 5");
 command("plan alrs 132 133 134");command("plan rlrs 141");
 command("plan set aa_millihz 1000");command("plan set ar_millihz 1000");
 command("plan set aa on");command("plan set ar on");command("plan use on");
 uwb_config_t zero=current;uint8_t original[64],encoded[64];
 assert(mulRangingPlanEncode(original,&zero.plan)&&original[0]==2);
 assert(mulRangingPlanFingerprint(&zero.plan)==0x32FC5019);
 command("plan set aa_start_delay_us 50000");persisted();
 assert(current.plan.aaStartDelayUs==50000&&mulRangingPlanVersion(&current.plan)==4);
 assert(mulRangingPlanFingerprint(&current.plan)==0x552FF855);
 // The console delegates all operation placement to the same LPS shared core.
 for(unsigned cycle=0;cycle<40;cycle++)for(unsigned i=0;i<mulRangingPlanCount(&zero.plan,cycle);i++) {
  mulRangingOperation_t a,b;assert(mulRangingPlanOperation(&zero.plan,cycle,i,&a));
  assert(mulRangingPlanOperation(&current.plan,cycle,i,&b));
  unsigned shift=a.kind==MUL_PLAN_AA?50000:0;
  assert(b.kind==a.kind&&b.initiator==a.initiator&&b.responder==a.responder);
  assert(b.startUs==a.startUs+shift&&b.pollUs==a.pollUs+shift&&b.endUs==a.endUs+shift);
 }
 uwb_config_t delayed=current;unsigned writes=nvWrites;
 uwb_runtime_init();assert(!started&&nvWrites==writes&&uwb_config_equal(&current,&delayed));
 captured[0]=0;command("plan show");drain();
 assert(strstr(captured,"codec=4 hash=552FF855")&&strstr(captured,"aa_start_delay_us=50000"));
 const char *invalid[]={"-1","4294967296","4294967295","200000","1x"};
 for(unsigned i=0;i<sizeof(invalid)/sizeof(*invalid);i++) {
  char line[80];snprintf(line,sizeof(line),"plan set aa_start_delay_us %s",invalid[i]);command(line);
  assert(nvWrites==writes&&uwb_config_equal(&current,&delayed)&&!started);
 }
 command("on");writes=nvWrites;command("plan set aa_start_delay_us 0");
 assert(nvWrites==writes&&current.plan.aaStartDelayUs==50000);command("off");
 // A failed journal write must roll back RAM and leave the old plan bootable.
 delayed=current;writeBudget=5;command("plan set aa_start_delay_us 0");writeBudget=-1;
 assert(uwb_config_equal(&current,&delayed)&&!started);persisted();
 uwb_runtime_init();assert(uwb_config_equal(&current,&delayed)&&!started);
 command("plan set aa_start_delay_us 70000");persisted(); // uint32, not uint16.
 assert(mulRangingPlanFingerprint(&current.plan)==0xF804555D);
 command("plan set aa_start_delay_us 0");persisted();
 assert(mulRangingPlanEncode(encoded,&current.plan)&&!memcmp(encoded,original,64));
 assert(uwb_config_equal(&current,&zero));writes=nvWrites;
 uwb_runtime_init();assert(nvWrites==writes&&uwb_config_equal(&current,&zero)&&!started);
 captured[0]=0;command("plan show");drain();
 assert(strstr(captured,"codec=2 hash=32FC5019")&&strstr(captured,"aa_start_delay_us=0"));
 puts("AA delay console: LPS golden hashes, codec2/4, uint32 validation, AA-only placement, RF-OFF, persistence and write rollback passed");
}
int main(void) {
 aa_delay_commands();
 memset(nv,255,sizeof(nv));uwb_runtime_init();command("addr 141");
 command("peer add 132");command("peer add 133");command("cfg set mode hybrid_static");
 assert(started);unsigned writes=nvWrites;command("plan init");assert(nvWrites==writes&&!current.plan_present);
 command("off");uwb_config_t retained=current;command("plan init");assert(current.plan_present&&!current.plan_selected);persisted();
 writes=nvWrites;command("plan init");command("plan set slot_ms 3");assert(nvWrites==writes);
 command("plan set lead_us 1000");command("plan set process_us 500");command("plan set slot_ms 3");
 command("plan alrs 32 33 34 35");command("plan rlrs 141 142");command("plan set reply_ms 5");command("plan set cycle_ms 200");
 command("plan set aa on");command("plan set ar on");command("plan set revision 4294967295");
 assert(current.plan.broadcast.revision==UINT32_MAX&&current.plan.broadcast.count==4&&current.plan.rlrCount==2);
 command("plan set policy freshness");command("plan set aa_priority 200");command("plan set ar_priority 1");
 command("plan use on");assert(current.plan_selected&&!started);persisted();
 assert(!memcmp(&current.schedule,&retained.schedule,sizeof(current.schedule))&&current.slot_ms==retained.slot_ms);
 command("on");assert(started&&current.rf_enabled);persisted();
 writes=nvWrites;command("plan set reply_ms 20");assert(nvWrites==writes);
 command("off");assert(!started);writes=nvWrites;command("cfg set slot_ms 50");command("cfg roster set 132 133 134");
 command("plan rlrs 32");command("plan alrs 32 32");command("plan set util_percent 100");
 command("plan set revision 4294967296");command("plan set aa_priority 256");command("plan set policy unknown");command("plan set revision -1");command("plan set guard_us 101");command("plan use maybe");
 assert(nvWrites==writes&&current.plan_selected&&!started);
 command("cfg set clock_estimator kalman");
 assert(current.clock.window==5&&current.clock.periodMs==1000&&current.clock.estimator==2);
 mulClockFitConfig_t clock;uwb_config_effective_clock(&current,&clock);assert(clock.periodMs==200&&clock.window==5);
 uwb_config_t before=current;writes=nvWrites;uwb_runtime_init();assert(nvWrites==writes&&uwb_config_equal(&current,&before)&&!started);
 captured[0]=0;command("plan show");drain();assert(strstr(captured,"PLAN selected=1 codec=3")&&strstr(captured,"PLAN OP cycle=1")&&strstr(captured,"id=142"));
 writes=nvWrites;usbReady=false;command("plan show");drain();assert(display==6&&displayIndex==0&&nvWrites==writes);usbReady=true;drain();assert(!display);
 command("plan use off");assert(!current.plan_selected&&current.plan_present&&current.slot_ms==retained.slot_ms);
 command("on");assert(started&&current.rf_enabled&&!current.plan_selected);persisted();
 assert(!starts);puts("Plan runtime: RF-OFF edits, independent retained profile, schema9 reboot, bounded backpressure preview, malformed/no-write, effective period, assigned AR activation and deselection recovery passed");return 0;
}
