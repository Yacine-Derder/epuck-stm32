#define main original_esp_main
#include "../esp_link/test.c"
#undef main
#include "mul_telemetry.h"
bool uwb_runtime_command(const char *line){(void)line;return false;}
void HAL_NVIC_DisableIRQ(int irq){assert(irq==USART3_IRQn);}
uint32_t HAL_UART_Abort(UART_HandleTypeDef *uart){assert(uart==&huart3);uart->RxState=HAL_UART_STATE_READY;return HAL_OK;}
int main(void){
 epuck_link_init();esp_link_init();assert(!esp_active);esp_link_set_enabled(1);assert(esp_active);
 open_session();cmd("CONTROL_HB");cmd("ARM");assert(motor_state!=MOTOR_ARMED&&!motor_arm_pending&&!motor_count);
 mulTelemetry_t r={.type=mulTlmRange,.node=16,.peer=17,.radio=2,.mode=1,.role=1,.epoch=1};char encoded[MUL_TLM_LINE];assert(mulTelemetryEncode(encoded,sizeof(encoded),&r));
 assert(esp_link_telemetry(encoded));assert(!esp_link_telemetry(encoded));
 esp_link_reply_to(id,"STOP first");unsigned before=(unsigned)strlen(uart3_output);esp_link_task();assert(strstr(uart3_output+before,"STOP first")&&!strstr(uart3_output+before,"UWB2"));
 esp_link_task();assert(strstr(uart3_output+before,"UWB2 ")&&!esp_telemetry_pending);
 assert(esp_link_telemetry(encoded));tick+=1001;esp_link_task();assert(!esp_telemetry_pending&&esp_link_telemetry_dropped()>0);
 /* HAL ORE while optional data is pending: invalidate/purge, require new OPEN,
  * then prove telemetry can resume. This is not physical UART validation. */
 wire("\n");tasks();open_session();cmd("CONTROL_HB");assert(esp_link_telemetry(encoded));
 huart3.ErrorCode=8;huart3.RxState=HAL_UART_STATE_READY;HAL_UART_ErrorCallback(&huart3);esp_link_task();
 assert(esp_uart_error_bits==8&&!esp_valid&&!esp_telemetry_pending&&!esp_link_telemetry(encoded));
 wire("\n");tasks();open_session();cmd("CONTROL_HB");
 r.mode=4;r.role=2;assert(mulTelemetryEncode(encoded,sizeof(encoded),&r));assert(!strncmp(encoded,"UWB3 ",5));
 for(unsigned i=0;i<16;i++)esp_link_task();before=(unsigned)strlen(uart3_output);
 assert(esp_link_telemetry(encoded));esp_link_task();assert(strstr(uart3_output+before,"UWB3 "));
 r=(mulTelemetry_t){.type=mulTlmTdoa,.node=141,.peer=133,.radio=2,.mode=5,.role=2,.epoch=99,.ratePolicy=1,.legacySlotMs=500,
 .tdoa={.reference=132,.follower=133,.referenceSequence=3666,.followerSequence=3676,.status=4,.clockValid=1,.rateState=1,.ratePpb=0,.tdoaDtu=-28,.rangeDifferenceMm=-131}};
 assert(mulTelemetryEncode(encoded,sizeof(encoded),&r)&&strlen(encoded)==309);
 for(unsigned i=0;i<16;i++)esp_link_task();before=(unsigned)strlen(uart3_output);
 assert(esp_link_telemetry(encoded));esp_link_task();assert(strstr(uart3_output+before,encoded));
 /* HAL backpressure retains a pending record without blocking or corrupting it. */
 cmd("CONTROL_HB");mock_tx3_busy=1;assert(esp_link_telemetry(encoded));
 before=(unsigned)strlen(uart3_output);
 for(unsigned i=0;i<100;i++)esp_link_task();
 assert(esp_telemetry_pending&&strlen(uart3_output)==before);
 mock_tx3_busy=0;esp_link_task();assert(!esp_telemetry_pending&&strstr(uart3_output+before,encoded));
 /* A full optional debug FIFO cannot starve TDoA; actual replies still win. */
 for(unsigned cycle=0;cycle<100;cycle++) {
   uart3_output[0]=0;cmd("CONTROL_HB");
   for(unsigned i=0;i<ESP_QUEUE;i++)esp_link_log("optional flood");
   assert(esp_link_telemetry(encoded));
   esp_link_reply_to(id,"essential reply");before=(unsigned)strlen(uart3_output);
   esp_link_task();assert(strstr(uart3_output+before,"essential reply"));
   before=(unsigned)strlen(uart3_output);esp_link_task();
   assert(strstr(uart3_output+before,encoded)&&!esp_telemetry_pending);
   for(unsigned i=0;i<ESP_QUEUE+2;i++)esp_link_task();tick+=10;
 }
 assert(!motor_arm_pending&&motor_state!=MOTOR_ARMED);
 esp_length=10;esp_discard=esp_fault=1;esp_link_set_enabled(0);assert(!esp_length&&!esp_discard&&!esp_fault);assert(!esp_active&&!esp_link_telemetry(encoded));
 assert(!motor_arm_pending&&!motor_count);puts("Wi-Fi-enabled UWB-only policy: ARM rejected, reserved control priority, bounded optional UART/TTL and disable passed");return 0;
}
