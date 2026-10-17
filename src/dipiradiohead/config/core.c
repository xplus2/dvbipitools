/* Copyright 2026 dvbipitools authors. Licensed under GPL-3.0-or-later.
 * See NOTICE and LICENSE for details and authorship information. */

#include <stdio.h>
#include <string.h>

#include "lib/helper/argutil.h"
#include "lib/sys/ioutil.h"
#include "lib/helper/uriparse.h"
#include "lib/net/netconnect.h"
#include "../config.h"
#include "../input/inputset.h"
#include "../mux/tspacketizer.h"
#include "priv.h"

#define RDH_PID_CAT 0x0001
#define RDH_PID_NIT 0x0010
#define RDH_PID_SDT 0x0011
#define RDH_PID_EIT 0x0012
#define RDH_PID_TDT 0x0014 /* MPTS only */

static int pid_emitted(const config_t *cfg, unsigned pid) {
  unsigned n = cfg->n_inputs;
  if (pid == RDH_PID_CAT || pid == RDH_PID_NIT || pid == RDH_PID_SDT || pid == RDH_PID_EIT) return 1;
  if (n <= 1) return pid == TSPACKETIZER_PID_PMT || pid == TSPACKETIZER_PID_AUDIO;
  return pid == RDH_PID_TDT || (pid >= INPUTSET_AUDIO_PID_BASE && pid < INPUTSET_AUDIO_PID_BASE + n) ||
         (pid >= INPUTSET_PMT_PID_BASE && pid < INPUTSET_PMT_PID_BASE + n);
}

int rdh_cas_pid_clash(const config_t *cfg, unsigned *pid_out, int *is_emm) {
  for (unsigned vi = 0; vi < cfg->n_cas_vendors; vi++) {
    const unsigned pids[2] = {cfg->cas_vendors[vi].ecm_pid, cfg->cas_vendors[vi].emm_pid};
    for (int k = 0; k < 2; k++) {
      if (!pid_emitted(cfg, pids[k])) continue;
      *pid_out = pids[k];
      *is_emm = k;
      return 1;
    }
  }
  return 0;
}

void rdh_cfg_defaults(config_t *cfg) {
  memset(cfg, 0, sizeof *cfg);
  cfg->tsid = 1;
  cfg->onid = 1;
  cfg->dscp = NET_DSCP_VIDEO_HIGH;
  cfg->cas_cp_duration_ms = 10000;
}

int rdh_set_buf(char *dst, size_t sz, const char *v, char *e, size_t n) {
  if (strlen(v) >= sz) {
    snprintf(e, n, "too long (max %zu)", sz - 1);
    return -1;
  }
  bufcpy(dst, sz, v);
  return 0;
}

int rdh_set_pbkeylen(int *dst, const char *v, char *e, size_t n) {
  unsigned u;
  if (argutil_uint_range(v, 16, 32, &u) || (u != 16 && u != 24 && u != 32)) {
    snprintf(e, n, "invalid '%s' (16|24|32)", v);
    return -1;
  }
  *dst = (int)u;
  return 0;
}

int rdh_add_input(config_t *cfg, const char *uri, char *e, size_t n) {
  if (cfg->n_inputs >= RADIOHEAD_MAX_INPUTS) {
    snprintf(e, n, "too many inputs (max %d)", RADIOHEAD_MAX_INPUTS);
    return -1;
  }
  memset(&cfg->inputs[cfg->n_inputs], 0, sizeof cfg->inputs[0]);
  cfg->inputs[cfg->n_inputs].uri = uri;
  cfg->n_inputs++;
  return 0;
}

int rdh_add_peer(config_t *cfg, const char *uri, char *e, size_t n) {
  if (strncmp(uri, "rist://", 7) == 0) {
    if (cfg->n_srt > 0) {
      bufcpy(e, n, "rist:// and srt:// peers cannot mix in one run");
      return -1;
    }
    if (cfg->n_rist >= ARGS_MAX_RIST_PEERS) {
      snprintf(e, n, "too many peers (max %d)", ARGS_MAX_RIST_PEERS);
      return -1;
    }
    if (rdh_set_buf(cfg->rist_uri[cfg->n_rist], sizeof cfg->rist_uri[0], uri, e, n)) return -1;
    cfg->n_rist++;
    return 0;
  }
  if (strncmp(uri, "srt://", 6) == 0) {
    if (cfg->n_rist > 0) {
      bufcpy(e, n, "rist:// and srt:// peers cannot mix in one run");
      return -1;
    }
    if (cfg->n_srt >= ARGS_MAX_SRT_PEERS) {
      snprintf(e, n, "too many srt:// peers (max %d)", ARGS_MAX_SRT_PEERS);
      return -1;
    }
    if (uri[6] == '@') {
      bufcpy(e, n, "srt:// output always calls out, no listener mode");
      return -1;
    }
    if (argutil_addrport_parse(uri + 6, &cfg->srt_family[cfg->n_srt], cfg->srt_host[cfg->n_srt], sizeof cfg->srt_host[0], &cfg->srt_port[cfg->n_srt])) {
      snprintf(e, n, "invalid srt uri '%s'", uri);
      return -1;
    }
    cfg->n_srt++;
    return 0;
  }
  snprintf(e, n, "invalid uri '%s' (must start with rist:// or srt://)", uri);
  return -1;
}

int rdh_item_hook(void *c, const char *list, int begin, char *e, size_t n) {
  config_t *cfg = c;
  if (!strcmp(list, "input")) {
    cfg->parse_item.input = begin;
    if (begin) {
      cfg->parse_item.have_input = 0;
    } else if (!cfg->parse_item.have_input) {
      bufcpy(e, n, "missing input");
      return -1;
    }
  } else if (!strcmp(list, "cas.ecmg")) {
    cfg->parse_item.vendor = begin;
    if (begin) {
      cfg->parse_item.have_vendor = 0;
    } else if (!cfg->parse_item.have_vendor) {
      bufcpy(e, n, "missing ecmg");
      return -1;
    }
  }
  return 0;
}

radio_input_t *rdh_cur_input(config_t *cfg, char *e, size_t n) {
  if (!cfg->parse_item.input || !cfg->parse_item.have_input) {
    bufcpy(e, n, "only valid inside an input list");
    return NULL;
  }
  return &cfg->inputs[cfg->n_inputs - 1];
}

cas_vendor_t *rdh_cur_vendor(config_t *cfg, char *e, size_t n) {
  if (!cfg->parse_item.vendor || !cfg->parse_item.have_vendor) {
    bufcpy(e, n, "only valid inside a cas.ecmg list");
    return NULL;
  }
  return &cfg->cas_vendors[cfg->n_cas_vendors - 1];
}
