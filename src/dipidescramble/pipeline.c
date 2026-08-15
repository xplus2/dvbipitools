/* Copyright 2026 dvbipitools authors. Licensed under GPL-3.0-or-later.
 * See NOTICE and LICENSE for details and authorship information. */

#include <stdint.h>
#include <string.h>
#include <unistd.h>

#include "lib/cas/biss/biss.h"
#include "lib/cas/biss/ca.h"
#include "lib/demux/tspack.h"
#include "lib/log.h"

#include "pipeline.h"
#include "version.h"

#define BISS_CA_SYSTEM_ID 0x2602
#define BISS_CA_SYSTEM_ID_CA 0x2610

#define SC_SECTION_TID_ECM_EVEN 0x80
#define SC_SECTION_TID_ECM_ODD 0x81

static int algo_from_mode(unsigned char mode, scramble_algo_t *out) {
  if (mode == 0x01 || mode == 0x02) {
    /* 0x01 DVB-CSA1, 0x02 DVB-CSA2: same cipher (libdvbcsa), differ only in CW convention */
    *out = SCRAMBLE_ALGO_CSA2;
    return 1;
  }
  if (mode == 0x10) {
    *out = SCRAMBLE_ALGO_CISSA;
    return 1;
  }
  return 0;
}

static void emit_downstream(void *ctx, const unsigned char pkt[188]);

/* edge-log gate, one rtmp target down never affects others */
static void rtmp_note_result(int ok, int *had_error, int idx) {
  if (!ok) {
    if (!*had_error) {
      log_line(TOOL_NAME ": rtmp[%d] output: write failed, will keep retrying", idx);
      *had_error = 1;
    }
  } else if (*had_error) {
    log_line(TOOL_NAME ": rtmp[%d] output: recovered", idx);
    *had_error = 0;
  }
}

void rtmp_fanout_cb(void *ctx, flv_tag_type_t type, uint32_t timestamp_ms, const unsigned char *data, size_t len) {
  loop_ctx_t *lc = ctx;
  int i;
  for (i = 0; i < lc->n_rtmp; i++)
    rtmp_note_result(rtmpout_write(lc->rtmp[i], type, timestamp_ms, data, len) >= 0, &lc->rtmp_had_error[i], i);
}

static void handle_ecm_section(loop_ctx_t *lc) {
  const unsigned char *sec = lc->ecm_asm.buf;
  size_t seclen = lc->ecm_asm.expect;
  int parity;
  unsigned char cw[16];

  if (sec[0] == SC_SECTION_TID_ECM_EVEN)
    parity = SCRAMBLE_PARITY_EVEN;
  else if (sec[0] == SC_SECTION_TID_ECM_ODD)
    parity = SCRAMBLE_PARITY_ODD;
  else
    return;

  if (device_resolve_cw(lc->dev, sec, seclen, psi_program_number(lc->psi), lc->cw_len, lc->ecm_pid, cw) != 0) {
    if (lc->cfg->ecm_profile.set)
      log_line(TOOL_NAME ": ecm_profile: CW resolve failed for this ECM section");
    return;
  }
  if (lc->have_cw[parity] && memcmp(lc->last_cw[parity], cw, sizeof cw) == 0)
    return; /* unchanged, same crypto-period repeat */

  memcpy(lc->last_cw[parity], cw, sizeof cw);
  lc->have_cw[parity] = 1;
  scrambler_set_key(lc->scr, parity, cw, (size_t)lc->cw_len, emit_downstream, lc);
  log_line(TOOL_NAME ": CW updated (parity=%s)", parity == SCRAMBLE_PARITY_EVEN ? "even" : "odd");
}

/* one ECM update carries both parities' SW at once (Tech 3292-s1 Table 13), unlike the
   even/odd-table_id-split ECM handled by handle_ecm_section() above */
static void handle_biss_ca_ecm_section(loop_ctx_t *lc) {
  unsigned char sw[2][BISS_CA_SW_LEN];

  if (biss_ca_state_resolve_ecm(lc->biss_ca, lc->ecm_asm.buf, lc->ecm_asm.expect, sw[SCRAMBLE_PARITY_EVEN], sw[SCRAMBLE_PARITY_ODD]) != 0)
    return;
  if (!lc->have_cw[SCRAMBLE_PARITY_EVEN] || memcmp(lc->last_cw[SCRAMBLE_PARITY_EVEN], sw[SCRAMBLE_PARITY_EVEN], BISS_CA_SW_LEN) != 0) {
    memcpy(lc->last_cw[SCRAMBLE_PARITY_EVEN], sw[SCRAMBLE_PARITY_EVEN], BISS_CA_SW_LEN);
    lc->have_cw[SCRAMBLE_PARITY_EVEN] = 1;
    scrambler_set_key(lc->scr, SCRAMBLE_PARITY_EVEN, sw[SCRAMBLE_PARITY_EVEN], BISS_CA_SW_LEN, emit_downstream, lc);
    log_line(TOOL_NAME ": biss-ca: CW updated (parity=even)");
  }
  if (!lc->have_cw[SCRAMBLE_PARITY_ODD] || memcmp(lc->last_cw[SCRAMBLE_PARITY_ODD], sw[SCRAMBLE_PARITY_ODD], BISS_CA_SW_LEN) != 0) {
    memcpy(lc->last_cw[SCRAMBLE_PARITY_ODD], sw[SCRAMBLE_PARITY_ODD], BISS_CA_SW_LEN);
    lc->have_cw[SCRAMBLE_PARITY_ODD] = 1;
    scrambler_set_key(lc->scr, SCRAMBLE_PARITY_ODD, sw[SCRAMBLE_PARITY_ODD], BISS_CA_SW_LEN, emit_downstream, lc);
    log_line(TOOL_NAME ": biss-ca: CW updated (parity=odd)");
  }
}

/* stops writing after first fail within one flush. once emit_failed is set, later packets (same batch) must not still land on disk/mux */
static void emit_downstream(void *ctx, const unsigned char pkt[188]) {
  loop_ctx_t *lc = ctx;
  int i;
  if (lc->emit_failed)
    return;
  if (lc->mkv) {
    mkv_feed(lc->mkv, pkt);
    if (mkv_error(lc->mkv)) {
      lc->emit_failed = 1;
      return;
    }
  } else {
    for (i = 0; i < lc->n_outfd; i++)
      if (write(lc->outfd[i], pkt, 188) != 188) {
        lc->emit_failed = 1;
        return;
      }
  }
  if (lc->flv) {
    flv_feed(lc->flv, pkt);
    if (flv_error(lc->flv)) {
      lc->emit_failed = 1;
      return;
    }
  }
  lc->packets++;
}

/* resolves BISS1/E key material into (algo, cw, cw_len); cw aliases cfg->biss1_sw or
   fills sw_buf, per biss1_sw / biss2_sw / biss2_esw precedence. 0 ok, -1 esw decrypt failed */
static int resolve_biss1e_key(const config_t *cfg, unsigned char sw_buf[BISS_KEY_LEN],
                               scramble_algo_t *algo, const unsigned char **cw, size_t *cw_len) {
  if (cfg->biss1_sw_given) {
    *algo = SCRAMBLE_ALGO_CSA2;
    *cw = cfg->biss1_sw;
    *cw_len = BISS1_KEY_LEN;
    return 0;
  }
  *algo = SCRAMBLE_ALGO_CISSA;
  *cw = sw_buf;
  *cw_len = BISS_KEY_LEN;
  if (cfg->biss2_sw_given) {
    memcpy(sw_buf, cfg->biss2_sw, BISS_KEY_LEN);
    return 0;
  }
  return biss_esw_decrypt(cfg->biss2_id, cfg->biss2_esw, sw_buf) == 0 ? 0 : -1;
}

static void setup_unicast_emm(loop_ctx_t *lc) {
  lc->ipi = ipiclient_new(lc->cfg->unicast_emm_uri, lc->cfg->insecure_tls);
  if (!lc->ipi)
    log_line(TOOL_NAME ": invalid -u/--unicast-emm uri, ignoring: %s", lc->cfg->unicast_emm_uri);
  else if (ipiclient_poll(lc->ipi, lc->cache, lc->dev))
    emmcache_save(lc->cache, lc->emm_file);
}

int pkt_cb(void *v, const unsigned char *pkt) {
  loop_ctx_t *lc = v;
  unsigned pid;
  const unsigned char *pl;
  size_t plen;
  int pusi;

  psi_feed(lc->psi, pkt);
  pid = tspack_pid(pkt);

  if (!lc->ecm_pid && psi_classify(lc->psi, pid) == PID_ECM)
    lc->ecm_pid = pid;
  if (!lc->emm_pid && psi_have_cat(lc->psi))
    lc->emm_pid = psi_emm_pid(lc->psi);

  if (!lc->cas_logged && psi_ready(lc->psi)) {
    unsigned pmt_ca = psi_pmt_ca_system_id(lc->psi);

    if (pmt_ca == BISS_CA_SYSTEM_ID_CA) {
      lc->cas_logged = 1;
      log_line(TOOL_NAME ": BISS Mode CA detected (ca_system_id=0x%04x)", BISS_CA_SYSTEM_ID_CA);
      if (!lc->cfg->biss2_ca_key_path) {
        log_line(TOOL_NAME ": BISS Mode CA stream detected, no --biss2-ca-key given");
        lc->fatal = 1;
        return 1;
      }
      lc->biss_ca = biss_ca_state_new(lc->cfg->biss2_ca_key_path);
      if (!lc->biss_ca) {
        log_line(TOOL_NAME ": cannot load RSA private key from --biss2-ca-key %s", lc->cfg->biss2_ca_key_path);
        lc->fatal = 1;
        return 1;
      }
      lc->scr = scrambler_new(SCRAMBLE_ALGO_CISSA);
      if (!lc->scr) {
        log_line(TOOL_NAME ": failed to set up BISS-CA descrambler");
        lc->fatal = 1;
        return 1;
      }
      lc->cw_len = (int)scrambler_cw_len(SCRAMBLE_ALGO_CISSA);
    } else if (pmt_ca == BISS_CA_SYSTEM_ID) {
      unsigned char sw[BISS_KEY_LEN];
      scramble_algo_t algo;
      const unsigned char *cw;
      size_t cw_len;
      lc->cas_logged = 1;
      log_line(TOOL_NAME ": BISS Mode 1/E detected (ca_system_id=0x%04x)", BISS_CA_SYSTEM_ID);
      if (!lc->cfg->biss1_sw_given && !lc->cfg->biss2_sw_given && !lc->cfg->biss2_esw_given) {
        log_line(TOOL_NAME ": BISS stream detected, no --biss1-sw/--biss2-sw/--biss2-esw given");
        lc->fatal = 1;
        return 1;
      }
      if (resolve_biss1e_key(lc->cfg, sw, &algo, &cw, &cw_len) != 0) {
        log_line(TOOL_NAME ": failed to decrypt --biss2-esw (no OpenSSL in this build?)");
        lc->fatal = 1;
        return 1;
      }
      lc->scr = scrambler_new(algo);
      if (!lc->scr) {
        log_line(TOOL_NAME ": failed to set up BISS descrambler");
        lc->fatal = 1;
        return 1;
      }
      lc->cw_len = (int)scrambler_cw_len(algo);
      scrambler_set_key(lc->scr, SCRAMBLE_PARITY_EVEN, cw, cw_len, NULL, NULL);
      scrambler_set_key(lc->scr, SCRAMBLE_PARITY_ODD, cw, cw_len, NULL, NULL);
    } else if (psi_have_cat(lc->psi) && lc->ecm_pid && psi_emm_pid(lc->psi) && psi_scrambling_mode(lc->psi)) {
      scramble_algo_t algo;
      log_line(TOOL_NAME ": CAS parameters resolved: ecm_pid=0x%04x emm_pid=0x%04x ca_system_id=0x%04x scrambling_mode=0x%02x", lc->ecm_pid, psi_emm_pid(lc->psi), psi_ca_system_id(lc->psi), psi_scrambling_mode(lc->psi));
      lc->cas_logged = 1;
      if (!lc->cfg->key_path || !lc->cfg->serial || !lc->cfg->emm_file) {
        log_line(TOOL_NAME ": ECM/EMM CAS detected, no -k/-s/-e given, giving up");
        lc->fatal = 1;
        return 1;
      }
      lc->dev = device_state_new(lc->cfg->key_path, lc->cfg->serial, &lc->cfg->ecm_profile);
      if (!lc->dev) {
        log_line(TOOL_NAME ": cannot load RSA private key from -k %s", lc->cfg->key_path);
        lc->fatal = 1;
        return 1;
      }
      if (emmcache_load(lc->cache, lc->dev, lc->emm_file) != 0)
        log_line(TOOL_NAME ": failed to read emm cache %s, continuing without it", lc->emm_file);
      if (lc->cfg->unicast_emm_uri)
        setup_unicast_emm(lc);
      if (algo_from_mode(psi_scrambling_mode(lc->psi), &algo)) {
        lc->scr = scrambler_new(algo);
        lc->cw_len = (int)scrambler_cw_len(algo);
      } else {
        log_line(TOOL_NAME ": unrecognized scrambling_mode 0x%02x, cannot descramble", psi_scrambling_mode(lc->psi));
        lc->fatal = 1;
        return 1;
      }
    }
  }

  /* BISS 1/E signaling pid also classifies PID_ECM. guard lc->dev, not just lc->biss_ca */
  if (lc->ecm_pid && pid == lc->ecm_pid && tspack_payload(pkt, &pl, &plen, &pusi) && psi_section_asm_feed(&lc->ecm_asm, pl, plen, pusi) && lc->scr) {
    if (lc->biss_ca)
      handle_biss_ca_ecm_section(lc);
    else if (lc->dev)
      handle_ecm_section(lc);
  }

  if (lc->emm_pid && pid == lc->emm_pid && tspack_payload(pkt, &pl, &plen, &pusi) && psi_section_asm_feed(&lc->emm_asm, pl, plen, pusi)) {
    if (lc->biss_ca) {
      biss_ca_state_on_emm(lc->biss_ca, lc->emm_asm.buf, lc->emm_asm.expect);
    } else if (lc->dev && emmcache_feed(lc->cache, lc->dev, lc->emm_asm.buf, lc->emm_asm.expect)) {
      emmcache_save(lc->cache, lc->emm_file);
    }
  }

  /* pkt is always genuinely mutable (tspack_t's acc[] or main()'s buffer). const is just tspack_feed()'s callback contract */
  if (lc->scr) {
    /* -1: reserved control value or key not loaded yet. fwd as-is */
    if (scrambler_decrypt_packet_queued(lc->scr, (unsigned char *)pkt, emit_downstream, lc) != 0)
      emit_downstream(lc, pkt);
  } else {
    emit_downstream(lc, pkt);
  }
  return lc->emit_failed ? 1 : 0;
}

/* drains scrambler_set_key()'s queued last-batch packets through emit_downstream() at shutdown */
void pipeline_flush(loop_ctx_t *lc) {
  scrambler_flush(lc->scr, emit_downstream, lc);
}
