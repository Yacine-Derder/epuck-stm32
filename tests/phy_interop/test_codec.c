#include "phy_diag.h"
#include <assert.h>
#include <string.h>
int main(void) {
  uint8_t frame[32],src=99; uint32_t seq=99;
  const uint8_t golden[32]={0x41,0x88,0,0xcf,0xbc,0xff,0xff,0x84,0,'M','3','F','I',1,0,0,0,0,
    0x21,0x20,0x23,0x22,0x25,0x24,0x27,0x26,0x29,0x28,0x2b,0x2a,0x2d,0x2c};
  phyDiagEncode(frame,0x84,0); assert(memcmp(frame,golden,32)==0);
  for (unsigned i=0;i<32;i++) { frame[i]^=1; assert(!phyDiagDecode(frame,32,&src,&seq)); frame[i]^=1; }
  assert(src==99 && seq==99);
  assert(!phyDiagDecode(frame,31,&src,&seq)); assert(!phyDiagDecode(frame,34,&src,&seq));
  phyDiagEncode(frame,0x86,0x12345678); assert(phyDiagDecode(frame,32,&src,&seq)); assert(src==0x86 && seq==0x12345678);
  phyDiagStats_t s={0}; phyDiagObserve(&s,UINT32_MAX); phyDiagObserve(&s,0); phyDiagObserve(&s,0);
  phyDiagObserve(&s,2); phyDiagObserve(&s,1); assert(s.gaps==1 && s.duplicates==1 && s.old==1);
}
