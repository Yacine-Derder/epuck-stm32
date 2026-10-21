/* Actual adapter + runtime manager: both roles in one executable, real journal. */
#define ADS_TEST_LOG_CAPACITY 262144
#define main legacy_adapter_tests_not_invoked
#include "../ads_radio/test.c"
#undef main
#include "../../Core/Src/uwb_runtime.c"
static uint8_t nv[2*UWB_CONFIG_SECTOR_SIZE],backup[sizeof(nv)];
static bool wifiEnabled;
void esp_link_set_enabled(uint8_t enabled) {wifiEnabled=enabled;}
uint8_t esp_link_telemetry(const char *line) {(void)line;return 0;}
uint32_t esp_link_uart_errors(void) {return 0;}
uint32_t esp_link_telemetry_dropped(void) {return 0;}
void esp_link_reset_diagnostics(void) {}
static bool flashSafe=true,rngGood=true,usbReady=true,readFail,failAfterCommit,failResume;
static int writeBudget=-1;
static uint8_t outputSlots=16;
static unsigned nvWrites,nvErases;
static uint64_t nonce=1000;
static bool nvread(void *ctx,uint32_t at,uint8_t *out,size_t size) {
 (void)ctx;assert(at+size<=sizeof(nv));if(readFail)return false;memcpy(out,nv+at,size);return true;
}
static bool nvprogram(void *ctx,uint32_t at,uint32_t word) {
 (void)ctx;assert(!mockRadioActive);assert(at%4==0&&at+4<=sizeof(nv));
 if(writeBudget==0)return false;
 if(writeBudget>0)writeBudget--;
 for(unsigned i=0;i<4;i++){uint8_t v=(uint8_t)(word>>(8*i));assert((nv[at+i]&v)==v);nv[at+i]&=v;}
 nvWrites++;
 if(at%128==124) {if(failResume)rxResult=DWT_ERROR;if(failAfterCommit)return false;}
 return true;
}
static bool nverase(void *ctx,unsigned bank) {
 (void)ctx;assert(!mockRadioActive);assert(bank<2);nvErases++;memset(nv+bank*UWB_CONFIG_SECTOR_SIZE,255,UWB_CONFIG_SECTOR_SIZE);return true;
}
const uwb_config_store_t *uwb_runtime_store(void) {static const uwb_config_store_t s={NULL,nvread,nvprogram,nverase};return &s;}
bool uwb_runtime_session(uint64_t *out) {*out=++nonce;return rngGood;}
const char *epuck_link_flash_error(void) {return flashSafe?NULL:"ERR configuration rejected: STOP unconfirmed; issue STOP";}
bool usb_console_try_write(const char *line) {if(!usbReady)return false;usb_console_write(line);return true;}
uint8_t usb_console_write_slots(void) {return outputSlots;}
static void command(const char *line) {assert(uwb_runtime_command(line));}
static void drain(void) {for(unsigned i=0;i<60;i++)uwb_runtime_task();}
static void persisted(void) {
 uwb_config_t c;uint32_t g;assert(uwb_config_load(uwb_runtime_store(),&c,&g));
 assert(applied&&uwb_config_equal(&c,&current)&&uwb_config_equal(&current,&active)&&g==generation);
}
static const char *const oldSequence[]={"CFG DEFAULTS","CFG SET peer 0x85","CFG SET address 0x84","CFG SET peer 0x86","CFG SET identity 1","CFG SET role 1","CFG SET mode 1","CFG APPLY","CFG SET mode 0","CFG APPLY","CFG SET mode 1","CFG SAVE"};

#ifndef MUL_RUNTIME_REUSE
int main(void) {
 memset(nv,255,sizeof(nv));uwb_runtime_init();assert(applied&&!started&&!savedValid&&!loadedAtBoot&&active.mode==0&&active.node==0x00);
 unsigned old=starts;tick=2000;uwb_runtime_task();assert(starts==old);assert(!uwb_runtime_command("ARM"));
 const char *bad[]={"CFG SET node 0x100","CFG SET peer 0x00","CFG SET mode tdma","CFG SET phy 1","CFG SET role 0","CFG SET node 132junk","CFG SET node 0x","CFG SET node -1","CFG SET node 4294967296","CFG SET role initiator extra"};
 for(unsigned i=0;i<sizeof(bad)/sizeof(*bad);i++){uwb_config_t before=current;command(bad[i]);assert(uwb_config_equal(&before,&current)&&!savedValid);}
 char longLine[100];memset(longLine,'A',99);memcpy(longLine,"CFG ",4);longLine[99]=0;command(longLine);
 /* Reproduce the old workflow's gate: stopped radio alone never implied safe
  * robot state. The supplied error could not identify the failed conjunction. */
 bool legacyRadioRunning=false;flashSafe=false;
 bool legacySaveAllowed=!legacyRadioRunning && flashSafe;
 assert(!legacySaveAllowed && epuck_link_flash_error()!=NULL);
 unsigned beforeWrites=nvWrites;
 for(unsigned i=0;i<sizeof(oldSequence)/sizeof(*oldSequence);i++)command(oldSequence[i]);
 assert(!savedValid&&!started&&nvWrites==beforeWrites&&current.node==0x00);
 assert(strstr(captured,"STOP unconfirmed"));flashSafe=true;
 /* Exact formerly failed sequence now commits each accepted command; aliases
  * are no-ops, not a maintenance ritual. */
 for(unsigned i=0;i<sizeof(oldSequence)/sizeof(*oldSequence);i++)command(oldSequence[i]);
 assert(savedValid&&started&&state.initiator&&active.node==0x84&&active.peer==0x86);persisted();
 beforeWrites=nvWrites;uint64_t sessionBefore=state.session;
 command("CFG SAVE");command("CFG APPLY");command("CFG SET role initiator");assert(beforeWrites==nvWrites&&state.session==sessionBefore);
 /* Requested two-board addresses/readable values, case insensitive. */
 command("CFG SET mode off");command("cFg SeT AdDrEsS 0X10");command("CFG SET peer 17");command("CFG SET identity ALR");command("CFG SET role INITIATOR");command("CFG SET interval_ms 500");
 command("CFG SET tx_ant_dtu 16381");command("CFG SET rx_ant_dtu 16382");command("CFG SET mode TwR");
 assert(started&&state.initiator&&state.local==0x10&&state.peer==0x11&&txAnt==16381&&rxAnt==16382);persisted();
 tick+=500;uwb_runtime_task();assert(state.pending);unsigned aborts=state.aborted;
 size_t abortAt=strlen(captured);
 command("CFG SET role responder");assert(strstr(captured+abortAt,"cause=0 stage=1"));assert(!state.initiator&&state.session!=sessionBefore&&started);(void)aborts;persisted();
 mulAdsPacket_t p={.source=0x11,.destination=0x10,.type=mulAdsPoll,.session=55,.transaction=1};
 receive(&p,MUL_TIMESTAMP_MASK-20);assert(sentPacket().type==mulAdsResponse);
 txMarker=10000000;status=DWT_INT_TXFRS_BIT_MASK;step();p.type=mulAdsFinal;receive(&p,20000000);
 assert(sentPacket().type==mulAdsReport);status=DWT_INT_TXFRS_BIT_MASK;step();assert(state.completed==1);
 command("CFG SET telemetry on");command("CFG SET samples on");
 for(unsigned i=0;i<6;i++){tick+=1000;uwb_runtime_task();}
 assert(count==4&&dropped>=2&&highWater==4);usbReady=false;command("TLM NEXT");assert(count==4);usbReady=true;command("TLM NEXT");assert(count==3);
 command("CFG SET role initiator");command("CFG SET correction_mm -500");command("CFG SET calibration on");assert(config.calibration);persisted();
 tick+=500;uwb_runtime_task();p=sentPacket();p.source=0x11;p.destination=0x10;p.type=mulAdsResponse;
 txMarker=MUL_TIMESTAMP_MASK-99999999;status=DWT_INT_TXFRS_BIT_MASK;receive(&p,50000001);
 txMarker=250000001;status=DWT_INT_TXFRS_BIT_MASK;step();p.type=mulAdsReport;p.stamps[0]=10000;p.stamps[1]=150000000;p.stamps[2]=350010000;
 receive(&p,300000000);assert(state.completed==1&&strstr(captured,"MULH CAL"));assert(count==1&&({mulTelemetry_t decoded;mulTelemetryDecode(telemetry[tail],&decoded)&&(decoded.flags&mulTlmCalibration);}));
 command("CFG SET tx_ant_dtu 16383");assert(!active.calibration);persisted();
 /* Flash failure at every word: previous saved record survives, old radio
  * resumes, no setter success and no pending RAM config. */
 uwb_config_t prior=current;memcpy(backup,nv,sizeof(nv));
 for(int fail=0;fail<32;fail++) {
  memcpy(nv,backup,sizeof(nv));writeBudget=fail;command("CFG SET identity rlr");writeBudget=-1;
  assert(uwb_config_equal(&active,&prior)&&started);persisted();
 }
 failAfterCommit=true;command("CFG SET identity rlr");failAfterCommit=false;assert(active.identity==2);persisted();
 /* Read/geometry failure never erases storage or updates runtime settings. */
 prior=active;unsigned eraseBefore=nvErases;readFail=true;command("CFG SET identity standalone");command("CFG LOAD");readFail=false;
 assert(nvErases==eraseBefore&&uwb_config_equal(&active,&prior));command("CFG LOAD");persisted();
 prior=active;beforeWrites=nvWrites;outputSlots=7;command("CFG SET identity standalone");outputSlots=16;
 assert(nvWrites==beforeWrites&&uwb_config_equal(&active,&prior)&&strstr(captured,"console busy"));
 /* Armed/unsafe rejects before ANY radio or flash mutation, even off/defaults. */
 prior=active;beforeWrites=nvWrites;sessionBefore=state.session;flashSafe=false;
 command("CFG SET mode off");command("CFG DEFAULTS");assert(uwb_config_equal(&active,&prior)&&state.session==sessionBefore&&nvWrites==beforeWrites);flashSafe=true;
 rngGood=false;command("CFG SET identity standalone");assert(uwb_config_equal(&saved,&prior));rngGood=true;command("CFG LOAD");persisted();
 /* Post-commit hardware error is explicitly saved-but-off, recoverable by LOAD. */
 failResume=true;command("CFG SET identity standalone");failResume=false;rxResult=DWT_SUCCESS;
 assert(saved.identity==0&&!started&&!applied&&strstr(captured,"configuration SAVED but radio OFF"));command("CFG LOAD");persisted();
 uwb_runtime_init();assert(loadedAtBoot&&savedValid&&started);persisted();
 /* Help/SHOW use backpressure-aware incremental output; all fields/defaults. */
 size_t helpAt=strlen(captured);command("hElP");usbReady=false;unsigned outputIndex=displayIndex;drain();assert(displayIndex==outputIndex);usbReady=true;drain();
 for(unsigned i=0;i<sizeof(help)/sizeof(*help);i++)assert(strstr(captured+helpAt,help[i]));
 const char *helpFields[]={"address","peer","identity","mode","role","interval_ms","phy","tx_ant_dtu","rx_ant_dtu","calibration","correction_mm","verbosity","telemetry","telemetry_ms","samples","factory","persistent","restarts","DEFAULTS","LOAD"};
 for(unsigned i=0;i<sizeof(helpFields)/sizeof(*helpFields);i++)assert(strstr(captured+helpAt,helpFields[i]));
 command("cFg ShOw");drain();assert(strstr(captured,"Configuration:      SAVED / ACTIVE")&&strstr(captured,"saved settings loaded at boot: YES"));
 command("CFG DEFAULTS");assert(!started&&!active.calibration&&active.node==0x00&&active.peer==0x01);persisted();
 beforeWrites=nvWrites;command("CFG DEFAULTS");assert(nvWrites==beforeWrites);assert(!nvErases);
 /* Board B setup from factory, no temporary peer or manual APPLY/SAVE. */
 command("CFG SET address 0x11");command("CFG SET peer 0x10");command("CFG SET identity rlr");
 command("CFG SET role responder");command("CFG SET calibration off");command("CFG SET mode twr");
 assert(started&&!state.initiator&&state.local==0x11&&state.peer==0x10);persisted();
 command("CFG DEFAULTS");
 /* Corrupt saved storage: LOAD preserves live state, reboot recovers defaults.
  * A valid old-schema fixture loads unchanged, without writing migration data. */
 memset(nv,0,sizeof(nv));command("CFG LOAD");assert(applied&&active.mode==0);
 uwb_runtime_init();assert(!loadedAtBoot&&!savedValid&&!started&&applied);
 uwb_config_t legacy;uwb_config_defaults(&legacy);legacy.node=0x86;legacy.peer=0x84;legacy.identity=2;legacy.calibration=1;legacy.correction_mm=-1234;
 memset(nv,255,sizeof(nv));uwb_config_encode(nv,&legacy,77);nv[4]=1;memset(nv+76,0,44);
 uint32_t legacyCrc=uwb_config_crc(nv,120);for(unsigned i=0;i<4;i++)nv[120+i]=(uint8_t)(legacyCrc>>(8*i));
 beforeWrites=nvWrites;uwb_runtime_init();
 assert(loadedAtBoot&&active.node==0x86&&active.peer==0x84&&generation==77&&nvWrites==beforeWrites&&active.calibration&&active.correction_mm==-1234);
 command("CFG SET wifi on");assert(wifiEnabled&&active.wifi);persisted();
 command("CFG SET telemetry on");tick+=1000;uwb_runtime_task();assert(count&&wifiCount);
 unsigned lost=wifiDrops;tick+=1001;uwb_runtime_task();assert(wifiDrops>lost);
 command("CFG SET telemetry_format compact");command("CFG SET wifi off");assert(!wifiEnabled);persisted();
 puts("same-binary immediate persistence/rollback/roles/8-bit IDs/help/old SAVE sequence/recovery tests passed");return 0;
}

#endif
