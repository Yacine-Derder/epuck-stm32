/* SPDX-License-Identifier: MIT */
#include "uwb_config.h"
#include "mul_common.h"
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
    c->telemetry,c->telemetry_ms,c->samples,c->wifi,c->telemetry_format,c->peer_count};
  memcpy(v,a,sizeof(a));
}
static void assign(uwb_config_t *c,const uint32_t *v) {
  c->node=v[0];c->peer=v[1];c->identity=v[2];c->mode=v[3];c->role=v[4];c->interval_ms=v[5];c->phy=v[6];
  c->tx_ant_dtu=v[7];c->rx_ant_dtu=v[8];c->calibration=v[9];
  /* Explicit two's complement conversion without out-of-range signed cast. */
  c->correction_mm=v[10]<=INT32_MAX?(int32_t)v[10]:-1-(int32_t)(UINT32_MAX-v[10]);
  c->verbosity=v[11];c->telemetry=v[12];c->telemetry_ms=v[13];c->samples=v[14];
  c->wifi=v[15];c->telemetry_format=v[16];c->peer_count=v[17];
}
void uwb_config_defaults(uwb_config_t *c) {
  *c=(uwb_config_t){.node=0x00,.peer=0x01,.identity=0,.mode=0,.role=2,.interval_ms=1000,
    .reference=0x84,.follower=0x85,.tx_ant_dtu=16380,.rx_ant_dtu=16380,.verbosity=2,.telemetry_ms=1000,.peer_count=1,.peers={{1,1,1000}}};
}
const char *uwb_config_validate(const uwb_config_t *c) {
  if(c->node>255||c->peer>255) return "address/peer: 0x00..0xFF or decimal 0..255";
  if(c->node==c->peer) return "address: node and peer must differ";
  mulNetwork_t network;
  if(c->reference>254||c->follower>254||!mulNetworkInit(&network,(uint8_t)c->reference,(uint8_t)c->follower))return "reference/follower: distinct IDs 0..254";
  if(c->rf_enabled>1||(!c->mode&&c->rf_enabled))return "RF: on/off; select a mode before on";
  if(c->protocol_role!=UWB_PROTOCOL_STANDALONE&&c->protocol_role!=UWB_PROTOCOL_RLR)return "protocol role: standalone or rlr; ref/follower unavailable";
  if(c->protocol_role==UWB_PROTOCOL_RLR&&(c->mode==1||c->mode==2))return "role rlr is passive; choose role standalone before a transmitting mode";
  if(c->mode>=4&&c->protocol_role!=UWB_PROTOCOL_RLR)return "hybrid requires protocol role rlr";
  if(c->identity>2) return "identity: standalone, alr, rlr";
  if(c->mode>5) return "mode: off, twr, beacon, listen, hybrid, hybrid_static";
  if(c->role!=1&&c->role!=2) return "role: initiator, responder";
  if(c->interval_ms<250||c->interval_ms>60000) return "interval_ms: 250..60000";
  if(c->phy) return "phy: only mixed (channel5/6M8/code9) supported";
  if(c->tx_ant_dtu>65535||c->rx_ant_dtu>65535) return "antenna delay: 0..65535 DTU";
  if(c->calibration>1||c->correction_mm < -10000||c->correction_mm>10000) return "calibration: on/off; correction_mm: -10000..10000";
  if(c->verbosity>2||c->telemetry>1||c->samples>1) return "verbosity: quiet/summary/full; telemetry/samples: on/off";
  if(c->telemetry_ms<1000||c->telemetry_ms>60000) return "telemetry_ms: 1000..60000";
  if(c->wifi>1||c->telemetry_format) return "wifi: on/off; telemetry_format: compact";
  if(!c->peer_count||c->peer_count>UWB_MAX_PEERS) return "peer count: 1..8";
  if(c->peer!=c->peers[0].address||c->interval_ms!=c->peers[0].interval_ms) return "primary peer alias mismatch";
  for(unsigned i=0;i<UWB_MAX_PEERS;i++) {
    const uwb_peer_t *p=&c->peers[i];
    if(i>=c->peer_count) {if(p->address||p->enabled||p->interval_ms)return "unused peers must be zero";continue;}
    if(p->address==c->node||p->enabled>1||p->interval_ms<250||p->interval_ms>60000)return "invalid peer/self/interval";
    for(unsigned j=0;j<i;j++)if(p->address==c->peers[j].address)return "duplicate peer";
  }
  if(c->mode>=4) {
    uint8_t local[8]={(uint8_t)c->node,0,0,0,0,0,0xcf,0xbc};bool roles[MUL_ALR_COUNT]={false};
    if(mulNetworkRole(&network,local)!=mulAlrUnsupported)return "hybrid requires an RLR address distinct from configured ALRs";
    for(unsigned i=0;i<c->peer_count;i++)if(c->peers[i].enabled){uint8_t id[8]={c->peers[i].address,0,0,0,0,0,0xcf,0xbc};mulAlrRole_t r=mulNetworkRole(&network,id);if(r>=0&&(unsigned)r<MUL_ALR_COUNT)roles[r]=true;}
    for(unsigned i=0;i<MUL_ALR_COUNT;i++)if(c->rf_enabled&&!roles[i])return "hybrid requires both configured reference/follower IDs as enabled peers";
  }
  return NULL;
}
bool uwb_config_equal(const uwb_config_t *a,const uwb_config_t *b) {
  uint32_t av[FIELDS],bv[FIELDS]; fields(a,av);fields(b,bv);return a->protocol_role==b->protocol_role && a->rf_enabled==b->rf_enabled && a->reference==b->reference && a->follower==b->follower && memcmp(av,bv,sizeof(av))==0 && memcmp(a->peers,b->peers,sizeof(a->peers))==0;
}
const char *uwb_config_set(uwb_config_t *c,const char *field,const char *value) {
  char fieldLower[24],valueLower[24];
  size_t f=0,valueLength=0;
  while(f<sizeof(fieldLower)-1&&field[f]) {fieldLower[f]=field[f]>='A'&&field[f]<='Z'?field[f]+('a'-'A'):field[f];f++;}
  while(valueLength<sizeof(valueLower)-1&&value[valueLength]) {valueLower[valueLength]=value[valueLength]>='A'&&value[valueLength]<='Z'?value[valueLength]+('a'-'A'):value[valueLength];valueLength++;}
  if(field[f]||value[valueLength]) return "field/value too long";
  fieldLower[f]=valueLower[valueLength]=0;field=fieldLower;value=valueLower;
  if(!strcmp(field,"address"))field="node";
  if(!strcmp(field,"rf_enabled")) {
    uwb_config_t candidate=*c;
    if(!strcmp(value,"on")||!strcmp(value,"1"))candidate.rf_enabled=1;
    else if(!strcmp(value,"off")||!strcmp(value,"0"))candidate.rf_enabled=0;
    else return "rf_enabled: on/off";
    const char *error=uwb_config_validate(&candidate);if(!error)*c=candidate;return error;
  }
  if(!strcmp(field,"reference")||!strcmp(field,"follower")) {
    const char *p=value;unsigned base=10;uint32_t n=0;
    if(p[0]=='0'&&p[1]=='x'){base=16;p+=2;}if(!*p)return "invalid roster ID";
    for(;*p;p++){unsigned d=*p>='0'&&*p<='9'?*p-'0':*p>='a'&&*p<='f'?*p-'a'+10:99;
      if(d>=base||n>(254-d)/base)return "roster ID: 0..254";
      n=n*base+d;}
    uwb_config_t candidate=*c;
    if(!strcmp(field,"reference"))candidate.reference=n;else candidate.follower=n;
    const char *error=uwb_config_validate(&candidate);if(!error)*c=candidate;return error;
  }
  static const char *names[]={"node","peer","identity","mode","role","interval_ms","phy","tx_ant_dtu","rx_ant_dtu",
    "calibration","correction_mm","verbosity","telemetry","telemetry_ms","samples","wifi","telemetry_format"};
  unsigned index=17;for(unsigned i=0;i<17;i++) if(!strcmp(field,names[i])) {index=i;break;}
  if(index==17) return "unknown field; use CFG HELP";
  static const struct {unsigned field;const char *name;const char *number;} aliases[]={
    {2,"standalone","0"},{2,"alr","1"},{2,"rlr","2"},
    {3,"off","0"},{3,"twr","1"},{3,"beacon","2"},{3,"listen","3"},{3,"hybrid","4"},{3,"hybrid_static","5"},{4,"initiator","1"},{4,"responder","2"},
    {6,"mixed","0"},{9,"off","0"},{9,"on","1"},
    {11,"quiet","0"},{11,"summary","1"},{11,"full","2"},
    {12,"off","0"},{12,"on","1"},{14,"off","0"},{14,"on","1"},{15,"off","0"},{15,"on","1"},{16,"compact","0"}};
  for(unsigned i=0;i<sizeof(aliases)/sizeof(*aliases);i++)
    if(aliases[i].field==index && !strcmp(aliases[i].name,value)) {value=aliases[i].number;break;}
  if((index==2||index==3||index==4||index==6||index==9||index==11||index==12||index==14||index==15||index==16) &&
     (value[0]<'0'||value[0]>'9')) {
    static const char *errors[]={"identity: standalone, alr, rlr","mode: off, twr, beacon, listen, hybrid, hybrid_static","role: initiator, responder"};
    if(index>=2&&index<=4) return errors[index-2];
    return index==16?"telemetry_format: compact":index==6?"phy: mixed":index==11?"verbosity: quiet, summary, full":"value: on, off";
  }
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
  uwb_config_t candidate=*c;assign(&candidate,v);
  if(index==3){candidate.protocol_role=candidate.mode>=4?UWB_PROTOCOL_RLR:UWB_PROTOCOL_STANDALONE;if(!candidate.mode)candidate.rf_enabled=0;}
  if((index==7||index==8)&&v[index]!=(index==7?c->tx_ant_dtu:c->rx_ant_dtu)) candidate.calibration=0;
  if(index==1) {if(candidate.peer!=c->peer)candidate.calibration=0;candidate.peers[0].address=(uint8_t)candidate.peer;}
  if(index==5) for(unsigned i=0;i<candidate.peer_count;i++)candidate.peers[i].interval_ms=(uint16_t)candidate.interval_ms;
  const char *error=uwb_config_validate(&candidate);
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
  /* v3 packs roster bytes; v4 adds RF enable in bit24 and protocol role in bits25..26 of the node word.
   * Record size, peer offsets, CRC and commit-last journal geometry stay fixed. */
  put(out+16,c->node|(c->reference<<8)|(c->follower<<16)|(c->rf_enabled<<24)|(c->protocol_role<<25));
  for(unsigned i=0;i<UWB_MAX_PEERS;i++) {out[88+4*i]=c->peers[i].address;out[89+4*i]=c->peers[i].enabled;out[90+4*i]=(uint8_t)c->peers[i].interval_ms;out[91+4*i]=(uint8_t)(c->peers[i].interval_ms>>8);}
  put(out+120,uwb_config_crc(out,120));put(out+124,COMMIT);
}
bool uwb_config_decode(const uint8_t in[UWB_CONFIG_RECORD_SIZE],uwb_config_t *c,uint32_t *generation) {
  if(get(in)!=MAGIC||(get(in+4)!=1&&get(in+4)!=2&&get(in+4)!=3&&get(in+4)!=UWB_CONFIG_VERSION)||get(in+8)!=UWB_CONFIG_RECORD_SIZE||get(in+124)!=COMMIT||get(in+120)!=uwb_config_crc(in,120)) return false;

  uint32_t v[FIELDS];for(unsigned i=0;i<FIELDS;i++) v[i]=get(in+16+4*i);
  uwb_config_t candidate={0};assign(&candidate,v);
  candidate.reference=0x84;candidate.follower=0x85;candidate.rf_enabled=candidate.mode!=0;candidate.protocol_role=candidate.mode>=4?UWB_PROTOCOL_RLR:UWB_PROTOCOL_STANDALONE;
  if(get(in+4)>=3) {
    if((get(in+4)==3&&(v[0]&UINT32_C(0xFF000000)))||(v[0]&UINT32_C(0xF8000000)))return false;
    if(get(in+4)==4){candidate.rf_enabled=(v[0]>>24)&1;candidate.protocol_role=(v[0]>>25)&3;}
    candidate.node=v[0]&255;candidate.reference=(v[0]>>8)&255;candidate.follower=(v[0]>>16)&255;
  }
  if(get(in+4)==1) {
    if(candidate.wifi||candidate.telemetry_format||candidate.peer_count)return false;
    for(unsigned i=88;i<120;i++)if(in[i])return false;
    candidate.peer_count=1;candidate.peers[0]=(uwb_peer_t){(uint8_t)candidate.peer,1,(uint16_t)candidate.interval_ms};
  } else for(unsigned i=0;i<UWB_MAX_PEERS;i++) candidate.peers[i]=(uwb_peer_t){in[88+4*i],in[89+4*i],(uint16_t)(in[90+4*i]|(in[91+4*i]<<8))};
  if(uwb_config_validate(&candidate)) return false;
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
