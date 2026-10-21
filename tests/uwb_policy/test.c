/* Reuse real UART/USB code and mocks without editing their setup semantics. */
#define main original_safety_main
#include "../epuck_console/test.c"
#undef main
int main(void) {
  assert(MUL_UWB_ONLY && !APP_ESP_ENABLED && APP_EPUCK_ENABLED && APP_EPUCK_STREAM_PERIOD_MS==0 && !APP_DEBUG_TELEMETRY);
  setup();
  assert(!epuck_link_flash_safe());
  assert(strstr(epuck_link_flash_error(),"grace"));
  /* Original SAVE failure despite stopped UWB: any received byte excluded the
   * standalone branch, while no STOP ACK made motor_known false. */
  tick=2000;rx_bytes=1;assert(!motor_known && !epuck_link_flash_safe());
  assert(strstr(epuck_link_flash_error(),"STOP unconfirmed"));
  rx_bytes=0;motor_state=MOTOR_ARMED;
  assert(!epuck_link_flash_safe() && strstr(epuck_link_flash_error(),"motors armed"));
  motor_state=MOTOR_DISARMED;motor_arm_pending=1;
  assert(!epuck_link_flash_safe());motor_arm_pending=0;
  tick=0;epuck_link_boot_safety();
  assert(mock_tx_busy && strcmp(mock_tx_copy,"STOP\n")==0);
  assert(motor_state==MOTOR_STOPPING && !motor_count && !motor_arm_pending);
  stop_ack();
  tick=2000;assert(!epuck_link_flash_safe());
  assert(strstr(epuck_link_flash_error(),"STREAM 0 unconfirmed"));tick=1;
  unsigned starts=tx_calls;
  console("ARM\nM 200 200\nSTREAM 1000\n"); step(2);
  assert(tx_calls==starts && !motor_count && !motor_arm_pending && motor_state==MOTOR_DISARMED);
  assert(stream_desired==0 && strstr(mock_usb_log,"UWB-only"));
  step(1000); assert(strcmp(mock_tx_copy,"PING\n")==0); finish(); reply("PONG\n"); step(1001);
  assert(strcmp(mock_tx_copy,"STREAM 0\n")==0); stream_confirm(); assert(stream_known && !stream_desired);
  assert(tick<2000U && !epuck_link_flash_safe());
  /* Lost STREAM0 acknowledgement with a completed PONG fence must still
   * retry idempotent stream stop after 5s, awaiting a fresh peer response. */
  reply("BOOT peer\n"); step(2000);
  assert(strcmp(mock_tx_copy,"PING\n")==0); finish(); reply("PONG\n"); step(2001);
  assert(strcmp(mock_tx_copy,"STREAM 0\n")==0); uint32_t sent=tick;
  finish(); step(tick+1); assert(strcmp(mock_tx_copy,"PING\n")==0);
  finish(); reply("PONG\n"); step(sent+500);
  assert(!stream_wait && !stream_known);
  step(sent+5001); assert(stream_need && !stream_peer_seen && strcmp(mock_tx_copy,"PING\n")==0);
  finish(); reply("PONG\n"); step(tick+1);
  assert(strcmp(mock_tx_copy,"STREAM 0\n")==0); stream_confirm();
  console("STOP\n"); step(tick+1); assert(strcmp(mock_tx_copy,"STOP\n")==0); stop_ack();
  assert(epuck_link_flash_safe());
  console("LINK_STATUS\n"); output(); assert(strstr(mock_usb_log,"[LINK]"));
  output(); assert(!strstr(mock_usb_log,"[SENSOR]") && !strstr(mock_usb_log,"light=["));
  huart1.ErrorCode=8; HAL_UART_ErrorCallback(&huart1); step(tick+1000); output();
  assert(strstr(mock_usb_log,"[EPUCK] FAULT") && strstr(mock_usb_log,"bits=0x8"));
  for (unsigned i=0;i<8 && mock_tx_busy;i++) {
    char command[32]; strcpy(command,mock_tx_copy); finish();
    reply(strncmp(command,"STREAM",6)==0 ? "OK STREAM\n" : "PONG\n"); step(tick+1);
  }
  assert(!mock_tx_busy);
  epuck_link_boot_safety(); assert(!epuck_link_flash_safe()); assert(strcmp(mock_tx_copy,"STOP\n")==0); finish(); step(tick+151);
  assert(motor_state==MOTOR_LOST && strcmp(mock_tx_copy,"STOP\n")==0 && !motor_arm_pending && !motor_count);
  stop_ack(); /* missing STOP ACK retries STOP only, never re-arms */
  puts("UWB-only boot STOP, movement/stream rejection, STREAM0, emergency STOP and fault reporting passed");
}
