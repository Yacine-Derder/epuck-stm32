/* SPDX-License-Identifier: MIT */
#include "uwb_config.h"
#include <limits.h>
#include <math.h>
#include <string.h>
#define MAGIC UINT32_C(0x31474643)
#define COMMIT UINT32_C(0x43464721)
#define FIELDS 18u
static uint32_t get(const uint8_t *p) {
  return (uint32_t)p[0]|((uint32_t)p[1]<<8)|((uint32_t)p[2]<<16)|((uint32_t)p[3]<<24);
}
static void put(uint8_t *p,uint32_t v) { for(unsigned i=0;i<4;i++) p[i]=(uint8_t)(v>>(8*i)); }
static void fields(const uwb_config_t *c,uint32_t v[FIELDS]) {
  const uint32_t a[FIELDS]={c->node,c->peer,c->identity,c->mode,c->role,c->interval_ms,c->phy,
    c->tx_ant_dtu,c->rx_ant_dtu,c->calibration,(uint32_t)c->correction_mm,c->verbosity,
    c->telemetry,c->telemetry_ms,c->samples,c->reserved[0],c->reserved[1],c->reserved[2]};
  memcpy(v,a,sizeof(a));
}
static void assign(uwb_config_t *c,const uint32_t *v) {
  c->node=v[0];c->peer=v[1];c->identity=v[2];c->mode=v[3];c->role=v[4];c->interval_ms=v[5];c->phy=v[6];
  c->tx_ant_dtu=v[7];c->rx_ant_dtu=v[8];c->calibration=v[9];
  /* Explicit two's complement conversion without out-of-range signed cast. */
  c->correction_mm=v[10]<=INT32_MAX?(int32_t)v[10]:-1-(int32_t)(UINT32_MAX-v[10]);
  c->verbosity=v[11];c->telemetry=v[12];c->telemetry_ms=v[13];c->samples=v[14];
  for(unsigned i=0;i<3;i++) c->reserved[i]=v[15+i];
}
void uwb_config_defaults(uwb_config_t *c) {
  *c=(uwb_config_t){.node=0x86,.peer=0x84,.identity=0,.mode=0,.role=2,.interval_ms=1000,
    .tx_ant_dtu=16380,.rx_ant_dtu=16380,.verbosity=2,.telemetry_ms=1000};
}
const char *uwb_config_validate(const uwb_config_t *c) {
  if(c->node<0x84||c->node>0x86||c->peer<0x84||c->peer>0x86) return "address: pinned ADS protocol supports 0x84..0x86";
  if(c->node==c->peer) return "address: node and peer must differ";
  if(c->identity>2) return "identity: 0=standalone 1=ALR 2=RLR";
  if(c->mode>1) return "mode: only 0=off and 1=ADS implemented";
  if(c->role!=1&&c->role!=2) return "role: 1=initiator 2=responder";
  if(c->interval_ms<250||c->interval_ms>60000) return "interval_ms: 250..60000";
  if(c->phy) return "phy: only 0=mixed channel5/6M8/code9 supported";
  if(c->tx_ant_dtu>65535||c->rx_ant_dtu>65535) return "antenna delay: 0..65535 DTU";
  if(c->calibration>1||c->correction_mm < -10000||c->correction_mm>10000) return "calibration: 0/1; correction_mm: -10000..10000";
  if(c->verbosity>2||c->telemetry>1||c->samples>1) return "verbosity: 0..2; telemetry/samples: 0/1";
  if(c->telemetry_ms<1000||c->telemetry_ms>60000) return "telemetry_ms: 1000..60000";
  if(c->reserved[0]||c->reserved[1]||c->reserved[2]) return "reserved fields must be zero";
  return NULL;
}
bool uwb_config_equal(const uwb_config_t *a,const uwb_config_t *b) {
  uint32_t av[FIELDS],bv[FIELDS]; fields(a,av);fields(b,bv);return memcmp(av,bv,sizeof(av))==0;
}
const char *uwb_config_set(uwb_config_t *c,const char *field,const char *value) {
  static const char *names[]={"node","peer","identity","mode","role","interval_ms","phy","tx_ant_dtu","rx_ant_dtu",
    "calibration","correction_mm","verbosity","telemetry","telemetry_ms","samples"};
  unsigned index=15;for(unsigned i=0;i<15;i++) if(!strcmp(field,names[i])) {index=i;break;}
  if(index==15) return "unknown field";
  const char *p=value;bool negative=false;unsigned base=10;
  if(index==10 && *p=='-') {negative=true;p++;}
  if(index<2 && p[0]=='0'&&p[1]=='x') {base=16;p+=2;}
  if(!*p) return "invalid number";
  uint32_t n=0;for(;*p;p++) {
    unsigned digit=*p>='0'&&*p<='9'?(unsigned)(*p-'0'):*p>='a'&&*p<='f'?(unsigned)(*p-'a'+10):*p>='A'&&*p<='F'?(unsigned)(*p-'A'+10):99;
    if(digit>=base||n>(UINT32_MAX-digit)/base) return "invalid number";
    n=n*base+digit;
  }
  if(index==10 && n>INT32_MAX) return "correction_mm overflow";
  uint32_t v[FIELDS];fields(c,v);v[index]=negative?0u-n:n;
  uwb_config_t candidate;assign(&candidate,v);const char *error=uwb_config_validate(&candidate);
  if(!error) *c=candidate;
  return error;
}
uint32_t uwb_config_crc(const uint8_t *data,size_t size) {
  uint32_t crc=UINT32_MAX;
  for(size_t i=0;i<size;i++) { crc^=data[i];for(unsigned j=0;j<8;j++) crc=(crc>>1)^((0u-(crc&1))&UINT32_C(0xEDB88320)); }
  return ~crc;
}
void uwb_config_encode(uint8_t out[UWB_CONFIG_RECORD_SIZE],const uwb_config_t *c,uint32_t generation) {
  memset(out,0,UWB_CONFIG_RECORD_SIZE);put(out,MAGIC);put(out+4,UWB_CONFIG_VERSION);put(out+8,UWB_CONFIG_RECORD_SIZE);put(out+12,generation);
  uint32_t v[FIELDS];fields(c,v);for(unsigned i=0;i<FIELDS;i++) put(out+16+4*i,v[i]);
  put(out+120,uwb_config_crc(out,120));put(out+124,COMMIT);
}
bool uwb_config_decode(const uint8_t in[UWB_CONFIG_RECORD_SIZE],uwb_config_t *c,uint32_t *generation) {
  if(get(in)!=MAGIC||get(in+4)!=UWB_CONFIG_VERSION||get(in+8)!=UWB_CONFIG_RECORD_SIZE||get(in+124)!=COMMIT||get(in+120)!=uwb_config_crc(in,120)) return false;
  for(unsigned i=88;i<120;i++) if(in[i]) return false;
  uint32_t v[FIELDS];for(unsigned i=0;i<FIELDS;i++) v[i]=get(in+16+4*i);
  uwb_config_t candidate;assign(&candidate,v);if(uwb_config_validate(&candidate)) return false;
  *c=candidate;*generation=get(in+12);return true;
}
typedef struct {bool valid;uint32_t generation,offset,free[2];uwb_config_t config;} scan_t;
static bool scan(const uwb_config_store_t *s,scan_t *result) {
  *result=(scan_t){.free={UINT32_MAX,UINT32_MAX}};
  for(uint32_t at=0;at<2*UWB_CONFIG_SECTOR_SIZE;at+=UWB_CONFIG_RECORD_SIZE) {
    uint8_t record[UWB_CONFIG_RECORD_SIZE];if(!s->read(s->context,at,record,sizeof(record))) return false;
    bool erased=true;for(unsigned i=0;i<sizeof(record);i++) if(record[i]!=255) {erased=false;break;}
    unsigned bank=at/UWB_CONFIG_SECTOR_SIZE;
    if(erased&&result->free[bank]==UINT32_MAX) result->free[bank]=at;
    uwb_config_t c;uint32_t gen;
    if(uwb_config_decode(record,&c,&gen)&&(!result->valid||((uint32_t)(gen-result->generation)<UINT32_C(0x80000000) && gen!=result->generation))) {
      result->valid=true;result->generation=gen;result->offset=at;result->config=c;
    }
  }
  return true;
}
bool uwb_config_load(const uwb_config_store_t *s,uwb_config_t *c,uint32_t *generation) {
  scan_t found;if(!scan(s,&found)||!found.valid) return false;
  *c=found.config;*generation=found.generation;return true;
}
bool uwb_config_save(const uwb_config_store_t *s,const uwb_config_t *c,uint32_t *generation) {
  scan_t found;if(uwb_config_validate(c)||!scan(s,&found)) return false;
  if(found.valid&&uwb_config_equal(c,&found.config)) {*generation=found.generation;return true;}
  unsigned bank=found.valid?found.offset/UWB_CONFIG_SECTOR_SIZE:0;
  uint32_t at=found.free[bank];
  if(at==UINT32_MAX) {
    if(found.valid) bank^=1u; /* Never erase the newest committed sector. */
    if(!s->erase(s->context,bank)) return false;
    at=bank*UWB_CONFIG_SECTOR_SIZE;
  }
  uint32_t gen=found.valid?found.generation+1:1;
  uint8_t record[UWB_CONFIG_RECORD_SIZE],verify[UWB_CONFIG_RECORD_SIZE];uwb_config_encode(record,c,gen);
  for(unsigned i=0;i<sizeof(record);i+=4) if(!s->program(s->context,at+i,get(record+i))) return false;
  /* Commit word is last. Verify through the read path before reporting saved. */
  if(!s->read(s->context,at,verify,sizeof(verify))||memcmp(verify,record,sizeof(record))) return false;
  *generation=gen;return true;
}
bool uwb_config_range(const uwb_config_t *c,double raw,bool valid,double *calibrated) {
  *calibrated=raw+(c->calibration ? c->correction_mm/1000.0 : 0.0);
  return valid&&isfinite(raw)&&raw>=0&&isfinite(*calibrated)&&*calibrated>=0;
}
