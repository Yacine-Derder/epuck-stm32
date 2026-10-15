/* Host-only mocks; include the implementations to inspect queue invariants. */
#include <assert.h>
#include <stdio.h>
#include <string.h>
#include "../../../Core/Src/epuck_link.c"
#include "../../../Core/Src/usb_console.c"
uint32_t tick, irq_mask, clear_calls, enable_calls, rx_calls, tx_calls;
UART_HandleTypeDef huart1;
USBD_HandleTypeDef hUsbDeviceFS;
static USBD_CDC_HandleTypeDef mock_cdc;
static char mock_usb_log[32768], mock_tx_copy[32];
static const uint8_t *mock_tx_pointer;
static uint8_t mock_tx_busy;
static uint32_t mock_tx_result;
uint32_t HAL_GetTick(void) { return tick; }
void HAL_NVIC_ClearPendingIRQ(int irq) { assert(irq==USART1_IRQn && irq_mask); }
void HAL_NVIC_SetPriority(int irq, uint32_t priority, uint32_t subpriority) { assert(irq==USART1_IRQn && priority==6 && subpriority==0); }
void HAL_NVIC_EnableIRQ(int irq) { assert(irq==USART1_IRQn); ++enable_calls; }
void mock_clear(UART_HandleTypeDef *uart) { assert(uart==&huart1); ++clear_calls; }
uint32_t HAL_UART_Receive_IT(UART_HandleTypeDef *uart, uint8_t *buffer, uint16_t size) {
  assert(uart==&huart1 && size==1); (void)buffer; ++rx_calls;
  uart->RxState=HAL_UART_STATE_BUSY_RX; uart->ErrorCode=0; return HAL_OK;
}
uint32_t HAL_UART_Transmit_IT(UART_HandleTypeDef *uart, const uint8_t *buffer, uint16_t size) {
  assert(uart==&huart1 && irq_mask && !mock_tx_busy && size<sizeof(mock_tx_copy));
  assert((size==5 && memcmp(buffer,"PING\n",5)==0) || (size==2 && memcmp(buffer,"R\n",2)==0) ||
         (size==4 && memcmp(buffer,"ARM\n",4)==0) || (size==3 && memcmp(buffer,"HB\n",3)==0) ||
         (size==5 && memcmp(buffer,"STOP\n",5)==0) || (size>2 && memcmp(buffer,"M ",2)==0));
  ++tx_calls;
  if (mock_tx_result==HAL_OK) {
    mock_tx_pointer=buffer; memcpy(mock_tx_copy,buffer,size); mock_tx_copy[size]=0; mock_tx_busy=1;
  }
  return mock_tx_result;
}
uint8_t CDC_Transmit_FS(uint8_t *buffer, uint16_t length) {
  assert(hUsbDeviceFS.dev_state==USBD_STATE_CONFIGURED && hUsbDeviceFS.pClassData==&mock_cdc && !mock_cdc.TxState);
  size_t used=strlen(mock_usb_log); assert(used+length<sizeof(mock_usb_log));
  memcpy(mock_usb_log+used,buffer,length); mock_usb_log[used+length]=0; mock_cdc.TxState=1; return USBD_OK;
}
static void setup(void) {
  hUsbDeviceFS.dev_state=USBD_STATE_CONFIGURED; hUsbDeviceFS.pClassData=&mock_cdc;
  epuck_link_init(); usb_console_task(); assert(tx_calls==0);
}
static void input(const char *s) {
  uint32_t calls=tx_calls; usb_console_receive_isr((const uint8_t *)s,(uint32_t)strlen(s)); assert(tx_calls==calls);
}
static void console(const char *s) { input(s); mock_cdc.TxState=0; usb_console_task(); }
static void step(uint32_t now) {
  tick=now; mock_cdc.TxState=0; usb_console_task(); epuck_link_task();
}
static void output(void) { for(int i=0;i<32;i++) { mock_cdc.TxState=0; usb_console_task(); } }
#if APP_EPUCK_ENABLED
static void finish(void) {
  assert(mock_tx_busy && strcmp((const char *)mock_tx_pointer,mock_tx_copy)==0);
  mock_tx_busy=0; HAL_UART_TxCpltCallback(&huart1);
}
static void reply(const char *s) {
  while (*s) { incoming=(uint8_t)*s++; huart1.RxState=HAL_UART_STATE_READY; huart1.ErrorCode=0; HAL_UART_RxCpltCallback(&huart1); }
}
static void arm(void) {
  console("ARM\n"); step(tick); assert(strcmp(mock_tx_copy,"ARM\n")==0 && motor_state==MOTOR_ARMING);
  finish(); reply("OK ARM\r\n"); step(tick+1); assert(motor_state==MOTOR_ARMED && motor_known && acknowledged_left==0);
}
static void stop_ack(void) {
  assert(strcmp(mock_tx_copy,"STOP\n")==0); finish(); reply("OK STOP\n"); step(tick+1);
  assert(motor_state==MOTOR_DISARMED && !motor_stop_pending && motor_count==0);
}
static void validation(void) {
  console("AR"); step(0); assert(tx_calls==0);
  console("M\r"); step(0); assert(tx_calls==0);
  console("\nM 200 200\n"); step(0); assert(strcmp(mock_tx_copy,"ARM\n")==0 && motor_count==0);
  finish(); reply("OK ARM\n"); step(1);
  const char *bad[]={"ARM 1\n","STOP 1\n","HELP 1\n","MOTOR_STATUS 1\n","HB\n","UNKNOWN\n","M\n","M 1\n","M 1 2 3\n","M 1001 0\n","M -1001 0\n","M 1.0 0\n","M + 0\n","M 99999999999999999 0\n","M 1\t2\n","M 1\r2\n"};
  for(unsigned i=0;i<sizeof(bad)/sizeof(bad[0]);i++) { uint32_t calls=tx_calls; console(bad[i]); step(1); assert(tx_calls==calls && motor_count==0); }
  console("M +1000 -1000\r\nM -200 200\nM 0 0\nM 1 2\nM 3 4\n");
  assert(motor_count==4 && requested_left==1 && requested_right==2);
  step(2); assert(strcmp(mock_tx_copy,"M 1000 -1000\n")==0);
  assert(acknowledged_left==0 && requested_left==1);
  finish(); reply("OK M\n"); step(3); assert(acknowledged_left==1000 && acknowledged_right==-1000 && strcmp(mock_tx_copy,"M -200 200\n")==0);
  console("HELP\nMOTOR_STATUS\n"); output();
  assert(strstr(mock_usb_log,"queue full") && strstr(mock_usb_log,"commanded steps/s") && strstr(mock_usb_log,"latest_ack"));
  /* Oversized USB line cannot become a movement command. */
  char big[100]; memset(big,'A',sizeof(big)-1); big[99]=0; console(big); console("\nSTOP\n");
  assert(motor_count==0 && motor_stop_pending && motor_state==MOTOR_STOPPING);
}
static void cancellation(void) {
  arm(); console("M 200 200\nM 300 300\n"); step(2);
  assert(strcmp(mock_tx_copy,"M 200 200\n")==0);
  console("STOP\nARM\nM 500 500\n");
  assert(motor_count==0 && !motor_arm_pending && motor_stop_pending && motor_wait==TX_IDLE);
  step(3); assert(strcmp(mock_tx_copy,"M 200 200\n")==0); /* Active buffer untouched. */
  finish(); reply("OK M\nOK ARM\n"); step(1000);
  assert(strcmp(mock_tx_copy,"STOP\n")==0 && motor_state!=MOTOR_ARMED);
  stop_ack(); assert(ping_started==1); /* Background can run after STOP completes. */
  finish(); step(tick); assert(strcmp(mock_tx_copy,"R\n")==0); finish();
  assert(!motor_count && !motor_arm_pending && motor_state==MOTOR_DISARMED);
  /* STOP also cancels an ARM that has not yet transmitted. */
  console("ARM\nSTOP\n"); step(tick); assert(strcmp(mock_tx_copy,"STOP\n")==0 && !motor_arm_pending); stop_ack();
}
static void heartbeats(void) {
  arm(); step(99); assert(tx_calls==1);
  for(unsigned t=100;t<=1000;t+=100) {
    step(t); assert(strcmp(mock_tx_copy,"HB\n")==0); uint32_t calls=tx_calls;
    step(t); assert(tx_calls==calls); finish(); reply("OK HB\n"); step(t+1);
    if(t==1000) { assert(strcmp(mock_tx_copy,"PING\n")==0); finish(); step(t+1); assert(strcmp(mock_tx_copy,"R\n")==0); finish(); }
  }
  assert(motor_state==MOTOR_ARMED && ping_sent==1 && sensor_requests==1);
  console("ARM\n"); step(1002); assert(strcmp(mock_tx_copy,"ARM\n")==0 && motor_state==MOTOR_ARMING); /* Explicit re-arm resets movement. */
  finish(); reply("OK ARM\n"); step(1003);
  console("STOP\n"); step(1102); assert(strcmp(mock_tx_copy,"STOP\n")==0); stop_ack();
}
static void lost(void) {
  arm(); console("M 200 200\n"); step(2); finish();
  /* Late ACK cannot resurrect a session after a task scheduling gap. */
  reply("OK M\n"); step(202); assert(motor_state==MOTOR_LOST && strcmp(mock_tx_copy,"STOP\n")==0 && !motor_count);
  console("ARM\nM 400 400\n"); assert(!motor_arm_pending && !motor_count); stop_ack();
  arm(); step(tick+100); assert(strcmp(mock_tx_copy,"HB\n")==0); finish();
  reply("ERR NOT_ARMED\n"); step(tick+1); assert(motor_state==MOTOR_LOST && strcmp(mock_tx_copy,"STOP\n")==0); stop_ack();
  arm(); step(tick+100); finish(); step(motor_last_ack+200);
  assert(motor_state==MOTOR_LOST && strcmp(mock_tx_copy,"STOP\n")==0); stop_ack();
  /* No OK ARM: bounded timeout, then STOP and explicit ARM again. */
  console("ARM\n"); step(tick); finish(); step(tick+150);
  assert(motor_state==MOTOR_LOST && strcmp(mock_tx_copy,"STOP\n")==0); stop_ack();
  arm(); huart1.ErrorCode=1; HAL_UART_ErrorCallback(&huart1); step(tick+1);
  assert(motor_state==MOTOR_LOST && strcmp(mock_tx_copy,"STOP\n")==0);
  reply("\n"); epuck_link_task(); stop_ack();
  arm(); for(unsigned i=0;i<1100;i++) reply("X"); step(tick+1);
  assert(motor_state==MOTOR_LOST && strcmp(mock_tx_copy,"STOP\n")==0);
  finish(); reply("\nOK STOP\n"); step(tick+1); assert(motor_state==MOTOR_DISARMED);
}
static void disconnects(void) {
  arm(); console("M 200 200\n"); input("M 500 500\nARM\n");
  usb_console_disconnected_isr(); /* Reconnect happened before next task. */
  assert(!usb_console_available()); step(2);
  assert(motor_state==MOTOR_LOST && !motor_count && !motor_arm_pending && strcmp(mock_tx_copy,"STOP\n")==0); stop_ack();
  arm(); hUsbDeviceFS.dev_state=0; step(tick+1);
  assert(motor_state==MOTOR_LOST && strcmp(mock_tx_copy,"STOP\n")==0);
  finish(); reply("OK STOP\n"); step(tick+1); hUsbDeviceFS.dev_state=USBD_STATE_CONFIGURED; step(tick+1);
  assert(motor_state==MOTOR_DISARMED && !motor_arm_pending);
  arm(); uint8_t bytes[1100]; memset(bytes,'X',sizeof(bytes));
  usb_console_receive_isr(bytes,sizeof(bytes)); assert(!usb_console_available()); step(tick+1);
  assert(motor_state==MOTOR_LOST && strcmp(mock_tx_copy,"STOP\n")==0); stop_ack();
  console("\nARM\n"); step(tick+1); assert(strcmp(mock_tx_copy,"ARM\n")==0);
}
static void retry(void) {
  console("ARM\n"); mock_tx_result=HAL_BUSY; step(0); assert(tx_calls==1 && motor_arm_pending);
  step(9); assert(tx_calls==1); console("STOP\n");
  mock_tx_result=HAL_OK; step(9); assert(strcmp(mock_tx_copy,"STOP\n")==0 && !motor_arm_pending); stop_ack();
}
#endif
int main(int argc, char **argv) {
  setup();
#if APP_EPUCK_ENABLED
  assert(argc==2);
  if(strcmp(argv[1],"validation")==0) validation();
  else if(strcmp(argv[1],"stop")==0) cancellation();
  else if(strcmp(argv[1],"heartbeat")==0) heartbeats();
  else if(strcmp(argv[1],"lost")==0) lost();
  else if(strcmp(argv[1],"disconnect")==0) disconnects();
  else if(strcmp(argv[1],"retry")==0) retry();
  else assert(0);
#else
  (void)argc; (void)argv;
  console("ARM\nM 200 200\nSTOP\nMOTOR_STATUS\nHELP\n"); step(1000); output();
  assert(tx_calls==0 && rx_calls==0 && enable_calls==0 && clear_calls==0);
  assert(strstr(mock_usb_log,"feature disabled") && strstr(mock_usb_log,"Commanded steps/s"));
  epuck_sensor_data_t sample; assert(!epuck_link_get_sensor_data(&sample) && !sample.valid);
#endif
  puts("PASS");
}

uint8_t esp_link_rx_callback(void *uart) { (void)uart; return 0; }
uint8_t esp_link_tx_callback(void *uart) { (void)uart; return 0; }
uint8_t esp_link_error_callback(void *uart) { (void)uart; return 0; }
void esp_link_reply_to(const char *session, const char *message) { (void)session; (void)message; }
void esp_link_session(char out[17]) { memset(out,0,17); }
uint8_t esp_link_owner_alive(const char *session) { (void)session; return 0; }

void esp_link_publish_sensor(void) {}

void esp_link_usb_status(void) {}
