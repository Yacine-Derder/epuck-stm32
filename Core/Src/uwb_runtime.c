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
#include <stdio.h>
#include <string.h>
static uwb_config_t current,saved,active;
static uint32_t generation;
static bool savedValid,started,initialized,radioTouched,applied;
#define TLM_CAPACITY 4u
#define TLM_LENGTH 192u
static char telemetry[TLM_CAPACITY][TLM_LENGTH];
static unsigned head,tail,count,highWater;
static uint32_t dropped,accepted;
const uwb_config_t *uwb_runtime_active(void) {return &active;}
void uwb_runtime_telemetry(const char *line) {
  if(!active.telemetry) return;
  size_t length=0;while(length<TLM_LENGTH&&line[length]) length++;
  if(count==TLM_CAPACITY||length>TLM_LENGTH-3u) {dropped++;return;}
  memcpy(telemetry[head],line,length+1);head=(head+1)%TLM_CAPACITY;count++;
  if(count>highWater) highWater=count;
  accepted++;
}
static void reply(const char *s) {usb_console_write(s);}
static void stop(void) {
  if(radioTouched) mul_ads_radio_stop();
  started=false;
}
static bool apply(void) {
  const char *error=uwb_config_validate(&current);if(error) {reply(error);return false;}
  stop();applied=false;
  /* Failed apply leaves radio stopped and the previous active snapshot intact. */
  if(current.mode) {
    uint64_t session;if(!uwb_runtime_session(&session)) {reply("ERR RNG session unavailable; radio off");return false;}
    mul_ads_radio_configure(&current,session);radioTouched=true;
    if(mul_ads_radio_init()!=DWT_SUCCESS) {mul_ads_radio_stop();reply("ERR radio init/readback; radio off");return false;}
    started=true;
  }
  active=current;applied=true;
  /* Queued samples retain their encoded IDs, but old calibration data must not
   * be mistaken for the new active configuration. Account for discarded items. */
  dropped+=count;head=tail=count=0;
  reply("CFG APPLIED");return true;
}
void uwb_runtime_init(void) {
  stop();uwb_config_defaults(&current);generation=0;
  savedValid=uwb_config_load(uwb_runtime_store(),&saved,&generation);
  if(savedValid) current=saved;
  initialized=true;apply();
}
void uwb_runtime_task(void) {if(initialized&&started) mul_ads_radio_process();}
static void show(const char *which,const uwb_config_t *c) {
  char line[192];
  snprintf(line,sizeof(line),"CFG %s node=0x%02lX peer=0x%02lX identity=%lu mode=%lu role=%lu interval_ms=%lu phy=%lu",
    which,(unsigned long)c->node,(unsigned long)c->peer,(unsigned long)c->identity,(unsigned long)c->mode,(unsigned long)c->role,(unsigned long)c->interval_ms,(unsigned long)c->phy);reply(line);
  snprintf(line,sizeof(line),"CFG %s tx_ant_dtu=%lu rx_ant_dtu=%lu calibration=%lu correction_mm=%ld verbosity=%lu",
    which,(unsigned long)c->tx_ant_dtu,(unsigned long)c->rx_ant_dtu,(unsigned long)c->calibration,(long)c->correction_mm,(unsigned long)c->verbosity);reply(line);
  snprintf(line,sizeof(line),"CFG %s telemetry=%lu telemetry_ms=%lu samples=%lu reserved=0,0,0 version=1",
    which,(unsigned long)c->telemetry,(unsigned long)c->telemetry_ms,(unsigned long)c->samples);reply(line);
}
bool uwb_runtime_command(const char *line) {
  if(strncmp(line,"CFG",3)&&strncmp(line,"TLM",3)) return false;
  char copy[64];size_t n=0;while(n<sizeof(copy)&&line[n]) n++;
  if(n>=sizeof(copy)) {reply("ERR oversized configuration command");return true;}
  memcpy(copy,line,n+1);char *token[5]={0};unsigned nt=0;char *p=copy;
  while(*p) {
    if(*p==' ') {p++;continue;}
    if(nt==5) {reply("ERR too many command arguments");return true;}
    token[nt++]=p;while(*p && *p!=' ')p++;
    if(*p)*p++=0;
  }
  if(nt<2) {reply("CFG SHOW [CURRENT|ACTIVE|SAVED|DEFAULTS] | SET field value | APPLY | SAVE | LOAD | REVERT | DEFAULTS");return true;}
  if(!strcmp(token[0],"TLM")) {
    if(nt==2&&!strcmp(token[1],"NEXT")) {
      if(!count) reply("TLM EMPTY");
      else if(usb_console_try_write(telemetry[tail])) {tail=(tail+1)%TLM_CAPACITY;count--;}
    } else if(nt==2&&!strcmp(token[1],"STATUS")) {
      char text[192];snprintf(text,sizeof(text),"TLM STATUS occupancy=%u capacity=%u high_water=%u dropped=%lu accepted=%lu sink=USB_PULL uart_forwarding=0",
        count,TLM_CAPACITY,highWater,(unsigned long)dropped,(unsigned long)accepted);reply(text);
    } else reply("ERR TLM NEXT | STATUS");
  } else if(strcmp(token[0],"CFG")) reply("ERR unknown command");
  else if(!strcmp(token[1],"SHOW")&&(nt==2||nt==3)) {
    if(nt==2||!strcmp(token[2],"CURRENT")) {
      char text[192];snprintf(text,sizeof(text),"CFG STATUS saved=%u generation=%lu pending=%u radio_running=%u applied=%u uwb_only=1",
        savedValid,(unsigned long)generation,!applied||!uwb_config_equal(&current,&active),started,applied);reply(text);show("CURRENT",&current);
    } else if(!strcmp(token[2],"ACTIVE")) {show("ACTIVE",&active);if(!applied)reply("CFG ACTIVE snapshot not applied; radio off");}
    else if(!strcmp(token[2],"SAVED")) {if(savedValid)show("SAVED",&saved);else reply("CFG no valid saved configuration");}
    else if(!strcmp(token[2],"DEFAULTS")) {uwb_config_t defaults;uwb_config_defaults(&defaults);show("DEFAULTS",&defaults);}
    else reply("ERR unknown SHOW target");
  } else if(nt==4&&!strcmp(token[1],"SET")) {
    const char *field=!strcmp(token[2],"address")?"node":token[2];
    uint32_t oldTx=current.tx_ant_dtu,oldRx=current.rx_ant_dtu;
    const char *error=uwb_config_set(&current,field,token[3]);
    if(error) {reply("ERR invalid configuration");reply(error);}else {
      reply("CFG updated; APPLY required; not saved");
      if(oldTx!=current.tx_ant_dtu||oldRx!=current.rx_ant_dtu) reply("CFG antenna change disables calibration; refit before enabling");
    }
  } else if(nt==2&&!strcmp(token[1],"APPLY")) apply();
  else if(nt==2&&(!strcmp(token[1],"LOAD")||!strcmp(token[1],"REVERT"))) {
    uwb_config_t loaded;uint32_t gen;
    if(uwb_config_load(uwb_runtime_store(),&loaded,&gen)) {saved=current=loaded;generation=gen;savedValid=true;reply("CFG loaded; APPLY required");}
    else reply("ERR no valid saved configuration");
  } else if(nt==2&&!strcmp(token[1],"DEFAULTS")) {uwb_config_defaults(&current);reply("CFG defaults selected; APPLY required; not saved");}
  else if(nt==2&&!strcmp(token[1],"SAVE")) {
    /* FLASH stalls single-bank instruction fetch. Never attempt while ranging
     * or before confirmed STOP/STREAM0 on an attached e-puck. */
    if(started||!epuck_link_flash_safe()) reply("ERR SAVE requires radio off and safe robot maintenance state");
    else if(uwb_config_save(uwb_runtime_store(),&current,&generation)) {saved=current;savedValid=true;reply("CFG SAVED verified");}
    else reply("ERR SAVE failed; previous committed record retained if valid");
  } else reply("ERR malformed CFG command; use CFG for help");
  return true;
}
#endif
