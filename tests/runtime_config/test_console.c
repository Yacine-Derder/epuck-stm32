#define main original_console_main
#include "../epuck_console/test.c"
#undef main
static unsigned configCalls;
static char commandCopy[64];
bool uwb_runtime_command(const char *text) {
 char lower[64];size_t n=strlen(text);assert(n<sizeof(lower));
 for(size_t i=0;i<=n;i++)lower[i]=text[i]>='A'&&text[i]<='Z'?text[i]+('a'-'A'):text[i];
 /* Routing double: real help/parser semantics are covered by test_help.c. */
 if(strncmp(lower,"cfg",3)&&strncmp(lower,"tlm",3)&&strncmp(lower,"help",4)&&strncmp(lower,"hybrid",6))return false;
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
 /* A true newline splits commands; USB packet fragmentation alone does not.
  * This is a demonstrated way to get the same safety error, not proof that
  * it caused the user's historical intermittent CFG SHOW symptom. */
 at=strlen(mock_usb_log);console("CFG\nSHOW\n");assert(strstr(mock_usb_log+at,"ERR UWB-only"));
 assert(!motor_arm_pending&&!motor_count);puts("USB actual parser fragmented/oversized/control/disconnect and telemetry queue backpressure checks passed");return 0;
}
