/* Copyright 2026 dvbipitools authors. Licensed under GPL-3.0-or-later.
 * See NOTICE and LICENSE for details and authorship information. */

#ifndef DIPIRADIOHEAD_CONFIG_PRIV_H
#define DIPIRADIOHEAD_CONFIG_PRIV_H

#include <stddef.h>

#include "../config.h"

typedef struct {
  int input;
  int have_input;
  int vendor;
  int have_vendor;
} item_state_t;

extern item_state_t rdh_item;

int rdh_set_buf(char *dst, size_t sz, const char *v, char *e, size_t n);
int rdh_set_pbkeylen(int *dst, const char *v, char *e, size_t n);
int rdh_add_input(config_t *cfg, const char *uri, char *e, size_t n);
int rdh_add_peer(config_t *cfg, const char *uri, char *e, size_t n);
int rdh_item_hook(void *c, const char *list, int begin, char *e, size_t n);
radio_input_t *rdh_cur_input(config_t *cfg, char *e, size_t n);
cas_vendor_t *rdh_cur_vendor(config_t *cfg, char *e, size_t n);

int rdh_apply_input(void *c, const char *v, char *e, size_t n);
int rdh_apply_input_sid(void *c, const char *v, char *e, size_t n);
int rdh_apply_input_sdt(void *c, const char *v, char *e, size_t n);
int rdh_apply_input_provider(void *c, const char *v, char *e, size_t n);

int rdh_apply_mcast(void *c, const char *v, char *e, size_t n);
int rdh_apply_out_iface(void *c, const char *v, char *e, size_t n);
int rdh_apply_rtp(void *c, const char *v, char *e, size_t n);
int rdh_apply_ttl(void *c, const char *v, char *e, size_t n);
int rdh_apply_dscp(void *c, const char *v, char *e, size_t n);
int rdh_apply_al_fec(void *c, const char *v, char *e, size_t n);
int rdh_apply_al_fec_port(void *c, const char *v, char *e, size_t n);
int rdh_apply_nit(void *c, const char *v, char *e, size_t n);
int rdh_apply_default_provider(void *c, const char *v, char *e, size_t n);
int rdh_apply_rist(void *c, const char *v, char *e, size_t n);
int rdh_apply_profile(void *c, const char *v, char *e, size_t n);
int rdh_apply_secret(void *c, const char *v, char *e, size_t n);
int rdh_apply_cname(void *c, const char *v, char *e, size_t n);
int rdh_apply_buffer(void *c, const char *v, char *e, size_t n);
int rdh_apply_srt_group_mode(void *c, const char *v, char *e, size_t n);
int rdh_apply_srt_passphrase(void *c, const char *v, char *e, size_t n);
int rdh_apply_srt_pbkeylen(void *c, const char *v, char *e, size_t n);
int rdh_apply_srt_streamid(void *c, const char *v, char *e, size_t n);
int rdh_apply_srt_packetfilter(void *c, const char *v, char *e, size_t n);
int rdh_apply_srt_latency(void *c, const char *v, char *e, size_t n);

int rdh_apply_cas_algo(void *c, const char *v, char *e, size_t n);
int rdh_apply_cas_cp_duration(void *c, const char *v, char *e, size_t n);
int rdh_apply_cas_fallback_clear(void *c, const char *v, char *e, size_t n);
int rdh_apply_cas_ecmg(void *c, const char *v, char *e, size_t n);
int rdh_apply_cas_required(void *c, const char *v, char *e, size_t n);
int rdh_apply_cas_ecmg_version(void *c, const char *v, char *e, size_t n);
int rdh_apply_cas_super_id(void *c, const char *v, char *e, size_t n);
int rdh_apply_cas_ecm_id(void *c, const char *v, char *e, size_t n);
int rdh_apply_cas_ecm_pid(void *c, const char *v, char *e, size_t n);
int rdh_apply_cas_emmg_port(void *c, const char *v, char *e, size_t n);
int rdh_apply_cas_emmg_max_conns(void *c, const char *v, char *e, size_t n);
int rdh_apply_cas_emmg_version(void *c, const char *v, char *e, size_t n);
int rdh_apply_cas_emmg_reverse(void *c, const char *v, char *e, size_t n);
int rdh_apply_cas_emm_pid(void *c, const char *v, char *e, size_t n);
int rdh_apply_cas_resilience(void *c, const char *v, char *e, size_t n);
int rdh_apply_cas_cwenc_algo(void *c, const char *v, char *e, size_t n);
int rdh_apply_cas_cwenc_aes_mode(void *c, const char *v, char *e, size_t n);
int rdh_apply_cas_cwenc_fixed_key(void *c, const char *v, char *e, size_t n);
int rdh_apply_cas_cwenc_key_list_a(void *c, const char *v, char *e, size_t n);
int rdh_apply_cas_cwenc_key_list_b(void *c, const char *v, char *e, size_t n);
int rdh_apply_biss1_sw(void *c, const char *v, char *e, size_t n);
int rdh_apply_biss2_sw(void *c, const char *v, char *e, size_t n);
int rdh_apply_biss2_emit_esw(void *c, const char *v, char *e, size_t n);
int rdh_apply_biss2_ca_receivers(void *c, const char *v, char *e, size_t n);
int rdh_apply_biss2_ca_session_id(void *c, const char *v, char *e, size_t n);

int rdh_apply_error(void *c, const char *v, char *e, size_t n);
int rdh_apply_insecure(void *c, const char *v, char *e, size_t n);
int rdh_apply_tsid(void *c, const char *v, char *e, size_t n);
int rdh_apply_onid(void *c, const char *v, char *e, size_t n);
int rdh_apply_verbose(void *c, const char *v, char *e, size_t n);
int rdh_apply_daemonize(void *c, const char *v, char *e, size_t n);
int rdh_apply_color(void *c, const char *v, char *e, size_t n);
int rdh_apply_metrics_sock(void *c, const char *v, char *e, size_t n);
int rdh_apply_metrics_id(void *c, const char *v, char *e, size_t n);
int rdh_apply_metrics_interval(void *c, const char *v, char *e, size_t n);
int rdh_apply_metrics_known_pids(void *c, const char *v, char *e, size_t n);
int rdh_apply_metrics_inspect_ts(void *c, const char *v, char *e, size_t n);

#endif
