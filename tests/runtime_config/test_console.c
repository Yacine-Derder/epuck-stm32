#define main original_console_main
#include "../epuck_console/test.c"
#undef main
static unsigned configCalls;
static char commandCopy[64];
bool uwb_runtime_command(const char *text) {
 if(strncmp(text,"CFG",3)&&strncmp(text,"TLM",3))return false;
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
 assert(!motor_arm_pending&&!motor_count);puts("USB actual parser fragmented/oversized/control/disconnect and telemetry queue backpressure checks passed");return 0;
}
