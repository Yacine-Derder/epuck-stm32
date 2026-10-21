/* Actual radio adapter + runtime manager, both roles in ONE executable. */
#define main legacy_adapter_tests_not_invoked
#include "../ads_radio/test.c"
#undef main
#include "../../Core/Src/uwb_runtime.c"
static uint8_t nv[2*UWB_CONFIG_SECTOR_SIZE];
static bool flashSafe=true,rngGood=true,usbReady=true;
static uint64_t nonce=1000;
static bool nvread(void *ctx,uint32_t at,uint8_t *out,size_t size) {(void)ctx;assert(at+size<=sizeof(nv));memcpy(out,nv+at,size);return true;}
static bool nvprogram(void *ctx,uint32_t at,uint32_t word) {(void)ctx;assert(at%4==0&&at+4<=sizeof(nv));for(unsigned i=0;i<4;i++){uint8_t v=(uint8_t)(word>>(8*i));assert((nv[at+i]&v)==v);nv[at+i]&=v;}return true;}
static bool nverase(void *ctx,unsigned bank) {(void)ctx;assert(bank<2);memset(nv+bank*UWB_CONFIG_SECTOR_SIZE,255,UWB_CONFIG_SECTOR_SIZE);return true;}
const uwb_config_store_t *uwb_runtime_store(void) {static const uwb_config_store_t s={NULL,nvread,nvprogram,nverase};return &s;}
bool uwb_runtime_session(uint64_t *out) {*out=++nonce;return rngGood;}
uint8_t epuck_link_flash_safe(void) {return flashSafe;}
bool usb_console_try_write(const char *line) {if(!usbReady)return false;usb_console_write(line);return true;}
static void command(const char *line) {assert(uwb_runtime_command(line));}
int main(void) {
 memset(nv,255,sizeof(nv));uwb_runtime_init();assert(applied&&!started&&!savedValid&&active.mode==0);
 unsigned old=starts;tick=2000;uwb_runtime_task();assert(starts==old);assert(!uwb_runtime_command("ARM"));
 const char *bad[]={"CFG SET node 0x99","CFG SET peer 0x86","CFG SET mode 3","CFG SET phy 1","CFG SET role 0","CFG SET node 132junk","CFG SET node 0x","CFG SET node -1","CFG SET node 4294967296","CFG SET role 1 extra"};
 for(unsigned i=0;i<sizeof(bad)/sizeof(*bad);i++) {uwb_config_t before=current;command(bad[i]);assert(uwb_config_equal(&before,&current));}
 char longLine[100];memset(longLine,'A',sizeof(longLine));memcpy(longLine,"CFG ",4);longLine[99]=0;command(longLine);
 command("CFG SET node 0x85");command("CFG SET identity 1");command("CFG SET role 1");command("CFG SET interval_ms 500");
 command("CFG SET tx_ant_dtu 16381");command("CFG SET rx_ant_dtu 16382");
 command("CFG SET mode 1");assert(active.mode==0);command("CFG APPLY");
 assert(started&&state.initiator&&state.local==0x85&&state.peer==0x84&&txAnt==16381&&rxAnt==16382&&active.identity==1);
 uint64_t firstSession=state.session;assert(firstSession==1001);tick+=500;uwb_runtime_task();assert(starts==old+1&&state.pending);
 command("CFG SAVE");assert(!savedValid); /* active ranging cannot stall for flash */
 command("CFG SET role 2");command("CFG SET identity 2");command("CFG APPLY");
 assert(!state.initiator&&active.identity==2&&state.session!=firstSession&&state.local==0x85);
 mulAdsPacket_t p={.source=0x84,.destination=0x85,.type=mulAdsPoll,.session=55,.transaction=1};
 receive(&p,MUL_TIMESTAMP_MASK-20);assert(sentPacket().type==mulAdsResponse);
 txMarker=10000000;status=DWT_INT_TXFRS_BIT_MASK;step();p.type=mulAdsFinal;receive(&p,20000000);
 assert(sentPacket().type==mulAdsReport);status=DWT_INT_TXFRS_BIT_MASK;step();assert(state.completed==1);
 command("CFG SET telemetry 1");command("CFG SET samples 1");command("CFG APPLY");
 for(unsigned i=0;i<6;i++){tick+=1000;uwb_runtime_task();}
 assert(count==4&&dropped==2&&highWater==4);
 char oversized[192];memset(oversized,'X',191);oversized[191]=0;uwb_runtime_telemetry(oversized);assert(count==4&&dropped==3);
 usbReady=false;command("TLM NEXT");assert(count==4);usbReady=true;command("TLM NEXT");assert(count==3);command("TLM STATUS");assert(strstr(captured,"uart_forwarding=0"));
 command("CFG SET role 1");command("CFG SET correction_mm -500");command("CFG SET calibration 1");command("CFG APPLY");assert(!count&&config.calibration);
 tick+=500;uwb_runtime_task();p=sentPacket();assert(p.type==mulAdsPoll&&p.source==0x85);
 p.source=0x84;p.destination=0x85;p.type=mulAdsResponse;
 txMarker=MUL_TIMESTAMP_MASK-99999999;status=DWT_INT_TXFRS_BIT_MASK;receive(&p,50000001);
 txMarker=250000001;status=DWT_INT_TXFRS_BIT_MASK;step();p.type=mulAdsReport;p.stamps[0]=10000;p.stamps[1]=150000000;p.stamps[2]=350010000;
 receive(&p,300000000);assert(state.completed==1);assert(strstr(captured,"MULH CAL"));assert(count==1&&strstr(telemetry[tail],"raw_m=")&&strstr(telemetry[tail],"calibration=1"));
 command("CFG SET mode 0");command("CFG APPLY");assert(!started);
 flashSafe=false;command("CFG SAVE");assert(!savedValid);flashSafe=true;
 command("CFG SAVE");assert(savedValid&&generation==1);command("CFG DEFAULTS");assert(!uwb_config_equal(&current,&saved));
 command("CFG LOAD");assert(uwb_config_equal(&current,&saved)&&current.calibration==1);
 /* Save ADS desired configuration while the active radio remains off. */
 command("CFG SET mode 1");command("CFG SAVE");assert(generation==2);
 uwb_runtime_init();assert(savedValid&&started&&state.initiator&&active.calibration&&state.session!=firstSession);
 rngGood=false;command("CFG APPLY");assert(!started&&!applied);rngGood=true;
 badAnt=true;command("CFG APPLY");assert(!started&&!applied);badAnt=false;
 command("CFG SHOW");command("CFG SHOW ACTIVE");command("CFG SHOW SAVED");command("CFG SHOW DEFAULTS");
 assert(strstr(captured,"snapshot not applied; radio off"));
 /* Torn/corrupt storage boots with off defaults, never the partial settings. */
 memset(nv,0,sizeof(nv));uwb_runtime_init();assert(!savedValid&&!started&&applied&&active.mode==0&&!active.calibration);
 puts("same-binary runtime initiator/responder/IDs/save/reboot/fail-closed/calibration/telemetry backpressure tests passed");
}
