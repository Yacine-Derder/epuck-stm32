/* Real bundled decoder and SPI addressing, not a copy of the sign extension.
 * Function/data section GC discards unrelated device initialization paths. */
#include <assert.h>
#include <stdio.h>
#include "../../ThirdParty/qorvo_dw3/driver/dw3000/dw3000_device.c"
static uint32_t sample,expected;
static uint16_t expectedLength;
static unsigned reads;
static int32_t read_spi(uint16_t hn,uint8_t *header,uint16_t n,uint8_t *out) {
 assert(hn==2&&n==expectedLength);
 uint32_t file=(header[0]&0x3eU)>>1,offset=((uint32_t)(header[0]&1U)<<6)|(header[1]>>2);
 assert((file<<16|offset)==expected);reads++;
 for(unsigned i=0;i<n;i++)out[i]=(uint8_t)(sample>>(8*i));
 return 0;
}
uint8_t dwt_generatecrc8(const uint8_t *data,uint32_t length,uint8_t crc) {
 (void)data;(void)length;(void)crc;assert(!"SPI CRC should not be requested");return 0;
}
int main(void) {
 struct dwt_spi_s spi={.readfromspi=read_spi};dwchip_t chip={.SPI=&spi};
 expected=DRX_DIAG3_ID;expectedLength=3;
 const int32_t ci[]={0,1,-1,1048575,-1048576,12345,-12345};
 for(unsigned i=0;i<sizeof(ci)/sizeof(*ci);i++){sample=(uint32_t)ci[i]&0x1fffff;assert(ull_readcarrierintegrator(&chip)==ci[i]);}
 expected=CIA_DIAG_0_ID;expectedLength=2;
 for(int32_t n=-4096;n<=4095;n++){sample=0xe000U|((uint32_t)n&0x1fff);assert(ull_readclockoffset(&chip)==n);}
 assert(reads==8199);
 assert(FREQ_OFFSET_MULTIPLIER==3.719329833984375);
 printf("Bundled DW3000 carrier21/CIA13: raw endpoints, every signed COE code, masks, SPI registers and local API multiplier passed\n");
 return 0;
}
