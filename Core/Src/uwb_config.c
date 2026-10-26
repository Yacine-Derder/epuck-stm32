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
    .slot_ms=10,.reference=0x84,.follower=0x85,.tx_ant_dtu=16380,.rx_ant_dtu=16380,.verbosity=2,.telemetry_ms=1000,.peer_count=1,.peers={{1,1,1000}}};
}
void uwb_config_clock(const uwb_config_t *c,mulClockFitConfig_t *clock) {
 if(c->clock.periodMs)*clock=c->clock;else {mulClockFitDefaults(clock);if(c->schedule.profile)clock->periodMs=c->schedule.periodMs;}
}
void uwb_config_schedule(const uwb_config_t *c,mulSchedule_t *schedule) {
 if(c->schedule.profile)*schedule=c->schedule;
 else mulScheduleLegacy(schedule,(uint8_t)c->reference,(uint8_t)c->follower,(uint16_t)c->slot_ms);
}
void uwb_config_effective_schedule(const uwb_config_t *c,mulSchedule_t *schedule) {
 if(c->plan_present&&c->plan_selected)*schedule=c->plan.broadcast;else uwb_config_schedule(c,schedule);
}
void uwb_config_effective_clock(const uwb_config_t *c,mulClockFitConfig_t *clock) {
 uwb_config_clock(c,clock);if(c->plan_present&&c->plan_selected)clock->periodMs=c->plan.broadcast.periodMs;
}
const char *uwb_config_plan_set(uwb_config_t *c,const mulRangingPlan_t *plan,bool selected) {
 if(!c||!plan)return "missing plan/configuration";
 if(c->rf_enabled)return "plan edit requires RF OFF";
 uwb_config_t candidate=*c;candidate.plan=*plan;candidate.plan_present=1;candidate.plan_selected=selected;
 const char *error=uwb_config_validate(&candidate);if(!error)*c=candidate;return error;
}
const char *uwb_config_roster(uwb_config_t *c,const uint8_t *ids,unsigned count) {
 if(c&&c->plan_selected)return "deselect plan before editing retained roster";
 if(!c||!ids||count<2||count>MUL_MAX_ALRS)return "ALR count: 2..8";
 uwb_config_t candidate=*c;mulSchedule_t schedule;uwb_config_schedule(c,&schedule);
 schedule.count=(uint8_t)count;memset(schedule.ids,0,sizeof(schedule.ids));memcpy(schedule.ids,ids,count);
 if(schedule.profile!=MUL_PROFILE_EXPERIMENTAL)schedule.profile=count==2?MUL_PROFILE_LEGACY:MUL_PROFILE_MULTI;
 const char *error=mulScheduleValidate(&schedule);if(error)return error;
 candidate.reference=ids[0];candidate.follower=ids[1];
 candidate.schedule=schedule.profile==MUL_PROFILE_LEGACY?(mulSchedule_t){0}:schedule;
 error=uwb_config_validate(&candidate);if(!error)*c=candidate;return error;
}
const char *uwb_config_validate(const uwb_config_t *c) {
  if(c->plan_present>1||c->plan_selected>1||(!c->plan_present&&c->plan_selected))return "invalid plan selection";
  if(c->plan_present) {
    const char *error=mulRangingPlanValidate(&c->plan,NULL);if(error)return error;
    if(c->plan_selected) {
      if(c->mode<4||c->protocol_role!=UWB_PROTOCOL_RLR)return "selected plan requires hybrid RLR mode";
      if(c->node>254||mulScheduleIndex(&c->plan.broadcast,(uint8_t)c->node)>=0)return "plan RLR must be outside ALR roster";
      if(c->rf_enabled&&(c->plan.enabled&MUL_PLAN_AR))return "active ALR-RLR adapter pending; RF stays OFF";
    }
  }
  mulClockFitConfig_t clock;uwb_config_clock(c,&clock);
  if(!mulClockFitConfigValid(&clock)||clock.periodMs!=(c->schedule.profile?c->schedule.periodMs:1000))return "clock configuration/period invalid";
  mulSchedule_t schedule;uwb_config_schedule(c,&schedule);
  const char *scheduleError=mulScheduleValidate(&schedule);if(scheduleError)return scheduleError;
  if(schedule.ids[0]!=c->reference||schedule.ids[1]!=c->follower||schedule.slotMs!=c->slot_ms)return "schedule aliases disagree";
  if(c->node>255||c->peer>255) return "address/peer: 0x00..0xFF or decimal 0..255";
  if(c->node==c->peer) return "address: node and peer must differ";
  mulNetwork_t network;
  if(c->reference>254||c->follower>254||!mulNetworkInit(&network,(uint8_t)c->reference,(uint8_t)c->follower))return "reference/follower: distinct IDs 0..254";
  if(c->slot_ms>UINT16_MAX||!c->slot_ms)return "slot_ms: 1..65535";
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
  if(c->mode>=4&&!c->plan_selected&&schedule.profile>=MUL_PROFILE_MULTI) {
    if(mulScheduleIndex(&schedule,(uint8_t)c->node)>=0)return "RLR identity must be outside ALR roster";
  }
  if(c->mode>=4&&!c->plan_selected&&schedule.profile==MUL_PROFILE_LEGACY) {
    uint8_t local[8]={(uint8_t)c->node,0,0,0,0,0,0xcf,0xbc};bool roles[MUL_ALR_COUNT]={false};
    if(mulNetworkRole(&network,local)!=mulAlrUnsupported)return "hybrid requires an RLR address distinct from configured ALRs";
    for(unsigned i=0;i<c->peer_count;i++)if(c->peers[i].enabled){uint8_t id[8]={c->peers[i].address,0,0,0,0,0,0xcf,0xbc};mulAlrRole_t r=mulNetworkRole(&network,id);if(r>=0&&(unsigned)r<MUL_ALR_COUNT)roles[r]=true;}
    for(unsigned i=0;i<MUL_ALR_COUNT;i++)if(c->rf_enabled&&!roles[i])return "hybrid requires both configured reference/follower IDs as enabled peers";
  }
  return NULL;
}
bool uwb_config_equal(const uwb_config_t *a,const uwb_config_t *b) {
  if(a->plan_present!=b->plan_present||a->plan_selected!=b->plan_selected)return false;
  if(a->plan_present) {
    uint8_t ap[MUL_PLAN_BYTES],bp[MUL_PLAN_BYTES];
    if(!mulRangingPlanEncode(ap,&a->plan)||!mulRangingPlanEncode(bp,&b->plan)||memcmp(ap,bp,sizeof(ap)))return false;
  }
  uint32_t av[FIELDS],bv[FIELDS]; fields(a,av);fields(b,bv);return memcmp(&a->clock,&b->clock,sizeof(a->clock))==0 && memcmp(&a->schedule,&b->schedule,sizeof(a->schedule))==0 && a->slot_ms==b->slot_ms && a->protocol_role==b->protocol_role && a->rf_enabled==b->rf_enabled && a->reference==b->reference && a->follower==b->follower && memcmp(av,bv,sizeof(av))==0 && memcmp(a->peers,b->peers,sizeof(a->peers))==0;
}
static bool decimal(const char *value,uint32_t maximum,uint32_t *out) {
 uint32_t n=0;if(!*value)return false;
 for(const char *p=value;*p;p++){
  if(*p<'0'||*p>'9')return false;
  unsigned digit=(unsigned)(*p-'0');
  if(n>maximum/10||(n==maximum/10&&digit>maximum%10))return false;
  n=n*10+digit;
 }
 if(n>maximum)return false;
 *out=n;return true;
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
  if(!strcmp(field,"clock_estimator")||!strcmp(field,"clock_window")||!strcmp(field,"clock_compensate")) {
    uwb_config_t candidate=*c;uwb_config_clock(c,&candidate.clock);
    if(!strcmp(field,"clock_estimator")) {
      if(!strcmp(value,"median"))candidate.clock.estimator=0;
      else if(!strcmp(value,"regression"))candidate.clock.estimator=1;
      else return "clock_estimator: median/regression";
    }else if(!strcmp(field,"clock_window")) {
      unsigned n=0;for(const char *p=value;*p;p++){if(*p<'0'||*p>'9'||n>32)return "clock_window: 8..32";n=n*10+(*p-'0');}
      if(n<8||n>32)return "clock_window: 8..32";
      candidate.clock.window=(uint8_t)n;
    }else {
      if(!strcmp(value,"on"))candidate.clock.compensate=1;
      else if(!strcmp(value,"off"))candidate.clock.compensate=0;
      else return "clock_compensate: on/off";
    }
    const char *error=uwb_config_validate(&candidate);if(!error)*c=candidate;return error;
  }
  if(c->plan_selected) {
    static const char *retained[]={"cycle_ms","tx_lead_us","rx_budget_us","airtime_us","guard_us","alr_alr_twr","twr_reply_ms","schedule_revision","profile","slot_ms","reference","follower"};
    for(unsigned i=0;i<sizeof(retained)/sizeof(*retained);i++)if(!strcmp(field,retained[i]))return "deselect plan before editing retained schedule";
  }
  if(!strcmp(field,"cycle_ms")||!strcmp(field,"tx_lead_us")||!strcmp(field,"rx_budget_us")||!strcmp(field,"airtime_us")||!strcmp(field,"guard_us")||!strcmp(field,"alr_alr_twr")||!strcmp(field,"twr_reply_ms")) {
    if(c->schedule.profile!=MUL_PROFILE_EXPERIMENTAL)return !strcmp(field,"cycle_ms")&&!strcmp(value,"1000")?NULL:"select experimental profile for timing budgets";
    uwb_config_t candidate=*c;uint32_t n;
    if(!strcmp(field,"alr_alr_twr")) {
      if(!strcmp(value,"on"))candidate.schedule.rangingFlags|=MUL_RANGING_ALR_ALR;
      else if(!strcmp(value,"off"))candidate.schedule.rangingFlags&=~MUL_RANGING_ALR_ALR;
      else return "alr_alr_twr: on/off";
    }else if(!strcmp(field,"twr_reply_ms")) {
      unsigned code=!strcmp(value,"20")?0:!strcmp(value,"10")?2:!strcmp(value,"5")?4:255;
      if(code==255)return "twr_reply_ms:20/10/5";
      candidate.schedule.rangingFlags=(candidate.schedule.rangingFlags&~MUL_RANGING_REPLY_MASK)|code;
    }else {
      if(!decimal(value,UINT16_MAX,&n))return "timing: decimal 0..65535";
      if(!strcmp(field,"cycle_ms")){candidate.schedule.periodMs=(uint16_t)n;if(candidate.clock.periodMs)candidate.clock.periodMs=(uint16_t)n;}
      else if(!strcmp(field,"tx_lead_us"))candidate.schedule.txLeadUs=(uint16_t)n;
      else if(!strcmp(field,"rx_budget_us"))candidate.schedule.rxBudgetUs=(uint16_t)n;
      else if(!strcmp(field,"airtime_us"))candidate.schedule.airtimeUs=(uint16_t)n;
      else {if(n%100||n<100||n>10000)return "guard_us: 100..10000 step100";candidate.schedule.guard100us=(uint8_t)(n/100);}
    }
    const char *error=uwb_config_validate(&candidate);if(!error)*c=candidate;return error;
  }
  if(!strcmp(field,"schedule_revision")) {
    if(!c->schedule.profile)return "revision applies to multi profile";
    uint32_t revision=0;if(!*value)return "revision: nonzero uint32";
    for(const char *p=value;*p;p++){if(*p<'0'||*p>'9'||revision>(UINT32_MAX-(unsigned)(*p-'0'))/10)return "revision: nonzero uint32";revision=revision*10+(unsigned)(*p-'0');}
    uwb_config_t candidate=*c;candidate.schedule.revision=revision;
    const char *error=uwb_config_validate(&candidate);if(!error)*c=candidate;return error;
  }
  if(!strcmp(field,"profile")) {
    uwb_config_t candidate=*c;mulSchedule_t schedule;uwb_config_schedule(c,&schedule);
    if(!strcmp(value,"experimental")){mulScheduleExperimental(&schedule);candidate.schedule=schedule;}
    else if(!strcmp(value,"multi")||(!strcmp(value,"legacy")&&schedule.count==2)){
      if(!mulScheduleTag(schedule.slotMs)||schedule.periodMs!=1000)return "restore compatibility spacing/cycle before profile";
      schedule.profile=!strcmp(value,"multi")?MUL_PROFILE_MULTI:MUL_PROFILE_LEGACY;
      schedule.txLeadUs=schedule.rxBudgetUs=schedule.airtimeUs=0;schedule.guard100us=schedule.rangingFlags=0;
      candidate.schedule=schedule.profile==MUL_PROFILE_LEGACY?(mulSchedule_t){0}:schedule;
    }else return "profile: legacy (N2), multi or experimental";
    const char *error=uwb_config_validate(&candidate);if(!error)*c=candidate;return error;
  }
  if(!strcmp(field,"slot_ms")) {
    uint32_t slot;if(!decimal(value,UINT16_MAX,&slot)||!slot)return "slot_ms: 1..65535";
    uwb_config_t candidate=*c;candidate.slot_ms=slot;if(candidate.schedule.profile)candidate.schedule.slotMs=(uint16_t)slot;
    const char *error=uwb_config_validate(&candidate);if(!error)*c=candidate;return error;
  }
  if(!strcmp(field,"reference")||!strcmp(field,"follower")) {
    if(c->schedule.profile)return "use cfg roster set for a multi schedule";
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
  memset(out,0,UWB_CONFIG_RECORD_SIZE);put(out,MAGIC);put(out+4,c->plan_present?UWB_CONFIG_PLAN_VERSION:c->schedule.profile==MUL_PROFILE_EXPERIMENTAL?UWB_CONFIG_TIMING_VERSION:c->clock.periodMs?UWB_CONFIG_CLOCK_VERSION:c->schedule.profile?UWB_CONFIG_MULTI_VERSION:UWB_CONFIG_VERSION);put(out+8,(c->plan_present||c->schedule.profile||c->clock.periodMs)?UWB_CONFIG_MULTI_RECORD_SIZE:UWB_CONFIG_RECORD_SIZE);put(out+12,generation);
  uint32_t v[FIELDS];fields(c,v);for(unsigned i=0;i<FIELDS;i++) put(out+16+4*i,v[i]);
  /* v3 packs roster bytes; v4 adds RF enable in bit24 and protocol role in bits25..26 of the node word.
   * Record size, peer offsets, CRC and commit-last journal geometry stay fixed. */
  put(out+16,c->node|(c->reference<<8)|(c->follower<<16)|(c->rf_enabled<<24)|(c->protocol_role<<25)|((uint32_t)(c->slot_ms==10?1:c->slot_ms==50?2:3)<<27));
  for(unsigned i=0;i<UWB_MAX_PEERS;i++) {out[88+4*i]=c->peers[i].address;out[89+4*i]=c->peers[i].enabled;out[90+4*i]=(uint8_t)c->peers[i].interval_ms;out[91+4*i]=(uint8_t)(c->peers[i].interval_ms>>8);}
  put(out+120,uwb_config_crc(out,120));put(out+124,COMMIT);
}
static bool decode_base(const uint8_t in[UWB_CONFIG_RECORD_SIZE],uwb_config_t *c,uint32_t *generation,const mulSchedule_t *schedule) {
  if(get(in)!=MAGIC||(get(in+4)!=1&&get(in+4)!=2&&get(in+4)!=3&&get(in+4)!=4&&get(in+4)!=UWB_CONFIG_VERSION&&!(schedule&&(get(in+4)==UWB_CONFIG_MULTI_VERSION||get(in+4)==UWB_CONFIG_CLOCK_VERSION||get(in+4)==UWB_CONFIG_TIMING_VERSION||get(in+4)==UWB_CONFIG_PLAN_VERSION)))||get(in+8)!=(schedule?UWB_CONFIG_MULTI_RECORD_SIZE:UWB_CONFIG_RECORD_SIZE)||get(in+124)!=COMMIT||get(in+120)!=uwb_config_crc(in,120)) return false;

  uint32_t v[FIELDS];for(unsigned i=0;i<FIELDS;i++) v[i]=get(in+16+4*i);
  uwb_config_t candidate={0};assign(&candidate,v);
  candidate.slot_ms=500; // Legacy schema had a compile-time500ms deployment.
  candidate.reference=0x84;candidate.follower=0x85;candidate.rf_enabled=candidate.mode!=0;candidate.protocol_role=candidate.mode>=4?UWB_PROTOCOL_RLR:UWB_PROTOCOL_STANDALONE;
  if(get(in+4)>=3) {
    if((get(in+4)==3&&(v[0]&UINT32_C(0xFF000000)))||(v[0]&(get(in+4)>=5?UINT32_C(0xE0000000):UINT32_C(0xF8000000))))return false;
    if(get(in+4)>=4){candidate.rf_enabled=(v[0]>>24)&1;candidate.protocol_role=(v[0]>>25)&3;}
    if(get(in+4)>=5) {
      unsigned slot=(v[0]>>27)&3;if(!slot)return false;
      candidate.slot_ms=slot==1?10:slot==2?50:500;
    }
    candidate.node=v[0]&255;candidate.reference=(v[0]>>8)&255;candidate.follower=(v[0]>>16)&255;
  }
  if(get(in+4)==1) {
    if(candidate.wifi||candidate.telemetry_format||candidate.peer_count)return false;
    for(unsigned i=88;i<120;i++)if(in[i])return false;
    candidate.peer_count=1;candidate.peers[0]=(uwb_peer_t){(uint8_t)candidate.peer,1,(uint16_t)candidate.interval_ms};
  } else for(unsigned i=0;i<UWB_MAX_PEERS;i++) candidate.peers[i]=(uwb_peer_t){in[88+4*i],in[89+4*i],(uint16_t)(in[90+4*i]|(in[91+4*i]<<8))};
  if(schedule&&schedule->profile>=MUL_PROFILE_MULTI)candidate.schedule=*schedule;
  // Schema8 prefix has only the old compact slot tag. Full numerical timing
  // belongs to the independently validated extension, never the packed alias.
  if(get(in+4)==UWB_CONFIG_TIMING_VERSION||get(in+4)==UWB_CONFIG_PLAN_VERSION)candidate.slot_ms=schedule->slotMs;
  if(uwb_config_validate(&candidate)) return false;
  *c=candidate;*generation=get(in+12);return true;
}
bool uwb_config_decode(const uint8_t in[UWB_CONFIG_RECORD_SIZE],uwb_config_t *c,uint32_t *generation) {
 return decode_base(in,c,generation,NULL);
}
static bool decode_multi(const uint8_t in[UWB_CONFIG_MULTI_RECORD_SIZE],uwb_config_t *c,uint32_t *generation) {
 bool planned=get(in+4)==UWB_CONFIG_PLAN_VERSION;
 bool experimental=get(in+4)==UWB_CONFIG_TIMING_VERSION;
 bool clock=get(in+4)==UWB_CONFIG_CLOCK_VERSION||experimental||planned;
 if((get(in+4)!=UWB_CONFIG_MULTI_VERSION&&!clock)||get(in+252)!=COMMIT||get(in+248)!=uwb_config_crc(in,248))return false;
 for(unsigned i=planned?240:experimental?173:clock?172:160;i<248;i++)if(in[i])return false;
 if((experimental||planned)&&in[172]>1)return false;
 if(planned&&(in[173]>1||in[174]||in[175]))return false;
 mulSchedule_t schedule;uwb_config_t candidate;uint32_t gen;
 if(!mulScheduleDecode(in+128,&schedule)||
    (experimental?schedule.profile!=MUL_PROFILE_EXPERIMENTAL:planned?false:schedule.profile>MUL_PROFILE_MULTI)||
    (!clock&&schedule.profile!=MUL_PROFILE_MULTI)||!decode_base(in,&candidate,&gen,&schedule))return false;
 if(clock) {
  mulClockFitConfig_t decoded;if(!mulClockFitConfigDecode(in+160,&decoded))return false;
  if((experimental||planned)&&!in[172]) {
   mulClockFitConfig_t defaults;uwb_config_clock(&candidate,&defaults);
   if(memcmp(&defaults,&decoded,sizeof(defaults)))return false;
  }else candidate.clock=decoded;
  if(uwb_config_validate(&candidate))return false;
 }
 if(planned) {
  if(!mulRangingPlanDecode(in+176,&candidate.plan))return false;
  candidate.plan_present=1;candidate.plan_selected=in[173];
  if(uwb_config_validate(&candidate))return false;
 }
 *c=candidate;*generation=gen;return true;
}
typedef struct {bool valid;uint32_t generation,offset,free[2],large[2];uwb_config_t config;} scan_t;
static bool erased_record(const uint8_t *p,unsigned n){for(unsigned i=0;i<n;i++)if(p[i]!=255)return false;return true;}
static bool scan(const uwb_config_store_t *s,scan_t *result) {
 *result=(scan_t){.free={UINT32_MAX,UINT32_MAX},.large={UINT32_MAX,UINT32_MAX}};
 for(uint32_t at=0;at<2*UWB_CONFIG_SECTOR_SIZE;at+=UWB_CONFIG_RECORD_SIZE) {
  uint8_t record[UWB_CONFIG_MULTI_RECORD_SIZE];
  if(!s->read(s->context,at,record,UWB_CONFIG_RECORD_SIZE))return false;
  unsigned bank=at/UWB_CONFIG_SECTOR_SIZE;bool erased=erased_record(record,UWB_CONFIG_RECORD_SIZE);
  if(erased&&result->free[bank]==UINT32_MAX)result->free[bank]=at;
  if(erased&&at%UWB_CONFIG_MULTI_RECORD_SIZE==0&&result->large[bank]==UINT32_MAX) {
   if(!s->read(s->context,at+UWB_CONFIG_RECORD_SIZE,record+UWB_CONFIG_RECORD_SIZE,UWB_CONFIG_RECORD_SIZE))return false;
   if(erased_record(record+UWB_CONFIG_RECORD_SIZE,UWB_CONFIG_RECORD_SIZE))result->large[bank]=at;
  }
  uwb_config_t c;uint32_t gen;bool valid=false;
  if((get(record+4)==UWB_CONFIG_MULTI_VERSION||get(record+4)==UWB_CONFIG_CLOCK_VERSION||get(record+4)==UWB_CONFIG_TIMING_VERSION||get(record+4)==UWB_CONFIG_PLAN_VERSION)&&at%UWB_CONFIG_MULTI_RECORD_SIZE==0) {
   if(!s->read(s->context,at+UWB_CONFIG_RECORD_SIZE,record+UWB_CONFIG_RECORD_SIZE,UWB_CONFIG_RECORD_SIZE))return false;
   valid=decode_multi(record,&c,&gen);
  } else valid=uwb_config_decode(record,&c,&gen);
  if(valid&&(!result->valid||((uint32_t)(gen-result->generation)<UINT32_C(0x80000000)&&gen!=result->generation))) {
   result->valid=true;result->generation=gen;result->offset=at;result->config=c;
  }
 }
 return true;
}
bool uwb_config_load(const uwb_config_store_t *s,uwb_config_t *c,uint32_t *generation) {
 scan_t found;if(!scan(s,&found)||!found.valid)return false;
 *c=found.config;*generation=found.generation;return true;
}
bool uwb_config_save(const uwb_config_store_t *s,const uwb_config_t *c,uint32_t *generation) {
 scan_t found;if(uwb_config_validate(c)||!scan(s,&found))return false;
 if(found.valid&&uwb_config_equal(c,&found.config)){*generation=found.generation;return true;}
 bool multi=c->plan_present||c->schedule.profile!=0||c->clock.periodMs!=0;
 unsigned bank=found.valid?found.offset/UWB_CONFIG_SECTOR_SIZE:0;
 uint32_t at=multi?found.large[bank]:found.free[bank];
 if(at==UINT32_MAX) {
  if(found.valid)bank^=1u;
  if(!s->erase(s->context,bank))return false;
  at=bank*UWB_CONFIG_SECTOR_SIZE;
 }
 uint32_t gen=found.valid?found.generation+1:1;
 uint8_t record[UWB_CONFIG_MULTI_RECORD_SIZE]={0},verify[UWB_CONFIG_MULTI_RECORD_SIZE];
 unsigned size=multi?UWB_CONFIG_MULTI_RECORD_SIZE:UWB_CONFIG_RECORD_SIZE;
 uwb_config_encode(record,c,gen);
 if(multi) {
  mulSchedule_t schedule;uwb_config_schedule(c,&schedule);
  if(!mulScheduleEncode(record+128,&schedule))return false;
  if(c->plan_present||c->clock.periodMs||schedule.profile==MUL_PROFILE_EXPERIMENTAL) {
   mulClockFitConfig_t clock;uwb_config_clock(c,&clock);
   if(!mulClockFitConfigEncode(record+160,&clock))return false;
  }
  if(c->plan_present||schedule.profile==MUL_PROFILE_EXPERIMENTAL)record[172]=c->clock.periodMs?1:0;
  if(c->plan_present) {
   record[173]=c->plan_selected;if(!mulRangingPlanEncode(record+176,&c->plan))return false;
  }
  put(record+248,uwb_config_crc(record,248));put(record+252,COMMIT);
 }
 for(unsigned i=0;i<size;i+=4)if(!s->program(s->context,at+i,get(record+i)))return false;
 if(!s->read(s->context,at,verify,size)||memcmp(verify,record,size))return false;
 *generation=gen;return true;
}
bool uwb_config_range(const uwb_config_t *c,double raw,bool valid,double *calibrated) {
  *calibrated=raw+(c->calibration ? c->correction_mm/1000.0 : 0.0);
  return valid&&isfinite(raw)&&raw>=0&&isfinite(*calibrated)&&*calibrated>=0;
}
