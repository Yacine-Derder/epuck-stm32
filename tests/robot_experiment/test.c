#define main legacy_main
#include "../esp_link/test.c"
#undef main
bool uwb_runtime_command(const char *line){(void)line;return false;}
void HAL_NVIC_DisableIRQ(int irq){assert(irq==USART3_IRQn);}
uint32_t HAL_UART_Abort(UART_HandleTypeDef *uart){assert(uart==&huart3);uart->RxState=HAL_UART_STATE_READY;return HAL_OK;}
static void init(void) {
 epuck_link_init();esp_link_set_enabled(1);
 assert(MUL_ROBOT_EXPERIMENT && MUL_UWB_ONLY && APP_ROBOT_ALLOWED);
 assert(stream_desired==APP_WIFI_SENSOR_PERIOD_MS);
 assert(requested_left==0 && requested_right==0 && !motor_arm_pending && !motor_count);
 epuck_link_boot_safety();assert(strcmp(robot_tx,"STOP\n")==0);robot_ack("OK STOP\n");
 open_session();assert(esp_link_owner_alive(id) && !esp_hb_seen);
}
int main(int argc,char **argv) {
 assert(argc==2);init();
 if(!strcmp(argv[1],"motor")) {
  cmd("M 200 -100");assert(strcmp(robot_tx,"ARM\n")==0 && motor_count==1);
  robot_ack("OK ARM\n");assert(strcmp(robot_tx,"M 200 -100\n")==0);
  robot_ack("OK M\n");assert(acknowledged_left==200 && acknowledged_right==-100);
  cmd("M -20 30");assert(strcmp(robot_tx,"M -20 30\n")==0);robot_ack("OK M\n");
  /* No PC traffic: only the dsPIC heartbeat, no 500 ms external expiry. */
  for(tick=100;tick<1000;tick+=100) {
   tasks();assert(strcmp(robot_tx,"HB\n")==0);robot_ack("OK HB\n");assert(esp_valid && motor_state==MOTOR_ARMED);
  }
  assert(!strstr(uart3_output,"OK HB"));
  cmd("STOP");assert(strcmp(robot_tx,"STOP\n")==0);robot_ack("OK STOP\n");
  assert(acknowledged_left==0 && acknowledged_right==0 && !motor_count);
  assert(strstr(uart3_output,"[MOTOR] OK STOP"));
 } else if(!strcmp(argv[1],"failure")) {
  cmd("M 1 2");robot_ack("OK ARM\n");robot_ack("OK M\n");
  wire("CLOSE 0123456789abcdef\n");tasks();assert(!esp_valid && !motor_count && !motor_arm_pending);
  assert(strcmp(robot_tx,"STOP\n")==0);robot_ack("OK STOP\n");
  open_session();assert(!motor_count && !motor_arm_pending && requested_left==0);
  cmd("M 3 4");robot_ack("OK ARM\n");robot_ack("ERR NOT_ARMED\n");
  assert(strcmp(robot_tx,"STOP\n")==0 && !motor_count);robot_ack("OK STOP\n");
  cmd("M 5 6");robot_ack("OK ARM\n");
  robot_busy=0;HAL_UART_TxCpltCallback(&huart1);tick+=150;tasks();
  assert(strcmp(robot_tx,"STOP\n")==0 && motor_state==MOTOR_LOST && !motor_count);
  robot_ack("OK STOP\n");tasks();assert(!motor_arm_pending && !motor_count);
 } else if(!strcmp(argv[1],"rate")) {
  /* Ten seconds of actual new producer records. No cached interpolation. */
  unsigned expected=0;
  for(tick=1;tick<=10001;tick+=APP_WIFI_SENSOR_PERIOD_MS) {
   latest_sample=(epuck_sensor_data_t){.valid=1,.accepted_records=++expected,.received_at_ms=tick,.time_ms=(uint16_t)tick,.steps_left=INT32_MIN,.steps_right=INT32_MAX};
   esp_link_publish_sensor();for(int k=0;k<8;k++)esp_link_task();
   assert(esp_sample_sequence==expected);
   size_t before=strlen(uart3_output);esp_link_publish_sensor();esp_link_task();assert(strlen(uart3_output)==before);
  }
  unsigned count=0;for(char *p=uart3_output;(p=strstr(p,"DATA2 "));p+=6)count++;
  assert(count==expected);assert(strstr(uart3_output,"-2147483648 2147483647"));
  printf("rate=%u Hz samples=%u bytes=%zu in 10 seconds (UART mock)\n",1000/APP_WIFI_SENSOR_PERIOD_MS,count,strlen(uart3_output));
 } else assert(0);
 puts("PASS");return 0;
}
