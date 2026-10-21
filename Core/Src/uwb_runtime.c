/* SPDX-License-Identifier: MIT */
#include "uwb_runtime.h"
#if MUL_UNIVERSAL
#include "app_config.h"
#if !MUL_UWB_ONLY
#error "Universal runtime image requires UWB-only safety policy"
#endif
#include "main.h"
#include "mul_ads_radio.h"
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
static const char *const help[]={
  "CFG HELP / HELP: settings activate and save automatically; no APPLY/SAVE step",
  "CFG SET <field> <value>; all fields persistent; every changed field restarts UWB",
  "address (alias node): 0x00..0xFF or 0..255; factory 0x00; provision unique IDs",
  "peer: 0x00..0xFF or 0..255; factory 0x01; must differ from address",
  "identity: standalone (factory), alr, rlr; metadata, never motor authorization",
  "mode: off (factory), twr; beacon/listen not enabled in this phase",
  "role: responder (factory), initiator; independent of robot identity",
  "interval_ms: 250..60000 ms; factory 1000; initiator transaction interval",
  "phy: mixed (factory/only): channel5, 6.8Mbps, code9, preamble128",
  "tx_ant_dtu / rx_ant_dtu: 0..65535 DW DTU; factory 16380 each; hardware once",
  "calibration: off (factory), on; raw stays available; negatives remain invalid",
  "correction_mm: -10000..10000 mm; factory 0; calibrated = raw + correction/1000",
  "Antenna delay changes disable calibration; refit before explicitly enabling",
  "verbosity: quiet, summary, full (factory); full retains six raw timestamps",
  "telemetry: off (factory), on; bounded USB + Wi-Fi queues; CFG SET wifi on enables UART3",
  "telemetry_ms: 1000..60000 ms; factory 1000; samples: off (factory), on",
  "wifi: off (factory), on; telemetry_format: compact (only); no motor authorization",
  "CFG SHOW [ACTIVE|SAVED|DEFAULTS]; includes schema and saved-settings boot status",
  "CFG DEFAULTS: activate and persist factory settings (UWB off, calibration off)",
  "CFG LOAD (REVERT alias): activate last valid saved settings; read failure preserves state",
  "CFG APPLY / CFG SAVE: compatibility aliases; normal SET already activates/saves",
  "Safety: wait startup grace 2s; attached robot needs STOP and STREAM 0 confirmed",
  "If rejected: STOP; wait OK STOP; inspect MOTOR_STATUS, STREAM_STATUS, LINK_STATUS",
  "ALR example: CFG SET address 0x10; CFG SET peer 0x11; CFG SET identity alr",
  "Then CFG SET role initiator; CFG SET calibration off; CFG SET mode twr",
  "RLR example: CFG SET address 0x11; CFG SET peer 0x10; CFG SET identity rlr",
  "Set a different peer first if the desired address equals the current peer",
  "Then CFG SET role responder; CFG SET calibration off; CFG SET mode twr",
  "Calibration example: CFG SET correction_mm -100; CFG SET calibration on (review first)",
  "Telemetry: CFG SET telemetry on; TLM STATUS; TLM NEXT; CFG SET telemetry off",
  "Unchanged settings do not write flash. Power-cycle, CFG SHOW to verify durability",
  "STOP / MOTOR_STATUS / STREAM_STATUS / LINK_STATUS remain available; ARM prohibited"
};
const uwb_config_t *uwb_runtime_active(void) {return &active;}
void uwb_runtime_publish(mulTelemetry_t *r) {
  if(!active.telemetry) return;
  r->sequence=telemetrySequence++;r->epoch=telemetryEpoch;
  r->node=(uint8_t)active.node;r->radio=2;r->mode=(uint8_t)active.mode;r->role=(uint8_t)active.role;
  r->uptimeMs=HAL_GetTick();r->depth=(uint16_t)(count+wifiCount);r->highWater=(uint16_t)(highWater+wifiHigh);
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
static void show(const uwb_config_t *c,const char *state) {
  displayConfig=*c;displayState=state;displayBoot=loadedAtBoot;
  displayGeneration=generation;display=2;displayIndex=0;
}
static void display_output(void) {
  if(!display) return;
  if(display==1) {
    if(displayIndex>=sizeof(help)/sizeof(*help)) {display=0;return;}
    if(usb_console_try_write(help[displayIndex])) displayIndex++;
    return;
  }
  const uwb_config_t *c=&displayConfig;char text[192];
  switch(displayIndex) {
  case 0:snprintf(text,sizeof(text),"Node address:       0x%02lX",(unsigned long)c->node);break;
  case 1:snprintf(text,sizeof(text),"Peer address:       0x%02lX",(unsigned long)c->peer);break;
  case 2:snprintf(text,sizeof(text),"Robot identity:     %s",identity(c->identity));break;
  case 3:snprintf(text,sizeof(text),"UWB mode:           %s",c->mode==1?"TWR":c->mode==2?"BEACON":c->mode==3?"LISTEN":"OFF");break;
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
  if(nt==2&&!strcmp(token[0],"diag")&&!strcmp(token[1],"reset")) {
    if(active.mode==1&&!mul_ads_radio_reset_stats())reply("ERR transaction active; retry DIAG RESET when idle");
    else {dropped=wifiDrops=accepted=0;highWater=count;wifiHigh=wifiCount;esp_link_reset_diagnostics();reply("DIAG counters reset; safety/session/transaction IDs unchanged");}
    return true;
  }
  if(nt==1&&!strcmp(token[0],"help")) {display=1;displayIndex=0;return true;}
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
  if(nt==1||(nt==2&&!strcmp(token[1],"help"))) {display=1;displayIndex=0;}
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
