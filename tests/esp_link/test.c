#include <assert.h>
#include <stdio.h>
#include <string.h>
#include "../../../Core/Src/epuck_link.c"
#include "../../../Core/Src/usb_console.c"
#include "../../../Core/Src/esp_link.c"
uint32_t tick,irq_mask,clear_calls,enable_calls,rx_calls,tx_calls;
UART_HandleTypeDef huart1, huart3;
USBD_HandleTypeDef hUsbDeviceFS;
static USBD_CDC_HandleTypeDef usb;
static char uart3_output[65536],usb_output[8192],robot_tx[32];
static uint8_t robot_busy, mock_rx_fail;
static const uint8_t *robot_pointer;
uint32_t HAL_GetTick(void) { return tick; }
void HAL_NVIC_ClearPendingIRQ(int irq) { assert(irq==USART1_IRQn || irq==USART3_IRQn); }
void HAL_NVIC_SetPriority(int irq,uint32_t p,uint32_t sub) { assert((irq==USART1_IRQn || irq==USART3_IRQn) && p==6 && sub==0); }
void HAL_NVIC_EnableIRQ(int irq) { assert(irq==USART1_IRQn || irq==USART3_IRQn); ++enable_calls; }
void mock_clear(UART_HandleTypeDef *uart) { (void)uart; ++clear_calls; }
uint32_t HAL_UART_Receive_IT(UART_HandleTypeDef *uart,uint8_t *p,uint16_t n) {
 (void)p; assert(n==1); if (uart == &huart3 && mock_rx_fail) return HAL_BUSY;
 uart->RxState=HAL_UART_STATE_BUSY_RX; uart->ErrorCode=0; ++rx_calls; return HAL_OK;
}
uint32_t HAL_UART_Transmit_IT(UART_HandleTypeDef *uart,const uint8_t *p,uint16_t n) {
 ++tx_calls;
 if(uart==&huart3) {
  size_t used=strlen(uart3_output); assert(used+n<sizeof(uart3_output)); memcpy(uart3_output+used,p,n); uart3_output[used+n]=0;
  HAL_UART_TxCpltCallback(uart);
 } else {
  assert(uart==&huart1 && !robot_busy && n<sizeof(robot_tx));
  robot_pointer=p; memcpy(robot_tx,p,n); robot_tx[n]=0; robot_busy=1;
 }
 return HAL_OK;
}
uint8_t CDC_Transmit_FS(uint8_t *p,uint16_t n) {
 size_t used=strlen(usb_output); assert(used+n<sizeof(usb_output)); memcpy(usb_output+used,p,n); usb_output[used+n]=0; usb.TxState=1; return USBD_OK;
}
#if APP_EPUCK_ENABLED && APP_ESP_ENABLED
static char test_id[17];
static const char *id=test_id;
static unsigned opens;
static void wire(const char *s) {
 while(*s) { esp_byte=(uint8_t)*s++; huart3.ErrorCode=0; huart3.RxState=HAL_UART_STATE_READY; HAL_UART_RxCpltCallback(&huart3); }
}
static void tasks(void) { esp_link_task(); epuck_link_task(); }
static void cmd(const char *s) { char frame[128]; snprintf(frame,sizeof(frame),"CMD %s %s\n",id,s); wire(frame); tasks(); }
static void robot_ack(const char *s) {
 assert(robot_busy && strcmp((const char *)robot_pointer,robot_tx)==0); robot_busy=0; HAL_UART_TxCpltCallback(&huart1);
 while(*s) { incoming=(uint8_t)*s++; huart1.RxState=HAL_UART_STATE_READY; HAL_UART_RxCpltCallback(&huart1); }
 tasks();
 for(int i=0;i<16;i++) esp_link_task();
}
static void open_session(void) {
 char record[64]; snprintf(test_id,sizeof(test_id),"%016llx",0x0123456789abcdefULL+opens++); id=test_id;
 snprintf(record,sizeof(record),"OPEN %s\r\n",id); wire(record); tasks(); assert(esp_valid && !esp_hb_seen);
}
static void wifi_arm(void) { cmd("CONTROL_HB"); cmd("ARM"); assert(strcmp(robot_tx,"ARM\n")==0); robot_ack("OK ARM\n"); assert(motor_owner==EPUCK_SOURCE_WIFI && motor_state==MOTOR_ARMED); }
static void streaming(void) {
 open_session();
 cmd("STREAM 50"); assert(stream_desired==50 && motor_owner==EPUCK_SOURCE_NONE && !motor_last_keepalive);
 assert(strstr(uart3_output,"RSP 0123456789abcdef [STREAM] desired=50"));
 tick=1000; tasks(); assert(strcmp(robot_tx,"PING\n")==0); robot_ack("PONG\n");
 assert(strcmp(robot_tx,"STREAM 50\n")==0 && stream_ack_source==EPUCK_SOURCE_WIFI);
 /* A USB request while the Wi-Fi request is pending does not steal its ACK. */
 hUsbDeviceFS.dev_state=USBD_STATE_CONFIGURED; hUsbDeviceFS.pClassData=&usb;
 epuck_link_console_command("STREAM 100"); assert(stream_desired==100 && stream_inflight==50);
 robot_ack("OK STREAM\n"); assert(strstr(uart3_output,"RSP 0123456789abcdef [STREAM] OK STREAM acknowledged=50"));
 robot_ack("PONG\n"); assert(strcmp(robot_tx,"STREAM 100\n")==0 && stream_ack_source==EPUCK_SOURCE_USB);
 robot_ack("OK STREAM\n"); robot_ack("PONG\n");
 for(int i=0;i<16;i++){usb.TxState=0;usb_console_task();}
 assert(strstr(usb_output,"OK STREAM acknowledged=100"));
 cmd("STREAM_STATUS"); assert(strstr(uart3_output,"[STREAM] desired=100"));
 assert(motor_owner==EPUCK_SOURCE_NONE && !motor_last_keepalive && stream_acknowledged==100);
 unsigned before=0; for(char *q=uart3_output;(q=strstr(q,"DATA2 "))!=NULL;q+=6) ++before;
 for(unsigned n=0;n<20;n++) {
  tick+=50;
  assert(accept_data("DATA 7 0 1 2 3 4 5 6 7 8 9 10 11 12 13 14 15 16 17 18 -20 21"));
  for(unsigned k=0;k<4;k++) esp_link_task();
 }
 unsigned after=0; for(char *q=uart3_output;(q=strstr(q,"DATA2 "))!=NULL;q+=6) ++after;
 assert(after==before+1); /* latest-only, maximum 1 Hz despite 20 Hz sensor arrivals */
 size_t used=strlen(uart3_output);
 for(unsigned k=0;k<20;k++) esp_link_task();
 assert(strlen(uart3_output)==used); /* No duplicate cached sample forwarding. */

}
static void ownership(void) {
 open_session(); cmd("ARM"); assert(!motor_arm_pending && !robot_busy); assert(strstr(uart3_output,"CONTROL_HB required"));
 wifi_arm(); cmd("M 200 -200"); assert(strcmp(robot_tx,"M 200 -200\n")==0); robot_ack("OK M\n");
 assert(acknowledged_left==200 && acknowledged_right==-200);
 assert(strstr(uart3_output,"RSP 0123456789abcdef [MOTOR] OK ARM"));
 assert(!hUsbDeviceFS.dev_state); /* Control works with no USB connection. */
 hUsbDeviceFS.dev_state=USBD_STATE_CONFIGURED; hUsbDeviceFS.pClassData=&usb;
 epuck_link_console_command("ARM"); epuck_link_console_command("M 1 2");
 for(int i=0;i<4;i++) { usb.TxState=0; usb_console_task(); }
 assert(strstr(usb_output,"owned by another source"));
 epuck_link_console_lost("USB disconnected"); assert(motor_state==MOTOR_ARMED);
 epuck_link_console_command("STOP"); epuck_link_task(); assert(strcmp(robot_tx,"STOP\n")==0);
 cmd("STOP"); /* Both sources subscribe to a coalesced in-flight STOP. */
 robot_ack("OK STOP\n"); assert(motor_owner==EPUCK_SOURCE_NONE);
 for(int i=0;i<4;i++) { usb.TxState=0; usb_console_task(); }
 assert(strstr(usb_output,"[MOTOR] OK STOP"));
 epuck_link_console_command("ARM"); epuck_link_task(); robot_ack("OK ARM\n"); assert(motor_owner==EPUCK_SOURCE_USB);
 cmd("ARM"); assert(motor_owner==EPUCK_SOURCE_USB);
 cmd("STOP"); assert(strcmp(robot_tx,"STOP\n")==0); robot_ack("OK STOP\n");
 assert(strstr(uart3_output,"RSP 0123456789abcdef [MOTOR] OK STOP"));
}
static void watchdog(void) {
 open_session(); wifi_arm();
 for(tick=100;tick<=400;tick+=100) { epuck_link_task(); assert(strcmp(robot_tx,"HB\n")==0); robot_ack("OK HB\n"); }
 tick=500; epuck_link_task(); /* ESP task deliberately never runs here. */
 assert(motor_state==MOTOR_LOST && strcmp(robot_tx,"STOP\n")==0 && !motor_count);
 robot_ack("OK STOP\n");
 assert(!esp_valid); cmd("CONTROL_HB"); cmd("ARM"); assert(motor_state==MOTOR_DISARMED);
 wire("OPEN 0123456789abcdef\n"); tasks(); assert(!esp_valid); /* Duplicate cannot reopen/replay. */
 open_session(); wifi_arm(); cmd("M 10 10"); robot_ack("OK M\n");
 wire("OPEN fedcba9876543210\nCMD 0123456789abcdef M 999 999\n"); tasks();
 assert(motor_state==MOTOR_LOST && !motor_count && !motor_arm_pending && strcmp(robot_tx,"STOP\n")==0);
 robot_ack("OK STOP\n"); id="fedcba9876543210"; cmd("ARM"); assert(motor_state==MOTOR_DISARMED); /* New HB still required. */
}
static void faults(void) {
 open_session(); wifi_arm(); wire("CMD 0123456789abcdef M 1"); tasks();
 huart3.ErrorCode=1; HAL_UART_ErrorCallback(&huart3); tasks();
 assert(!esp_valid && motor_state==MOTOR_LOST && strcmp(robot_tx,"STOP\n")==0);
 robot_ack("OK STOP\n"); wire("\n"); tasks(); open_session(); wifi_arm();
 for(int i=0;i<1100;i++) wire("X"); tasks(); assert(!esp_valid && motor_state==MOTOR_LOST); robot_ack("OK STOP\n");
 wire("\n"); tasks(); open_session(); wifi_arm();
 for(int i=0;i<17;i++) esp_link_reply_to(id,"control queue saturation");
 assert(!esp_link_owner_alive(id)); tasks(); assert(motor_state==MOTOR_LOST); robot_ack("OK STOP\n");
 wire("\n"); tasks(); open_session(); cmd("CONTROL_HB");
 char stale[64]; snprintf(stale,sizeof(stale),"CMD %s ARM\n",id); wire(stale); tick+=500; tasks(); assert(!motor_arm_pending && !robot_busy); /* Stale queued HB/ARM cannot revive. */
}
static void drain_usb(void) {
 for(int i=0;i<32;i++) { usb.TxState=0; usb_console_task(); }
}
static void diagnostics(void) {
 hUsbDeviceFS.dev_state=USBD_STATE_CONFIGURED; hUsbDeviceFS.pClassData=&usb;
 open_session(); cmd("CONTROL_HB");
 esp_busy=1U; /* Broken ESP TX must not hide the USB diagnostic. */
 tick=500U; tasks(); drain_usb();
 assert(!esp_valid && esp_invalidations==1U);
 assert(strstr(usb_output,"reason=control_hb_timeout hb_ms=500 seen=1"));
 assert(strstr(usb_output,"[ESP] faults uart=0"));
 unsigned losses=esp_invalidations, queued=console_message_count;
 esp_invalidate("framing_invalid"); assert(esp_invalidations==losses && console_message_count==queued);
 usb_console_receive_isr((const uint8_t *)"ESP_STATUS\n",11U); drain_usb();
 assert(strstr(usb_output,"valid=0 hb_seen=0 hb_ms=0 last=control_hb_timeout lost_hb_ms=500"));
 esp_busy=0U; for(int i=0;i<16;i++) esp_link_task();
 char *diag=strstr(uart3_output,"DIAG 0123456789abcdef reason=control_hb_timeout");
 char *lost=strstr(uart3_output,"SESSION 0123456789abcdef LOST"); assert(diag && lost && diag<lost);
 open_session();
 queued=console_message_count; huart3.ErrorCode=5U; HAL_UART_ErrorCallback(&huart3);
 assert(esp_uart_errors==1U && console_message_count==queued); /* ISR only captures. */
 tasks(); drain_usb(); assert(strstr(usb_output,"reason=uart_fault"));
 assert(esp_uart_error_bits==5U && !esp_rx_overflows && !esp_control_full);
 wire("\n");tasks();open_session();
 for(unsigned i=0;i<1100;i++) wire("X"); tasks();drain_usb();
 assert(esp_rx_overflows==77U && esp_uart_errors==1U && !esp_control_full);
 assert(strstr(usb_output,"reason=rx_overflow"));
 wire("\n");tasks();open_session();
 for(unsigned i=0;i<17;i++) esp_link_reply_to(id,"full");
 assert(esp_control_full==1U);tasks();drain_usb();
 assert(esp_control_full==1U && esp_last_fault_q==16U && esp_last_fault_mask==ESP_FAULT_TX);
 assert(strstr(usb_output,"reason=control_tx_full") && strstr(usb_output,"fault_q=16"));
 for(int i=0;i<16;i++) esp_link_task(); wire("\n");tasks();open_session();
 for(unsigned i=0;i<16;i++) assert(esp_enqueue("RSP queued",1U));
 uint32_t full=esp_control_full;
 esp_invalidate("framing_invalid");
 assert(esp_control_full==full && !esp_tx_fault); /* Emergency diagnostics never recursively fault. */
 drain_usb(); assert(strstr(usb_output,"reason=framing_invalid"));
 esp_qhead=esp_qtail=esp_count=0U;esp_valid=1U;
 for(unsigned i=0;i<15;i++) assert(esp_enqueue("RSP queued",1U));
 esp_invalidate("framing_invalid");
 assert(esp_count==16U && !esp_tx_fault && esp_control_full==full);
 assert(strncmp(esp_queue[15],"SESSION ",8U)==0); /* Only remaining slot belongs to LOST, not DIAG. */
}
static void reason_codes(void) {
 hUsbDeviceFS.dev_state=USBD_STATE_CONFIGURED; hUsbDeviceFS.pClassData=&usb;
 const char *records[]={"GARBAGE", "CMD zzzzzzzzzzzzzzzz HELP", "CLOSE 0123456789abcdef extra", "CMD 0123456789abcdef"};
 const char *reasons[]={"framing_invalid", "session_invalid", "close_malformed", "cmd_malformed"};
 for(unsigned i=0;i<4;i++) {
  esp_session_id[0]=0; wire("OPEN 0123456789abcdef\n");tasks();assert(esp_valid);
  char record[128];snprintf(record,sizeof(record),"%s\n",records[i]);wire(record);tasks();drain_usb();
  assert(!esp_valid && strcmp(esp_last_reason,reasons[i])==0);
 }
 esp_session_id[0]=0;wire("OPEN 0123456789abcdef\n");tasks();
 wire("CMD 0123456789abcdef HELP\n");tick+=500U;tasks();assert(strcmp(esp_last_reason,"stale_input")==0);
 esp_session_id[0]=0;wire("OPEN 0123456789abcdef\n");tasks();wire("CLOSE 0123456789abcdef\n");tasks();
 assert(strcmp(esp_last_reason,"explicit_close")==0);
 esp_session_id[0]=0;wire("OPEN 0123456789abcdef\n");tasks();wire("OPEN fedcba9876543210\n");tasks();
 assert(esp_valid && strcmp(esp_last_reason,"session_replaced")==0);
}
static void sensors(void) {
 const char *fields[22]={"65535","-32768","32767","-1","0","1","2","3","4","-32768","32767","-8","-7","-6","-5","-4","-3","-32768","0","32767","-2147483648","2147483647"};
 char data[256]; epuck_sensor_data_t before, after;
 assert(!epuck_link_get_sensor_data(&before));
 strcpy(data,"DATA"); for(unsigned i=0;i<22;i++) { strcat(data," ");strcat(data,fields[i]); }
 assert(accept_data(data)); assert(epuck_link_get_sensor_data(&before));
 assert(before.time_ms==65535 && before.prox[0]==INT16_MIN && before.light[7]==-3 && before.acc[2]==INT16_MAX && before.steps_left==INT32_MIN && before.steps_right==INT32_MAX);
 for(unsigned i=0;i<22;i++) {
  const char *saved=fields[i];
  const char *bad[4]={i==0?"65536":(i<20?"32768":"2147483648"),i==0?"-1":(i<20?"-32769":"-2147483649"),"NaN","99999999999999999999999999999999999999999"};
  for(unsigned j=0;j<4;j++) {
   fields[i]=bad[j]; strcpy(data,"DATA"); for(unsigned k=0;k<22;k++) { strcat(data," ");strcat(data,fields[k]); }
   assert(!accept_data(data)); assert(epuck_link_get_sensor_data(&after)); assert(memcmp(&before,&after,sizeof(before))==0);
  }
  fields[i]=saved;
 }
 strcpy(data,"DATA"); for(unsigned i=0;i<21;i++) { strcat(data," ");strcat(data,fields[i]); } assert(!accept_data(data));
 strcat(data," 0 1"); assert(!accept_data(data));
 strcpy(data,"DATA"); fields[0]="0"; for(unsigned i=0;i<22;i++) { strcat(data," ");strcat(data,fields[i]); }
 assert(accept_data(data)); assert(latest_sample.time_ms==0 && latest_sample.accepted_records==2 && esp_count==1); /* New raw sample retained; telemetry waits for next 1 Hz slot. */
 for(unsigned i=0;i<310;i++) parse_byte(i<5?(uint8_t)"DATA "[i]:'X'); parse_byte('\n');
 assert(malformed_data==1 && latest_sample.accepted_records==2);
 for(char *p=data;*p;p++) parse_byte((uint8_t)*p); parse_byte('\r');parse_byte('\n');
 assert(latest_sample.accepted_records==3 && !discard_line);
}
static void telemetry(void) {
 latest_sample=(epuck_sensor_data_t){.valid=1,.accepted_records=1,.time_ms=65535,.received_at_ms=UINT32_MAX-9,.steps_left=INT32_MIN,.steps_right=INT32_MAX};
 latest_sample.light[0]=32766;
 latest_sample.prox[0]=INT16_MIN; latest_sample.acc[2]=INT16_MAX; tick=10;
 esp_link_publish_sensor(); esp_link_task();
 assert(strstr(uart3_output,"DATA2 1 20 65535 -32768")); assert(strstr(uart3_output,"-2147483648 2147483647\n"));
 assert(!strstr(uart3_output,"32766")); /* light data is not on the new wire */
 unsigned first=esp_sample_sequence;
 latest_sample.accepted_records=2; tick+=999; esp_link_publish_sensor(); assert(esp_sample_sequence==first);
 tick++; esp_link_publish_sensor(); assert(esp_sample_sequence==2);
 esp_sample_tick=UINT32_MAX-500; latest_sample.accepted_records=3; tick=498;
 esp_link_publish_sensor(); assert(esp_sample_sequence==2);
 tick=499; esp_link_publish_sensor(); assert(esp_sample_sequence==3); /* 1000 ms across wrap */
 esp_link_log("Qorvo\r\nCMD not a command\n"); for (unsigned i=0;i<16;i++) esp_link_task();
 assert(strstr(uart3_output,"LOG Qorvo\n") && strstr(uart3_output,"LOG CMD not a command\n"));
 assert(!motor_arm_pending && !motor_count);
}
#endif
int main(int argc,char **argv) {
 if(argc==2 && strcmp(argv[1],"initfail")==0) mock_rx_fail=1;
 epuck_link_init(); esp_link_init();
 assert(enable_calls == APP_EPUCK_ENABLED + (APP_ESP_ENABLED && !mock_rx_fail));
#if APP_EPUCK_ENABLED && APP_ESP_ENABLED
 assert(argc==2);
 if(strcmp(argv[1],"ownership")==0) ownership();
 else if(strcmp(argv[1],"watchdog")==0) watchdog();
 else if(strcmp(argv[1],"faults")==0) faults();
 else if(strcmp(argv[1],"telemetry")==0) telemetry();
 else if(strcmp(argv[1],"sensors")==0) sensors();
 else if(strcmp(argv[1],"streaming")==0) streaming();
 else if(strcmp(argv[1],"diagnostics")==0) diagnostics();
 else if(strcmp(argv[1],"reasons")==0) reason_codes();
 else if(strcmp(argv[1],"initfail")==0) {
  assert(!esp_active); unsigned calls=tx_calls;
  esp_link_task(); esp_link_log("ignored before init"); assert(tx_calls==calls);
  mock_rx_fail=0; esp_link_init(); assert(esp_active);
 }
 else assert(0);
#else
 (void)argc; (void)argv;
 unsigned calls=tx_calls;
 esp_link_task(); epuck_link_task();
 if(!APP_ESP_ENABLED) { esp_link_log("ignored"); assert(!esp_link_owner_alive("0123456789abcdef")); }
 assert(tx_calls==calls);
#endif
 puts("PASS");
}
