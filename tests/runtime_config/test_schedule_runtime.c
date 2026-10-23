#define MUL_RUNTIME_REUSE
#include "test_runtime.c"
int main(void) {
 memset(nv,255,sizeof(nv));uwb_runtime_init();command("addr 0x8d");
 command("peer add 0x84");command("peer add 0x85");command("cfg set mode hybrid_static");
 assert(started&&active.rf_enabled);unsigned writes=nvWrites;
 command("cfg roster set 0x84 0x85 0x86");assert(nvWrites==writes&&!active.schedule.profile);
 command("cfg set slot_ms 50");assert(nvWrites==writes&&active.slot_ms==10);
 command("off");assert(!started&&!active.rf_enabled);
 command("cfg roster set 0x84 0x85 0x86 0x87 0x88 0x89 0x8a 0x8b");
 assert(active.schedule.profile==MUL_PROFILE_MULTI&&active.schedule.count==8&&active.peer_count==3);
 persisted();writes=nvWrites;uwb_runtime_init();assert(nvWrites==writes&&active.schedule.count==8&&!active.rf_enabled);
 command("cfg roster show");drain();assert(strstr(captured,"profile=2 count=8")&&strstr(captured,"ALR slot=7 id=0x8B"));
 command("cfg roster set 0x84 0x84 0x86");assert(nvWrites==writes&&active.schedule.count==8);
 command("cfg set slot_ms 500");assert(nvWrites==writes&&active.slot_ms==10);
 command("on");assert(started&&active.rf_enabled);
 command("off");assert(!started&&!active.rf_enabled);
 command("cfg roster set 0x84 0x85");assert(!active.schedule.profile&&active.reference==132&&active.follower==133);
 command("on");assert(started&&active.rf_enabled&&active.mode==5);persisted();
 puts("Multi runtime: RF-OFF roster/slot transactions, N8 save/reboot, malformed/no-write, passive multi activation and N2 restore passed");return 0;
}
