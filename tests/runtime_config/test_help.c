/* Actual command parser/pager/radio, UWB-only; no fake help implementation. */
#define MUL_RUNTIME_REUSE
#include "test_runtime.c"
int main(void) {
 memset(nv,255,sizeof(nv));tick=0;uwb_runtime_init();
 const char *commands[]={"help","help config","help hybrid","help peers",
 "help telemetry","help safety","help all","cfg help","peer help",
 "help cfg","help peer","help tlm","HeLp HyBrId"};
 for(unsigned t=0;t<sizeof(helpTopics)/sizeof(*helpTopics);t++)
   for(unsigned i=0;i<helpTopics[t].count;i++) {
     const char *line=helpTopics[t].lines[i];assert(strlen(line)<=80);
     for(unsigned j=0;line[j];j++)assert(line[j]>=32&&line[j]<=126);
   }
 assert(helpTopics[0].count>=20&&helpTopics[0].count<=30);
 unsigned writes=nvWrites;flashSafe=false; /* Read-only must bypass mutation gates. */
 for(unsigned c=0;c<sizeof(commands)/sizeof(*commands);c++) {
   captured[0]=0;command(commands[c]);assert(uwb_runtime_help_active());
   unsigned page=helpPage;usbReady=false;drain();assert(displayIndex==0&&helpPage==page);
   usbReady=true;outputSlots=4;drain();assert(displayIndex==0);outputSlots=16;
   drain();assert(!uwb_runtime_help_active()&&strstr(captured,"=== "));
   assert(!strstr(captured,"ERR UWB-only")&&nvWrites==writes&&!starts);
   if(c==0)assert(!strstr(captured,"65535")&&!strstr(captured,"factory")&&!strstr(captured,"Examples"));
   if(c==6)for(unsigned t=0;t<sizeof(helpTopics)/sizeof(*helpTopics);t++)
     for(unsigned i=0;i<helpTopics[t].count;i++)assert(strstr(captured,helpTopics[t].lines[i]));
 }
 captured[0]=0;command("help unknown");assert(strstr(captured,"ERR help topic")&&strstr(captured,"config, hybrid, peers, telemetry, safety or all"));
 captured[0]=0;command("help bad extra");assert(strstr(captured,"ERR help [topic]"));
 command("cfg show");drain();assert(strstr(captured,"Node address:")&&!strstr(captured,"ERR UWB-only")&&nvWrites==writes);
 /* Unsupported command stays unsupported; no invented system status alias. */
 assert(!uwb_runtime_command("status"));
 flashSafe=true;command("cfg set address 0x8d");command("peer add 0x84");command("peer add 0x85");
 command("cfg set telemetry on");command("cfg set mode hybrid");
 writes=nvWrites;unsigned tx=starts;captured[0]=0;command("help all");
 uint8_t frame[34],ref[8]={0x84,0,0,0,0,0,0xcf,0xbc};
 mulEncodeTestPacket(frame,ref,10,50000);memcpy(input,frame,34);length=36;rxMarker=100000;
 status=DWT_INT_RXFCG_BIT_MASK;tick=10000;uwb_runtime_task();
 assert(uwb_runtime_help_active()&&hybridReceiver.received==1&&accepted>0);
 assert(!strstr(captured,"HYBRID passive=")&&starts==tx&&nvWrites==writes);
 drain();assert(!uwb_runtime_help_active()&&hybridReceiver.received==1);
 command("cfg set mode off");assert(!started);persisted();
 puts("Topic help: concise ASCII overview/all topics/aliases, unknown syntax, backpressure, safety/read-only persistence and concurrent passive RX passed");
 return 0;
}
