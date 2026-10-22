#define MUL_RUNTIME_REUSE
#include "../runtime_config/test_runtime.c"
static void frame(uint32_t events,unsigned bytes) {
 const uint8_t ref[8]={0x84,0,0,0,0,0,0xcf,0xbc};
 mulEncodeTestPacket(input,ref,10,100000);length=bytes;rxMarker=MUL_TIMESTAMP_MASK-638000;status=events;step();
}
int main(void) {
 memset(nv,255,sizeof(nv));uwb_runtime_init();command("cfg set address 0x8d");
 command("peer add 0x84");command("peer add 0x85");command("cfg set verbosity quiet");command("cfg set mode hybrid");
 unsigned tx=starts,writes=nvWrites;frame(DWT_INT_RXFCG_BIT_MASK|DWT_INT_CIADONE_BIT_MASK,36);
 assert(hybridFrames[0].age==1&&hybridFrames[0].rx==MUL_TIMESTAMP_MASK-638000);
 char line[320];assert(mul_hybrid_radio_frequency(0,line,sizeof(line))&&strlen(line)<317);
#if MUL_HYBRID_FREQ_DIAG
 puts(line); /* Also consumable by the independent host decoder. */
 assert(frequencyConfigurations>0&&frequencyCiReads==1&&frequencyCoeReads==1);
 assert(frequencyCiOrder<frequencyCoeOrder&&frequencyCoeOrder<frequencySystemOrder);
 assert(strstr(line,"ci_raw=-12345 coe_raw=-2048 coe_present=1")&&strstr(line,"seq=10")&&strstr(line,"metric_rate=UNKNOWN"));
 frame(DWT_INT_RXFCG_BIT_MASK|DWT_INT_RXFCE_BIT_MASK|DWT_INT_CIADONE_BIT_MASK,36);
 assert(frequencyCiReads==1&&frequencyCoeReads==1); /* CRC error wins. */
 frame(DWT_INT_RXFCG_BIT_MASK|DWT_INT_CIADONE_BIT_MASK,20);
 assert(frequencyCiReads==1); /* Wrong length never reads diagnostic registers. */
 frame(DWT_INT_RXFCG_BIT_MASK,36);
 assert(frequencyCiReads==2&&frequencyCoeReads==1&&!hybridFrames[0].coePresent);
 assert(mul_hybrid_radio_frequency(0,line,sizeof(line))&&strstr(line,"coe_present=0"));
 frequencyCi=frequencyCoe=0;frame(DWT_INT_RXFCG_BIT_MASK|DWT_INT_CIADONE_BIT_MASK,36);
 assert(mul_hybrid_radio_frequency(0,line,sizeof(line))&&strstr(line,"ci_raw=0 coe_raw=0 coe_present=1"));
 unsigned reads=frequencyCiReads+frequencyCoeReads;
#endif
 captured[0]=0;command("hybrid frequency");assert(strstr(captured,"MULH FREQ")&&strstr(captured,"metric_rate=UNKNOWN"));
 assert(starts==tx&&nvWrites==writes&&active.mode==4);
#if MUL_HYBRID_FREQ_DIAG
 assert(reads==frequencyCiReads+frequencyCoeReads); /* Command is snapshot-only. */
 /* Full-width diagnostic output remains within the unmodified USB bound. */
 hybridFrames[0].ci=-1048576;hybridFrames[0].coe=-4096;
 hybridFrames[0].observedMs=hybridFrames[0].age=UINT32_MAX;
 assert(mul_hybrid_radio_frequency(0,line,sizeof(line))&&strlen(line)<317);
#else
 assert(strstr(line,"disabled"));assert(!mul_hybrid_radio_frequency(1,line,sizeof(line)));
#endif
 puts("Hybrid raw frequency diagnostics: default-off/no-TX/no-NV, pre-rearm capture, CRC/shape/CIA guards, zero/present and USB bounds passed");
 return 0;
}
