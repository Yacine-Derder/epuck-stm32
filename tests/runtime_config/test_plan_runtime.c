#define MUL_RUNTIME_REUSE
#include "test_runtime.c"
int main(void) {
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
