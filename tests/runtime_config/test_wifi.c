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
 esp_link_set_enabled(0);assert(!esp_active&&!esp_link_telemetry(encoded));
 assert(!motor_arm_pending&&!motor_count);puts("Wi-Fi-enabled UWB-only policy: ARM rejected, reserved control priority, bounded optional UART/TTL and disable passed");return 0;
}
