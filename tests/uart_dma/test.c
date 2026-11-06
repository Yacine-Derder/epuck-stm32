#define main legacy_main
#include "../esp_link/test.c"
#undef main
bool uwb_runtime_command(const char *line){(void)line;return false;}
void HAL_NVIC_DisableIRQ(int irq){assert(irq==USART3_IRQn);}
uint32_t HAL_UART_Abort(UART_HandleTypeDef *uart){assert(uart==&huart3);uart->RxState=HAL_UART_STATE_READY;return HAL_OK;}
uint32_t HAL_UART_AbortReceive(UART_HandleTypeDef *uart){return HAL_UART_Abort(uart);}
static DMA_HandleTypeDef dma;
static uint32_t dma_position;
uint32_t HAL_UARTEx_GetRxEventType(UART_HandleTypeDef *uart){return uart->event;}
uint32_t HAL_UARTEx_ReceiveToIdle_DMA(UART_HandleTypeDef *uart,uint8_t *bytes,uint16_t size) {
 assert(uart==&huart3 && bytes==esp_dma_buffer && size==ESP_DMA_SIZE);
 if(mock_rx_fail)return HAL_BUSY;
 uart->hdmarx=&dma;dma.remaining=size;dma.pending=0;dma_position=0;uart->RxState=HAL_UART_STATE_BUSY_RX;return HAL_OK;
}
static void pending_irq(void) {
 if(dma.pending) {dma.pending=0;huart3.event=HAL_UART_RXEVENT_TC;HAL_UARTEx_RxEventCallback(&huart3,ESP_DMA_SIZE);}
}
void mock_dma_feed(const char *s) {
 while(*s) {
  esp_dma_buffer[dma_position++]=(uint8_t)*s++;
  if(dma_position==ESP_DMA_SIZE) {dma_position=0;dma.pending=1;if(!irq_mask)pending_irq();}
  dma.remaining=ESP_DMA_SIZE-dma_position;
 }
 if(!irq_mask){huart3.event=HAL_UART_RXEVENT_IDLE;HAL_UARTEx_RxEventCallback(&huart3,(uint16_t)dma_position);}
}
static void init(void) {
 epuck_link_init();esp_link_set_enabled(1);assert(esp_active && huart3.hdmarx==&dma);
 epuck_link_boot_safety();robot_ack("OK STOP\n");open_session();
 assert(esp_valid && esp_link_owner_alive(id));uart3_output[0]=0;
}
int main(int argc,char **argv) {
 assert(argc==2);init();
 if(!strcmp(argv[1],"wrap")) {
  /* Fragmentation, concatenation, HT/IDLE duplicates, many wraps. */
  for(unsigned i=0;i<500;i++) {
   mock_dma_feed("CMD 0123456789abcdef MOTOR_");tasks();
   mock_dma_feed("STATUS\nCMD ffffffffffffffff STOP\n");tasks();tasks();
   assert(esp_valid);uart3_output[0]=0;tick++;
  }
  assert(esp_dma_wraps>10 && !esp_rx_overflows && !esp_uart_errors);
  /* A pending TC before its IRQ and the committed TC give the same producer. */
  while(dma_position<ESP_DMA_SIZE-8U) {mock_dma_feed("\n");tasks();}
  irq_mask=1;mock_dma_feed("\n\n\n\n\n\n\n\n\n\n\n\n");esp_dma_poll();uint32_t producer=esp_dma_previous;
  assert(dma.pending);irq_mask=0;pending_irq();esp_dma_poll();assert(esp_dma_previous==producer);
  puts("DMA wraps, pending-TC race, fragmented/concatenated records passed");
 } else if(!strcmp(argv[1],"burst")) {
  irq_mask=1;mock_dma_feed("CMD 0123456789abcdef M 10 20\n");irq_mask=0;tasks();
  assert(!strcmp(robot_tx,"ARM\n"));robot_ack("OK ARM\n");robot_ack("OK M\n");
  assert(acknowledged_left==10 && acknowledged_right==20);
  /* About300us at921600:28 bytes captured despite no UART ISR. */
  irq_mask=1;mock_dma_feed("CMD 0123456789abcdef STOP\n");irq_mask=0;tasks();
  assert(!strcmp(robot_tx,"STOP\n"));robot_ack("OK STOP\n");
  assert(strstr(uart3_output,"OK STOP") && !esp_uart_errors);
 } else if(!strcmp(argv[1],"errors")) {
  const unsigned errors[]={1,2,4,8,HAL_UART_ERROR_DMA};
  for(unsigned i=0;i<5;i++) {
   huart3.ErrorCode=errors[i];HAL_UART_ErrorCallback(&huart3);assert(esp_dma_restart_needed);
   unsigned restarts=esp_dma_restarts;tasks();assert(!esp_valid && esp_dma_restarts==restarts+1);
   mock_dma_feed("\n");tasks();open_session();assert(esp_valid);
  }
  assert(esp_dma_errors==1 && esp_uart_errors==5);
  /* Detect overwrite rather than parsing uncertain commands. */
  for(unsigned i=0;i<ESP_DMA_SIZE;i++)mock_dma_feed("X");tasks();assert(!esp_valid && esp_rx_overflows);
 } else if(!strcmp(argv[1],"sustained")) {
  const char *noise="CMD ffffffffffffffff MOTOR_STATUS\n";unsigned offset=0;
  unsigned uwb=0,sensors=0;uint64_t bytes=0,busy_until=0;
  char radio[310];memset(radio,'0',309);memcpy(radio,"UWB3 ",5);radio[309]=0;
  cmd("M 50 -60");robot_ack("OK ARM\n");robot_ack("OK M\n");uart3_output[0]=0;
  for(tick=1;tick<=300000;tick++) {
   /*92B/ms input ~=920000 wire bits/s, including continuous valid framing. */
   for(unsigned i=0;i<92;i++){char fragment[2]={noise[offset++],0};if(!noise[offset])offset=0;mock_dma_feed(fragment);}
   if(tick%27==0)assert(esp_link_telemetry(radio));
   if(tick%100==0)latest_sample=(epuck_sensor_data_t){.valid=1,.accepted_records=tick/100,.received_at_ms=tick,.time_ms=(uint16_t)tick,.steps_left=-(int32_t)tick,.steps_right=(int32_t)tick};
   mock_tx3_busy=(uint64_t)tick*1000<busy_until;
   tasks();for(unsigned k=0;robot_busy && k<8;k++) {
    if(!strcmp(robot_tx,"HB\n"))robot_ack("OK HB\n");
    else if(!strcmp(robot_tx,"PING\n"))robot_ack("PONG\n");
    else {assert(!strncmp(robot_tx,"STREAM ",7));robot_ack("OK STREAM\n");}
   }
   size_t n=strlen(uart3_output);
   if(n) {
    bytes+=n;busy_until=(uint64_t)tick*1000+(n*10000000ULL+APP_ESP_BAUD-1)/APP_ESP_BAUD;
    if(strstr(uart3_output,"UWB3 "))uwb++;
    if(strstr(uart3_output,"DATA2 ")){unsigned seq,age,source;assert(sscanf(strstr(uart3_output,"DATA2 "),"DATA2 %u %u %u",&seq,&age,&source)==3);assert(seq==sensors+1 && source==(uint16_t)(seq*100));sensors++;}
    uart3_output[0]=0;
   }
   assert(esp_valid && !esp_rx_overflows && !esp_uart_errors && !esp_dma_errors && !esp_tx_fault);
  }
  mock_tx3_busy=0;esp_link_task();if(strstr(uart3_output,"DATA2 "))sensors++;
  assert(sensors==3000 && uwb==300000/27 && !esp_sensor_coalesced && !esp_telemetry_drops);
  while(offset){char fragment[2]={noise[offset++],0};if(!noise[offset])offset=0;mock_dma_feed(fragment);}
  cmd("STOP");assert(!strcmp(robot_tx,"STOP\n"));robot_ack("OK STOP\n");
  printf("300s simulated: DMA RX27600000B, UWB=%u DATA2=%u output=%lluB; no errors/drops; STOP acknowledged\n",uwb,sensors,(unsigned long long)bytes);
 } else assert(0);
 puts("PASS");return 0;
}
