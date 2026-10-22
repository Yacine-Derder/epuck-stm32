/* SPDX-License-Identifier: MIT */
#include "uwb_runtime.h"
#if MUL_UNIVERSAL
#include "app_config.h"
#if !MUL_UWB_ONLY
#error "Universal runtime image requires UWB-only safety policy"
#endif
#include "main.h"
#include "mul_ads_radio.h"
#include "mul_hybrid_radio.h"
#include "deca_device_api.h"
#include "epuck_link.h"
#include "usb_console.h"
#include "esp_link.h"
#include <stdio.h>
#include <string.h>
static uwb_config_t current,saved,active;
static uint32_t generation;
static bool savedValid,started,initialized,radioTouched,applied,loadedAtBoot,durabilityKnown;
#define TLM_CAPACITY 4u
#define TLM_LENGTH MUL_TLM_LINE
static char telemetry[TLM_CAPACITY][TLM_LENGTH];
static unsigned head,tail,count,highWater;
static uint32_t dropped,accepted,telemetrySequence,lastIdleReport;
static uint64_t telemetryEpoch;
static char wifiQueue[TLM_CAPACITY][TLM_LENGTH];
static uint32_t wifiQueuedAt[TLM_CAPACITY],wifiDrops;
static unsigned wifiHead,wifiTail,wifiCount,wifiHigh;
static unsigned display,displayIndex;
static uwb_config_t displayConfig;
static const char *displayState;
static bool displayBoot;
static uint32_t displayGeneration;
static bool displayStats;
static const char *const help_commands[]={
  "=== E-PUCK STM32 COMMANDS ===",
  "",
  "General",
  "  - help [topic]              Show commands or topic details",
  "",
  "UWB configuration",
  "  - cfg show                  Show current configuration",
  "  - cfg set <field> <value>    Change and save a setting",
  "  - cfg help                  List configuration options",
  "",
  "UWB diagnostics",
  "  - hybrid status|diag|frames  Passive reception diagnostics",
  "  - sync status               Listen-mode synchronization state",
  "  - diag reset                Reset idle diagnostic counters",
  "",
  "Peers",
  "  - peer list|help             Show peers or management commands",
  "",
  "Telemetry",
  "  - tlm status|next            Show queues or retrieve one sample",
  "",
  "Robot safety",
  "  - help safety               Stop command and robot restrictions",
  "",
  "More information",
  "  - help config|hybrid|peers|telemetry|safety|all",
};
static const char *const help_config[]={
  "=== UWB CONFIGURATION ===",
  "  - cfg show [active|current|saved|defaults]  Read settings",
  "  - cfg set <field> <value>    Validate, activate and save one change",
  "  - cfg load                  Activate last valid saved settings",
  "  - cfg revert                Legacy alias for cfg load",
  "  - cfg save / cfg apply      Legacy aliases; set already saves",
  "  - cfg defaults              Replace settings with factory values",
  "",
  "Fields and valid values (names and enum values are case-insensitive)",
  "  - address (alias node), peer: decimal or 0x00..0xff; distinct IDs",
  "  - identity: standalone | alr | rlr (metadata only)",
  "  - mode: off | twr | beacon | listen | hybrid | hybrid_static",
  "  - role: responder | initiator (ADS setting, not an ALR TDMA role)",
  "  - interval_ms: 250..60000; primary-peer transaction interval",
  "  - phy: mixed only; channel5, 6.8Mbps, code9, preamble128",
  "  - tx_ant_dtu, rx_ant_dtu: 0..65535; hardware antenna delays",
  "  - calibration: off | on; correction_mm: -10000..10000",
  "  - verbosity: quiet | summary | full; full includes raw ADS timestamps",
  "  - telemetry, wifi, samples: off | on; see help telemetry",
  "  - telemetry_ms: 1000..60000; telemetry_format: compact only",
  "",
  "Persistence and calibration",
  "  - Settings are persistent; each changed field restarts UWB.",
  "  - Unchanged values do not write flash; reboot and cfg show to verify.",
  "  - Antenna/primary-peer changes disable pair calibration; refit first.",
  "  - Calibrated range = raw + correction_mm/1000; raw stays available.",
  "  - Hybrid needs responder, distinct RLR ID and both ALR peers enabled.",
  "  - Factory: mode off; address 0x00, peer 0x01; provision unique IDs.",
  "  - cfg show defaults lists factory settings without changing anything.",
  "",
  "Examples (inspect current settings first; keep calibrated primary peer)",
  "  - cfg set address 0x8d",
  "  - cfg set mode hybrid",
  "  - cfg set verbosity quiet",
};
static const char *const help_hybrid[]={
  "=== PASSIVE HYBRID UWB ===",
  "  - cfg set mode hybrid       Passive RLR; receiver rate stays unknown",
  "  - cfg set mode hybrid_static  Stationary-only rate experiment",
  "  - hybrid status             Counts, freshness and last observation",
  "  - hybrid diag               Frame/rejection/model counters",
  "  - hybrid frames             Last 34/74/84-byte RX snapshots",
  "  - diag reset                Reset counters only when no TWR audit active",
  "  - sync status               Separate listen-mode arrival models",
  "",
  "Interpretation",
  "  - status 0=no_relation, 1=no_rate, 2=stale_rate, 3=inconsistent,",
  "    4=available; last_status is historical, not current freshness.",
  "  - capture_age: invalid or >15ms RX-to-service age; 65535=invalid.",
  "  - available: decoded follower publication, not local TDoA validity.",
  "  - missing_twr: no expected prior stage; loss/reorder/duplicates possible.",
  "  - delay_ms: age of inter-ALR propagation measurement at follower TX.",
  "  - Frame slots are independent captures, not one complete exchange.",
  "  - RLR never transmits; inter-ALR TWR is not an RLR range.",
  "  - Unknown receiver rate forbids metric TDoA in ordinary hybrid.",
  "  - Two ALRs alone cannot give a unique unconstrained 2D position.",
};
static const char *const help_peers[]={
  "=== UWB PEERS ===",
  "  - peer list                 List saved peers, enable state and intervals",
  "  - peer stats                Per-peer statistics in active twr mode",
  "  - peer add <id>             Add a peer using current interval",
  "  - peer remove <id>          Remove a peer; keep at least one",
  "  - peer enable <id>          Include a configured peer",
  "  - peer disable <id>         Retain settings but exclude that peer",
  "  - peer interval <id> <ms>   Set interval, 250..60000ms",
  "  - peer help                 Show this topic",
  "",
  "Constraints",
  "  - IDs: decimal or 0x00..0xff; no self/duplicate; maximum eight peers.",
  "  - Changes validate, activate and save automatically.",
  "  - First peer is primary; replacing/removing it disables calibration.",
  "  - For hybrid, add/enable 0x84 and 0x85; keep calibrated primary.",
  "  - Extra peers do not extend the current two-ALR TDMA protocol.",
};
static const char *const help_telemetry[]={
  "=== UWB TELEMETRY ===",
  "  - cfg set telemetry on|off  Enable/disable bounded telemetry queues",
  "  - cfg set wifi on|off       Enable/disable optional UART3 forwarding",
  "  - cfg set telemetry_ms <ms> Set summary interval, 1000..60000ms",
  "  - cfg set samples on|off    Include detailed ADS/beacon samples",
  "  - cfg set telemetry_format compact  Existing wire-format setting",
  "  - cfg set verbosity quiet|summary|full  Optional USB diagnostics",
  "  - tlm status                Separate USB/Wi-Fi depths, drops and faults",
  "  - tlm next                  Retrieve one queued USB record, or empty",
  "",
  "Output",
  "  - UWB1/UWB2 compatibility retained; hybrid records use UWB3.",
  "  - Hybrid raw records need telemetry on; samples does not gate them.",
  "  - Each queue holds four records; slow/unconsumed readers cause drops.",
  "  - Cumulative queue drops alone are not RF/Wi-Fi packet-loss rates.",
  "  - Wi-Fi does not authorize motors; help does not stop UART forwarding.",
};
static const char *const help_safety[]={
  "=== ROBOT SAFETY (UWB-ONLY IMAGE) ===",
  "Robot commands below must be entered in UPPERCASE (legacy parser).",
  "  - stop                      Stop/disarm; wait for acknowledgement",
  "  - motor_status              Inspect motor/safety state",
  "  - stream_status             Inspect robot sensor-stream state",
  "  - link_status               Inspect e-puck communication state",
  "",
  "Restrictions",
  "  - Arming, movement and sensor-stream changes are prohibited.",
  "  - Read-only UWB help/configuration/diagnostics remain permitted.",
  "  - Settings changes require startup grace and confirmed safe state.",
  "  - Wait 2s; if rejected, stop and verify motor/stream/link status.",
  "  - Heartbeat, watchdog, UART faults and safe shutdown remain active.",
  "  - No UWB mode, identity or help command grants motor authorization.",
  "  - Optional UWB text pauses during help; errors and safety still report.",
};
typedef struct {const char *name;const char *const *lines;unsigned count;} help_topic_t;
#define HELP_TOPIC(name) {#name,help_##name,sizeof(help_##name)/sizeof(*help_##name)}
static const help_topic_t helpTopics[]={HELP_TOPIC(commands),HELP_TOPIC(config),
 HELP_TOPIC(hybrid),HELP_TOPIC(peers),HELP_TOPIC(telemetry),HELP_TOPIC(safety)};
#undef HELP_TOPIC
static unsigned helpPage;
static bool helpAll;
bool uwb_runtime_help_active(void) {return display==1;}
static void help_start(const char *topic) {
 if(!topic)topic="commands";
 if(!strcmp(topic,"cfg"))topic="config";
 if(!strcmp(topic,"peer"))topic="peers";
 if(!strcmp(topic,"tlm"))topic="telemetry";
 helpAll=!strcmp(topic,"all");
 for(unsigned i=0;i<sizeof(helpTopics)/sizeof(*helpTopics);i++)
   if(helpAll||!strcmp(topic,helpTopics[i].name)){helpPage=i;display=1;displayIndex=0;return;}
 display=0;
 usb_console_write("ERR help topic; use config, hybrid, peers, telemetry, safety or all");
}
const uwb_config_t *uwb_runtime_active(void) {return &active;}
void uwb_runtime_publish(mulTelemetry_t *r) {
  if(!active.telemetry) return;
  r->sequence=telemetrySequence++;r->epoch=telemetryEpoch;
  r->node=(uint8_t)active.node;r->radio=2;r->mode=(uint8_t)active.mode;r->role=(uint8_t)active.role;
  if(r->type!=mulTlmPeer)r->uptimeMs=HAL_GetTick();
  r->depth=(uint16_t)(count+wifiCount);r->highWater=(uint16_t)(highWater+wifiHigh);
  r->dropped=dropped+wifiDrops;r->uartErrors=esp_link_uart_errors();r->txDropped=esp_link_telemetry_dropped();
  char line[MUL_TLM_LINE];if(!mulTelemetryEncode(line,sizeof(line),r)){dropped++;return;}
  if(count==TLM_CAPACITY)dropped++;
  else {memcpy(telemetry[head],line,sizeof(line));head=(head+1)%TLM_CAPACITY;count++;if(count>highWater)highWater=count;}
  if(active.wifi) {
    if(wifiCount==TLM_CAPACITY)wifiDrops++;
    else {memcpy(wifiQueue[wifiHead],line,sizeof(line));wifiQueuedAt[wifiHead]=HAL_GetTick();wifiHead=(wifiHead+1)%TLM_CAPACITY;wifiCount++;if(wifiCount>wifiHigh)wifiHigh=wifiCount;}
  }
  accepted++;
}
void uwb_runtime_telemetry(const char *line) {
  mulTelemetry_t r;if(mulTelemetryDecode(line,&r))uwb_runtime_publish(&r);else dropped++;
}
static void telemetry_task(void) {
  if(wifiCount) {
    if((uint32_t)(HAL_GetTick()-wifiQueuedAt[wifiTail])>=1000U) {wifiDrops++;wifiTail=(wifiTail+1)%TLM_CAPACITY;wifiCount--;}
    else if(esp_link_telemetry(wifiQueue[wifiTail])) {wifiTail=(wifiTail+1)%TLM_CAPACITY;wifiCount--;}
  }
  if(active.telemetry&&!active.mode&&(uint32_t)(HAL_GetTick()-lastIdleReport)>=active.telemetry_ms) {
    lastIdleReport=HAL_GetTick();mulTelemetry_t r={.type=mulTlmSummary};uwb_runtime_publish(&r);
  }
}
static void reply(const char *s) {usb_console_write(s);}
static void stop(void) {
  if(radioTouched) mul_ads_radio_stop();
  started=false;
}
/* Preflight initialization can fail BEFORE committing any new settings. No main
 * loop radio service/TX occurs here. Stop RX again before all flash operations. */
static bool prepare(const uwb_config_t *c) {
  stop();
  if(!c->mode) return true;
  uint64_t session;if(!uwb_runtime_session(&session)) return false;
  telemetryEpoch=session;mul_ads_radio_configure(c,session);radioTouched=true;
  int32_t status=mul_ads_radio_init();mul_ads_radio_stop();
  return status==DWT_SUCCESS;
}
static bool resume(const uwb_config_t *c) {
  if(c->mode && mul_ads_radio_resume()!=DWT_SUCCESS) {stop();return false;}
  active=current=*c;started=c->mode!=0;applied=true;return true;
}
static void restore(const uwb_config_t *old) {
  if(!prepare(old)||!resume(old)) {
    stop();applied=false;reply("ERR previous radio could not resume; saved settings retained; check hardware then CFG LOAD");
  }
}
static bool update(const uwb_config_t *candidate) {
  const char *error=uwb_config_validate(candidate);
  if(error) {reply("ERR configuration rejected");reply(error);return false;}
  if(applied&&durabilityKnown&&savedValid&&uwb_config_equal(candidate,&active)&&uwb_config_equal(candidate,&saved)) {
    reply("CFG SAVED / ACTIVE (unchanged; no flash write)");return true;
  }
  if(usb_console_write_slots()<8) {
    reply("ERR console busy; let output drain and retry; configuration unchanged");return false;
  }
  error=epuck_link_flash_error();
  if(error) {reply(error);return false;} /* No active/saved mutation or radio stop. */
  display=0; /* Cancel a paged snapshot before replacing its live radio state. */
  uwb_config_t old=active;
  if(!prepare(candidate)) {reply("ERR radio preflight failed; settings not changed");restore(&old);return false;}
  uint32_t newGeneration;
  if(!uwb_config_save(uwb_runtime_store(),candidate,&newGeneration)) {
    /* A backend may report failure after a complete commit (e.g. verify read
     * failed). Resolve from CRC-validated flash, never from a RAM assumption. */
    uwb_config_t found;uint32_t foundGeneration;
    if(uwb_config_load(uwb_runtime_store(),&found,&foundGeneration)) {
      saved=found;generation=foundGeneration;savedValid=durabilityKnown=true;
      if(uwb_config_equal(&found,candidate)) {newGeneration=foundGeneration;goto committed;}
    } else durabilityKnown=false;
    reply("ERR flash update failed; new setting not accepted; restoring previous radio");
    if(!durabilityKnown) reply("ERR storage durability unconfirmed; inspect CFG SHOW/LOAD; no records erased for recovery");
    restore(&old);return false;
  }
committed:
  saved=*candidate;generation=newGeneration;savedValid=durabilityKnown=true;
  /* A post-commit hardware fault is reported honestly: durable but radio off.
   * Retrying LOAD uses the same saved settings without another flash write. */
  current=*candidate;
  if(!resume(candidate)) {applied=false;reply("ERR configuration SAVED but radio OFF; check hardware, retry CFG LOAD");return false;}
  dropped+=count;head=tail=count=0;wifiDrops+=wifiCount;wifiHead=wifiTail=wifiCount=0;
  esp_link_set_enabled((uint8_t)active.wifi);
  reply("CFG SAVED / ACTIVE");return true;
}
void uwb_runtime_init(void) {
  stop();uwb_config_defaults(&current);active=current;generation=0;applied=false;
  loadedAtBoot=savedValid=uwb_config_load(uwb_runtime_store(),&saved,&generation);
  durabilityKnown=true;
  if(savedValid) current=saved;
  initialized=true;
  if(!uwb_runtime_session(&telemetryEpoch))telemetryEpoch=0;
  if(!prepare(&current)||!resume(&current)) {stop();applied=false;reply("ERR startup radio failed; inspect CFG SHOW, retry CFG LOAD");}
  esp_link_set_enabled((uint8_t)(applied&&current.wifi));
  reply(applied?"STM32/DWM3000 UWB ready; Type HELP or CFG HELP for commands":
    "STM32/DWM3000 console ready; radio OFF (startup error); Type CFG HELP");
}
static const char *identity(uint32_t n) {return n==1?"ALR":n==2?"RLR":"Standalone";}
static const char *onoff(uint32_t n) {return n?"ON":"OFF";}
static const char *configVerbosity(uint32_t n) {return n==0?"Quiet":n==1?"Summary":"Full";}
static void show(const uwb_config_t *c,const char *label) {
  displayConfig=*c;displayState=label;displayBoot=loadedAtBoot;
  displayGeneration=generation;display=2;displayIndex=0;
}
static void display_output(void) {
  if(!display) return;
  if(display==1) {
    const help_topic_t *topic=&helpTopics[helpPage];
    if(displayIndex>=topic->count) {
      if(helpAll&&helpPage+1<sizeof(helpTopics)/sizeof(*helpTopics)){helpPage++;displayIndex=0;}
      else display=0;
      return;
    }
    /* One bounded line per service call. Keep four queue slots available for
     * errors/safety replies; never wait for CDC or change radio configuration. */
    if(usb_console_write_slots()>4&&usb_console_try_write(topic->lines[displayIndex]))displayIndex++;
    return;
  }
  const uwb_config_t *c=&displayConfig;char text[320];
  if(display==4) {
    if(!mul_ads_radio_sync_status(displayIndex,text,sizeof(text))){display=0;return;}
    if(usb_console_try_write(text))displayIndex++;
    return;
  }
  if(display==3) {
    if(displayIndex>=c->peer_count){display=0;return;}
    const uwb_peer_t *p=&c->peers[displayIndex];mulTelemetry_t r={0};
    if(displayStats)mul_ads_radio_peer_stats(displayIndex,&r);
    if(!displayStats) {
      snprintf(text,sizeof(text),"PEER 0x%02X %s interval=%u primary=%u",p->address,p->enabled?"ON":"OFF",p->interval_ms,displayIndex==0);
      if(usb_console_try_write(text))displayIndex++;
      return;
    }
    snprintf(text,sizeof(text),"PEER 0x%02X %s interval=%u attempts=%lu done=%lu abort=%lu timeout=%lu invalid=%lu reject=%lu raw_mm=%ld cal_mm=%ld flags=%u last_ok_ms=%lu",
      p->address,p->enabled?"ON":"OFF",p->interval_ms,(unsigned long)r.attempts,(unsigned long)r.completed,(unsigned long)r.aborted,(unsigned long)r.timeouts,(unsigned long)r.invalid,(unsigned long)r.rejected,(long)r.rawMm,(long)r.calMm,r.flags,(unsigned long)r.uptimeMs);
    if(usb_console_try_write(text))displayIndex++;
    return;
  }
  switch(displayIndex) {
  case 0:snprintf(text,sizeof(text),"Node address:       0x%02lX",(unsigned long)c->node);break;
  case 1:snprintf(text,sizeof(text),"Peer address:       0x%02lX",(unsigned long)c->peer);break;
  case 2:snprintf(text,sizeof(text),"Robot identity:     %s",identity(c->identity));break;
  case 3:snprintf(text,sizeof(text),"UWB mode:           %s",c->mode==5?"HYBRID_STATIC":c->mode==4?"HYBRID":c->mode==1?"TWR":c->mode==2?"BEACON":c->mode==3?"LISTEN":"OFF");break;
  case 4:snprintf(text,sizeof(text),"TWR role:           %s",c->role==1?"Initiator":"Responder");break;
  case 5:snprintf(text,sizeof(text),"Interval:           %lu ms",(unsigned long)c->interval_ms);break;
  case 6:snprintf(text,sizeof(text),"PHY:                Mixed channel5 / 6.8Mbps / code9 / preamble128");break;
  case 7:snprintf(text,sizeof(text),"TX antenna delay:   %lu DTU",(unsigned long)c->tx_ant_dtu);break;
  case 8:snprintf(text,sizeof(text),"RX antenna delay:   %lu DTU",(unsigned long)c->rx_ant_dtu);break;
  case 9:snprintf(text,sizeof(text),"Calibration:        %s",onoff(c->calibration));break;
  case 10:snprintf(text,sizeof(text),"Correction:         %ld mm",(long)c->correction_mm);break;
  case 11:snprintf(text,sizeof(text),"Diagnostics:        %s",configVerbosity(c->verbosity));break;
  case 12:snprintf(text,sizeof(text),"Telemetry:          %s",onoff(c->telemetry));break;
  case 13:snprintf(text,sizeof(text),"Telemetry interval: %lu ms",(unsigned long)c->telemetry_ms);break;
  case 14:snprintf(text,sizeof(text),"Ranging samples:    %s",onoff(c->samples));break;
  case 15:snprintf(text,sizeof(text),"Schema version:     %u; saved settings loaded at boot: %s",UWB_CONFIG_VERSION,displayBoot?"YES":"NO");break;
  case 16:snprintf(text,sizeof(text),"Configuration:      %s",displayState);break;
  case 18:snprintf(text,sizeof(text),"Wi-Fi UART telemetry: %s; format: compact v2",onoff(c->wifi));break;
  case 19:snprintf(text,sizeof(text),"Configured peers: %lu (maximum 8)",(unsigned long)c->peer_count);break;
  case 17:snprintf(text,sizeof(text),"Saved generation:   %lu; UWB-only safety policy",(unsigned long)displayGeneration);break;
  default:display=0;return;
  }
  if(usb_console_try_write(text)) displayIndex++;
}
void uwb_runtime_task(void) {
  display_output();telemetry_task();
  if(initialized&&started) mul_ads_radio_process();
}
static bool number(const char *s,unsigned maximum,unsigned *out) {
  if(!s||!*s)return false;
  unsigned base=10,v=0,n=0;
  if(s[0]=='0'&&s[1]=='x'){s+=2;base=16;}
  while(*s){unsigned d=*s>='0'&&*s<='9'?(unsigned)(*s-'0'):*s>='a'&&*s<='f'?(unsigned)(*s-'a'+10):99;
    if(d>=base||d>maximum||v>(maximum-d)/base)return false;
    v=v*base+d;s++;n++;}
  if(!n)return false;
  *out=v;return true;
}
static void peer_command(unsigned nt,char **t) {
  if(nt==1||(nt==2&&!strcmp(t[1],"help"))) {
    help_start("peers");return;
  }
  if(nt==2&&(!strcmp(t[1],"list")||!strcmp(t[1],"stats"))) {
    displayStats=!strcmp(t[1],"stats");
    if(displayStats&&(!started||active.mode!=1)){reply("ERR peer statistics require active TWR; PEER LIST remains available");return;}
    displayConfig=active;display=3;displayIndex=0;return;
  }
  unsigned id;if((nt!=3&&nt!=4)||!number(t[2],255,&id)){reply("ERR PEER HELP; address 0x00..0xFF");return;}
  uwb_config_t c=current;unsigned i=0;while(i<c.peer_count&&c.peers[i].address!=id)i++;
  if(nt==3&&!strcmp(t[1],"add")) {
    if(i<c.peer_count||id==c.node||c.peer_count==UWB_MAX_PEERS){reply("ERR peer duplicate/self/table full");return;}
    c.peers[c.peer_count++]=(uwb_peer_t){(uint8_t)id,1,(uint16_t)c.interval_ms};
  } else if(i==c.peer_count){reply("ERR peer not configured");return;}
  else if(nt==3&&!strcmp(t[1],"remove")) {
    if(c.peer_count==1){reply("ERR keep one peer; DISABLE instead");return;}
    if(i==0)c.calibration=0; /* Pair correction never transfers to a new primary. */
    for(unsigned j=i+1;j<c.peer_count;j++)c.peers[j-1]=c.peers[j];
    c.peers[--c.peer_count]=(uwb_peer_t){0};
  } else if(nt==3&&!strcmp(t[1],"enable"))c.peers[i].enabled=1;
  else if(nt==3&&!strcmp(t[1],"disable"))c.peers[i].enabled=0;
  else if(nt==4&&!strcmp(t[1],"interval")) {
    unsigned ms;if(!number(t[3],60000,&ms)||ms<250){reply("ERR interval 250..60000");return;}c.peers[i].interval_ms=(uint16_t)ms;
  } else {reply("ERR PEER HELP");return;}
  c.peer=c.peers[0].address;c.interval_ms=c.peers[0].interval_ms;update(&c);
}
bool uwb_runtime_command(const char *line) {
  char copy[64];size_t n=0;while(n<sizeof(copy)&&line[n]) n++;
  if(n>=sizeof(copy)) {reply("ERR oversized command");return true;}
  for(size_t i=0;i<n;i++) {
    if(line[i]<32||line[i]>126) {reply("ERR malformed command");return true;}
    copy[i]=line[i]>='A'&&line[i]<='Z'?line[i]+('a'-'A'):line[i];
  }
  copy[n]=0;char *token[5]={0};unsigned nt=0;char *p=copy;
  while(*p) {
    if(*p==' ') {p++;continue;}
    if(nt==5) {reply("ERR too many command arguments");return true;}
    token[nt++]=p;while(*p&&*p!=' ')p++;
    if(*p)*p++=0;
  }
  if(!nt) return false;
  if(nt==2&&!strcmp(token[0],"hybrid")&&(!strcmp(token[1],"status")||!strcmp(token[1],"diag")||!strcmp(token[1],"frames"))) {
    if(!started||active.mode<4)reply("HYBRID inactive; mode hybrid/hybrid_static requires responder and enabled legacy ALR peers");
    else {
      char text[384];
      if(!strcmp(token[1],"status")){mul_hybrid_radio_status(text,sizeof(text));reply(text);}
      else for(unsigned i=0;i<3;i++) {
        if(!strcmp(token[1],"diag"))mul_hybrid_radio_diagnostics(i,text,sizeof(text));
        else mul_hybrid_radio_frame(i,text,sizeof(text));
        reply(text);
      }
    }return true;
  }
  if(nt==2&&!strcmp(token[0],"sync")&&!strcmp(token[1],"status")) {
    if(!started||active.mode!=3)reply("SYNC UNKNOWN; select mode listen for apparent arrival models");
    else {display=4;displayIndex=0;}return true;
  }
  if(!strcmp(token[0],"peer")){peer_command(nt,token);return true;}
  if(nt==2&&!strcmp(token[0],"diag")&&!strcmp(token[1],"reset")) {
    if(active.mode&&!mul_ads_radio_reset_stats())reply("ERR transaction active; retry DIAG RESET when idle");
    else {dropped=wifiDrops=accepted=0;highWater=count;wifiHigh=wifiCount;esp_link_reset_diagnostics();reply("DIAG counters reset; safety/session/transaction IDs unchanged");}
    return true;
  }
  if(!strcmp(token[0],"help")) {
    if(nt<=2)help_start(nt==2?token[1]:NULL);
    else reply("ERR help [topic]; use config, hybrid, peers, telemetry, safety or all");
    return true; /* Unknown topics must not fall through to robot safety dispatch. */
  }
  if(strcmp(token[0],"cfg")&&strcmp(token[0],"tlm")) return false;
  if(!strcmp(token[0],"tlm")) {
    if(nt==2&&!strcmp(token[1],"next")) {
      if(!count) reply("TLM EMPTY");
      else if(usb_console_try_write(telemetry[tail])) {tail=(tail+1)%TLM_CAPACITY;count--;}
    } else if(nt==2&&!strcmp(token[1],"status")) {
      char text[192];snprintf(text,sizeof(text),"TLM STATUS usb=%u wifi=%u capacity_each=4 high=%u/%u drops=%lu/%lu accepted=%lu uart_errors=%lu",
        count,wifiCount,highWater,wifiHigh,(unsigned long)dropped,(unsigned long)wifiDrops,(unsigned long)accepted,(unsigned long)esp_link_uart_errors());reply(text);
    } else reply("ERR TLM NEXT | STATUS");
    return true;
  }
  if(nt==1||(nt==2&&!strcmp(token[1],"help"))) {help_start("config");}
  else if((nt==2||nt==3)&&!strcmp(token[1],"show")) {
    if(nt==3&&!strcmp(token[2],"defaults")) {uwb_config_t factory;uwb_config_defaults(&factory);show(&factory,"FACTORY DEFAULTS (not changed)");}
    else if(nt==3&&!strcmp(token[2],"saved")) {
      if(savedValid) show(&saved,durabilityKnown?"SAVED (last verified record)":"LAST VERIFIED SNAPSHOT; storage now UNCONFIRMED");
      else reply("CFG no valid saved configuration; factory defaults are separate");
    } else if(nt==2||!strcmp(token[2],"current")||!strcmp(token[2],"active"))
      show(nt==3&&!strcmp(token[2],"active")?&active:&current,!durabilityKnown?"DURABILITY UNCONFIRMED":!applied?"RADIO OFF / activation failed":savedValid&&uwb_config_equal(&active,&saved)?"SAVED / ACTIVE":"FACTORY DEFAULTS / ACTIVE (not saved)");
    else reply("ERR SHOW accepts ACTIVE, SAVED, DEFAULTS");
  } else if(nt==4&&!strcmp(token[1],"set")) {
    uwb_config_t candidate=current;uint32_t oldTx=current.tx_ant_dtu,oldRx=current.rx_ant_dtu;const char *error=uwb_config_set(&candidate,token[2],token[3]);
    if(error) {reply("ERR configuration rejected");reply(error);}
    else if(update(&candidate) && (candidate.tx_ant_dtu!=oldTx||candidate.rx_ant_dtu!=oldRx))
      reply("CFG antenna delay changed; calibration disabled; refit before enabling");
  } else if(nt==2&&!strcmp(token[1],"defaults")) {uwb_config_t factory;uwb_config_defaults(&factory);update(&factory);}
  else if(nt==2&&(!strcmp(token[1],"load")||!strcmp(token[1],"revert"))) {
    uwb_config_t loaded;uint32_t gen;
    if(uwb_config_load(uwb_runtime_store(),&loaded,&gen)) update(&loaded);
    else {durabilityKnown=false;reply("ERR no readable valid saved record; active configuration unchanged; flash not modified");}
  } else if(nt==2&&(!strcmp(token[1],"apply")||!strcmp(token[1],"save"))) update(&current);
  else reply("ERR malformed CFG command; use CFG HELP");
  return true;
}
#endif
