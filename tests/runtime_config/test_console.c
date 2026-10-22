#define main original_console_main
#include "../epuck_console/test.c"
#undef main
static unsigned configCalls;
static char commandCopy[64];
bool uwb_runtime_command(const char *text) {
 char lower[64];size_t n=strlen(text);assert(n<sizeof(lower));
 for(size_t i=0;i<=n;i++)lower[i]=text[i]>='A'&&text[i]<='Z'?text[i]+('a'-'A'):text[i];
 /* Routing double: real command semantics are covered by test_commands.c and
  * test_help.c; this test checks the unchanged USB/safety line boundary. */
 const char *owned[]={"cfg","tlm","help","hybrid","show","status","on","off","mode","role","addr","reference","follower","peer","cal","wifi","diag","save","sync"};
 bool found=false;
 for(unsigned i=0;i<sizeof(owned)/sizeof(*owned);i++) {
   size_t length=strlen(owned[i]);
   if(!strncmp(lower,owned[i],length)&&(!lower[length]||lower[length]==' '))found=true;
 }
 if(!found)return false;
 configCalls++;snprintf(commandCopy,sizeof(commandCopy),"%s",text);return true;
}
int main(void) {
 setup();console("CFG SET role ");assert(!configCalls);console("1\r\n");assert(configCalls==1&&!strcmp(commandCopy,"CFG SET role 1"));
 char overlong[90];memset(overlong,'X',sizeof(overlong));memcpy(overlong,"CFG ",4);overlong[88]='\n';overlong[89]=0;
 console(overlong);assert(configCalls==1);console("CFG SET role 2\n");assert(configCalls==2);
 console("CFG SET role\t1\n");assert(configCalls==2);
 input("CFG SET role 1\n");usb_console_disconnected_isr();usb_console_task();assert(configCalls==2);
 for(unsigned i=0;i<16;i++) {usb_console_write("occupied");}
 assert(!usb_console_try_write("TLM STAT backpressure"));
 output();assert(usb_console_try_write("TLM STAT accepted"));output();assert(strstr(mock_usb_log,"TLM STAT accepted"));
 unsigned before=configCalls;size_t at=strlen(mock_usb_log);
 console("cFg ");console("ShOw\r");console("\n");assert(configCalls==before+1&&!strcmp(commandCopy,"cFg ShOw"));
 console("help ");console("safety\r\n");assert(configCalls==before+2);
 assert(!strstr(mock_usb_log+at,"ERR UWB-only"));
 /* Newlines still split commands; SHOW is now independently owned by UWB. */
 at=strlen(mock_usb_log);before=configCalls;console("CFG\nSHOW\n");
 assert(configCalls==before+2&&!strstr(mock_usb_log+at,"ERR UWB-only"));
 before=configCalls;console("RoLe ");console("rLr\r\nOFF\nSTATUS\n");
 assert(configCalls==before+3&&!strcmp(commandCopy,"STATUS"));
 assert(!motor_arm_pending&&!motor_count);puts("USB actual parser fragmented/oversized/control/disconnect and telemetry queue backpressure checks passed");return 0;
}
