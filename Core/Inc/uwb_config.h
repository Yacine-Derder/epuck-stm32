/* SPDX-License-Identifier: MIT */
#ifndef UWB_CONFIG_H
#define UWB_CONFIG_H
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
/* Explicit little-endian wire schema; never persist a C structure. */
#define UWB_CONFIG_VERSION 2u
#define UWB_MAX_PEERS 8u
typedef struct {uint8_t address,enabled;uint16_t interval_ms;} uwb_peer_t;
#define UWB_CONFIG_RECORD_SIZE 128u
#define UWB_CONFIG_SECTOR_SIZE 0x20000u
#define UWB_CONFIG_FLASH_START UINT32_C(0x080C0000)
#define UWB_CONFIG_FLASH_END UINT32_C(0x08100000)
typedef struct {
  uint32_t node, peer, identity, mode, role, interval_ms, phy;
  uint32_t tx_ant_dtu, rx_ant_dtu, calibration;
  int32_t correction_mm;
  uint32_t verbosity, telemetry, telemetry_ms, samples, wifi,telemetry_format,peer_count;
  uwb_peer_t peers[UWB_MAX_PEERS];
} uwb_config_t;
/* identity: 0 standalone / 1 ALR / 2 RLR; mode: 0 off / 1 ADS;
 * role: 1 initiator / 2 responder; phy: 0 proven mixed-radio channel-5 profile. */
void uwb_config_defaults(uwb_config_t *c);
const char *uwb_config_validate(const uwb_config_t *c);
bool uwb_config_equal(const uwb_config_t *a,const uwb_config_t *b);
/* Strict single-field candidate update; no mutation on rejection. Case-insensitive
 * fields/enum words, numeric aliases, decimal/0x IDs and signed decimal mm.
 * Persistence/activation is performed by the runtime transaction, not here. */
const char *uwb_config_set(uwb_config_t *c,const char *field,const char *value);
uint32_t uwb_config_crc(const uint8_t *data,size_t size);
void uwb_config_encode(uint8_t out[UWB_CONFIG_RECORD_SIZE],const uwb_config_t *c,uint32_t generation);
bool uwb_config_decode(const uint8_t in[UWB_CONFIG_RECORD_SIZE],uwb_config_t *c,uint32_t *generation);
/* Backends address only offsets within the two reserved sectors. program()
 * writes one aligned word, erase() one sector. Return false on any failure. */
typedef struct {
  void *context;
  bool (*read)(void *,uint32_t,uint8_t *,size_t);
  bool (*program)(void *,uint32_t,uint32_t);
  bool (*erase)(void *,unsigned);
} uwb_config_store_t;
bool uwb_config_load(const uwb_config_store_t *store,uwb_config_t *c,uint32_t *generation);
bool uwb_config_save(const uwb_config_store_t *store,const uwb_config_t *c,uint32_t *generation);
/* Software correction changes a derived output only. Invalid raw input remains
 * invalid; a negative corrected value also remains invalid, without clamping. */
bool uwb_config_range(const uwb_config_t *c,double raw_m,bool raw_valid,double *calibrated_m);
#endif
