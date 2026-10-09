/* Copyright 2026 dvbipitools authors. Licensed under GPL-3.0-or-later.
 * See NOTICE and LICENSE for details and authorship information. */

#include <errno.h>
#include <stdint.h>
#include <string.h>
#include <unistd.h>

#include "lib/cas/biss/biss.h"
#include "lib/cas/biss/ca.h"
#include "lib/demux/tspack.h"
#include "lib/helper/log.h"
#include "lib/helper/secure_zero.h"
#include "lib/sys/signal.h"

#include "pipeline.h"
#include "version.h"

#define BISS_CA_SYSTEM_ID 0x2602
#define BISS_CA_SYSTEM_ID_CA 0x2610

#define SC_SECTION_TID_ECM_EVEN 0x80
#define SC_SECTION_TID_ECM_ODD 0x81

static int algo_from_mode(unsigned char mode, scramble_algo_t *out) {
  switch (mode) {
    case 0x01:
    case 0x02:
      /* 0x01 DVB-CSA1, 0x02 DVB-CSA2: same cipher (libdvbcsa), differ only in CW convention */
      *out = SCRAMBLE_ALGO_CSA2;
      return 1;
    case 0x10:
      *out = SCRAMBLE_ALGO_CISSA;
      return 1;
    default:
      return 0;
  }
}

static void emit_downstream(void *ctx, const unsigned char pkt[188]);
static void emit_downstream_inspect(void *ctx, const unsigned char pkt[188]);

#define EMIT(lc) ((lc)->insp_out ? emit_downstream_inspect : emit_downstream)

/* edge-log gate, one rtmp target down never affects others */
void descramble_rtmp_note_result(int ok, int *had_error, int idx) {
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

void rtmp_fanout_cb(void *ctx, flv_tag_type_t type, uint32_t timestamp_ms, const unsigned char *hdr, size_t hn, const unsigned char *payload, size_t pn) {
  loop_ctx_t *lc = ctx;
  for (int i = 0; i < lc->n_rtmp; i++) descramble_rtmp_note_result(rtmpout_write(lc->rtmp[i], type, timestamp_ms, hdr, hn, payload, pn) >= 0, &lc->rtmp_had_error[i], i);
}

void srt_service_all(loop_ctx_t *lc) {
  for (int i = 0; i < lc->n_srt; i++) {
    srtsink_status_t st;
    srtsink_service(lc->srt[i], &st);
    if (st.connected != lc->srt_connected[i]) {
      log_line(TOOL_NAME ": srt[%d] output: %s", i, st.connected ? "connected" : "link down, reconnecting");
      lc->srt_connected[i] = st.connected;
    }
  }
}

static void handle_ecm_section(loop_ctx_t *lc) {
  const unsigned char *sec = lc->ecm_asm.buf;
  size_t seclen = lc->ecm_asm.expect;
  int parity;
  unsigned char cw[16];

  if (sec[0] == SC_SECTION_TID_ECM_EVEN)     parity = SCRAMBLE_PARITY_EVEN;
  else if (sec[0] == SC_SECTION_TID_ECM_ODD) parity = SCRAMBLE_PARITY_ODD;
  else return;

  if (device_resolve_cw(lc->dev, sec, seclen, psi_program_number(lc->psi), lc->cw_len, lc->ecm_pid, cw) != 0) {
    if (lc->cfg->ecm_profile.set) log_line(TOOL_NAME ": ecm_profile: CW resolve failed for this ECM section");
    lc->ecm_errors_total++;
    return;
  }
  if (lc->have_cw[parity] && memcmp(lc->last_cw[parity], cw, sizeof cw) == 0) { /* unchanged, same crypto period repeat */
    secure_zero(cw, sizeof cw);
    return;
  }

  memcpy(lc->last_cw[parity], cw, sizeof cw);
  lc->have_cw[parity] = 1;
  lc->cryptoperiod_transitions_total++;
  scrambler_set_key(lc->scr, parity, cw, (size_t)lc->cw_len, EMIT(lc), lc);
  secure_zero(cw, sizeof cw);
  log_line(TOOL_NAME ": CW updated (parity=%s)", parity == SCRAMBLE_PARITY_EVEN ? "even" : "odd");
}

static void update_biss_ca_cw(loop_ctx_t *lc, int parity, const unsigned char sw[BISS_CA_SW_LEN]) {
  if (lc->have_cw[parity] && memcmp(lc->last_cw[parity], sw, BISS_CA_SW_LEN) == 0) return;
  memcpy(lc->last_cw[parity], sw, BISS_CA_SW_LEN);
  lc->have_cw[parity] = 1;
  lc->cryptoperiod_transitions_total++;
  scrambler_set_key(lc->scr, parity, sw, BISS_CA_SW_LEN, EMIT(lc), lc);
  log_line(TOOL_NAME ": biss-ca: CW updated (parity=%s)", parity == SCRAMBLE_PARITY_EVEN ? "even" : "odd");
}

/* ECM update carries both parities SW (Tech 3292-s1 Table 13) */
static void handle_biss_ca_ecm_section(loop_ctx_t *lc) {
  unsigned char sw[2][BISS_CA_SW_LEN];
  if (biss_ca_state_resolve_ecm(lc->biss_ca, lc->ecm_asm.buf, lc->ecm_asm.expect, sw[SCRAMBLE_PARITY_EVEN], sw[SCRAMBLE_PARITY_ODD]) != 0) {
    lc->ecm_errors_total++;
    return;
  }
  update_biss_ca_cw(lc, SCRAMBLE_PARITY_EVEN, sw[SCRAMBLE_PARITY_EVEN]);
  update_biss_ca_cw(lc, SCRAMBLE_PARITY_ODD, sw[SCRAMBLE_PARITY_ODD]);
  secure_zero(sw, sizeof sw);
}

/* full or partial write-retry, EINTR aside. 0 ok, -1 error */
static int flush_outfd(const loop_ctx_t *lc, int i) {
  size_t off = 0;
  while (off < lc->outbuf_len) {
    ssize_t n = write(lc->outfd[i], lc->outbuf + off, lc->outbuf_len - off);
    if (n < 0) {
      if (errno == EINTR) continue;
      return -1;
    }
    off += (size_t)n;
  }
  return 0;
}

static int flush_all_outfd(loop_ctx_t *lc) {
  for (int i = 0; i < lc->n_outfd; i++) {
    if (flush_outfd(lc, i) < 0) {
      lc->outbuf_len = 0;
      return -1;
    }
  }
  lc->outbuf_len = 0;
  return 0;
}

/* stops writing after first fail within one flush. once emit_failed is set, later packets (same batch) must not still land on disk/mux */
static void emit_downstream(void *ctx, const unsigned char pkt[188]) {
  loop_ctx_t *lc = ctx;
  if (lc->emit_failed) return;
  if (lc->mkv) {
    mkv_feed(lc->mkv, pkt);
    if (mkv_error(lc->mkv)) {
      lc->emit_failed = 1;
      lc->output_errors_total++;
      return;
    }
  } else if (lc->n_outfd > 0) {
    if (lc->outbuf_len + 188 > sizeof lc->outbuf && flush_all_outfd(lc) < 0) {
      lc->emit_failed = 1;
      lc->output_errors_total++;
      return;
    }
    memcpy(lc->outbuf + lc->outbuf_len, pkt, 188);
    lc->outbuf_len += 188;
  }
  if (lc->flv) {
    flv_feed(lc->flv, pkt);
    if (flv_error(lc->flv)) {
      lc->emit_failed = 1;
      lc->output_errors_total++;
      return;
    }
  }
  for (int i = 0; i < lc->n_srt; i++) srtsink_write(lc->srt[i], pkt, 188);
  lc->packets++;
}

/* resolves BISS1/E key into (algo, cw, cw_len). cw aliases cfg->biss1_sw or fills sw_buf. 0 ok, -1 esw decrypt failed */
static int resolve_biss1e_key(const config_t *cfg, unsigned char sw_buf[BISS_KEY_LEN], scramble_algo_t *algo, const unsigned char **cw, size_t *cw_len) {
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
  lc->ipi = ipiclient_new(lc->cfg->unicast_emm_uri, lc->cfg->insecure_tls, lc->cfg->unicast_emm_token_header);
  if (!lc->ipi) {
    log_line(TOOL_NAME ": invalid -u/--unicast-emm uri, ignoring: %s", lc->cfg->unicast_emm_uri);
    return;
  }
  lc->ipi_pending = ipiclient_poll_start(lc->ipi);
}

void pipeline_service_unicast_emm(loop_ctx_t *lc) {
  ipiclient_poll_state_t st;
  if (!lc->ipi_pending) return;
  st = ipiclient_poll_step(lc->ipi_pending);
  if (st == IPICLIENT_POLL_PENDING) return;
  if (st == IPICLIENT_POLL_DONE) {
    if (ipiclient_poll_take(lc->ipi_pending, lc->cache, lc->dev)) lc->emmcache_dirty = 1;
  } else {
    ipiclient_poll_free(lc->ipi_pending);
  }
  lc->ipi_pending = NULL;
}

#define EMMCACHE_SAVE_DEBOUNCE_S 1.0

void pipeline_service_emmcache(loop_ctx_t *lc) {
  double now;
  if (!lc->emmcache_dirty) return;
  now = mono_seconds();
  if (now - lc->emmcache_last_save < EMMCACHE_SAVE_DEBOUNCE_S) return;
  emmcache_save(lc->cache, lc->emm_file);
  lc->emmcache_dirty = 0;
  lc->emmcache_last_save = now;
}

void pipeline_flush_emmcache(loop_ctx_t *lc) {
  if (!lc->emmcache_dirty) return;
  emmcache_save(lc->cache, lc->emm_file);
  lc->emmcache_dirty = 0;
}

static int detect_cas_scheme(loop_ctx_t *lc) {
  unsigned pmt_ca = psi_pmt_ca_system_id(lc->psi);
  switch (pmt_ca) {
    case BISS_CA_SYSTEM_ID_CA: {
      lc->cas_logged = 1;
      log_line(TOOL_NAME ": BISS Mode CA detected (ca_system_id=0x%04x)", BISS_CA_SYSTEM_ID_CA);
      if (lc->cfg->n_biss2_ca_key == 0) {
        log_line(TOOL_NAME ": BISS Mode CA stream detected, no --biss2-ca-key given");
        lc->fatal = 1;
        return 1;
      }
      lc->biss_ca = biss_ca_state_new(lc->cfg->biss2_ca_key, (size_t)lc->cfg->n_biss2_ca_key);
      if (!lc->biss_ca) {
        log_line(TOOL_NAME ": no usable RSA private key in --biss2-ca-key");
        lc->key_load_errors_total++;
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
      lc->cas_mode = "biss-ca";
      break;
    }
    case BISS_CA_SYSTEM_ID: {
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
        lc->key_load_errors_total++;
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
      lc->cas_mode = "biss1e";
      break;
    }
    default:
      if (psi_have_cat(lc->psi) && lc->ecm_pid && psi_emm_pid(lc->psi) && psi_scrambling_mode(lc->psi)) {
        scramble_algo_t algo;
        log_line(TOOL_NAME ": CAS parameters resolved: ecm_pid=0x%04x emm_pid=0x%04x ca_system_id=0x%04x scrambling_mode=0x%02x", lc->ecm_pid, psi_emm_pid(lc->psi), psi_ca_system_id(lc->psi), psi_scrambling_mode(lc->psi));
        lc->cas_logged = 1;
        if (!lc->cfg->key_path || !lc->cfg->serial || !lc->cfg->emm_file) {
          log_line(TOOL_NAME ": ECM/EMM CAS detected, no -k/-s/-e given, giving up");
          lc->fatal = 1;
          return 1;
        }
        lc->dev = device_state_new(lc->cfg->key_path, lc->cfg->serial, &lc->cfg->ecm_profile, lc->cfg->max_services);
        if (!lc->dev) {
          log_line(TOOL_NAME ": cannot load RSA private key from -k %s", lc->cfg->key_path);
          lc->key_load_errors_total++;
          lc->fatal = 1;
          return 1;
        }
        lc->cas_mode = "classic";
        if (emmcache_load(lc->cache, lc->dev, lc->emm_file) != 0) log_line(TOOL_NAME ": failed to read emm cache %s", lc->emm_file);
        if (lc->cfg->unicast_emm_uri) setup_unicast_emm(lc);
        if (algo_from_mode(psi_scrambling_mode(lc->psi), &algo)) {
          lc->scr = scrambler_new(algo);
          lc->cw_len = (int)scrambler_cw_len(algo);
        } else {
          log_line(TOOL_NAME ": unrecognized scrambling_mode 0x%02x", psi_scrambling_mode(lc->psi));
          lc->fatal = 1;
          return 1;
        }
      }
      break;
  }
  return 0;
}

static void emit_downstream_inspect(void *ctx, const unsigned char pkt[188]) {
  loop_ctx_t *lc = ctx;
  tsinspect_packet(lc->insp_out, pkt);
  emit_downstream(ctx, pkt);
}

int pkt_cb_inspect(void *v, const unsigned char *pkt) {
  loop_ctx_t *lc = v;
  tsinspect_packet(lc->insp_in, pkt);
  return pkt_cb(v, pkt);
}

int pkt_cb(void *v, const unsigned char *pkt) {
  loop_ctx_t *lc = v;
  unsigned pid;
  const unsigned char *pl;
  size_t plen;
  int pusi;

  psi_feed(lc->psi, pkt);
  pid = tspack_pid(pkt);
  if (!lc->ecm_pid && psi_classify(lc->psi, pid) == PID_ECM) lc->ecm_pid = pid;
  if (!lc->emm_pid && psi_have_cat(lc->psi)) lc->emm_pid = psi_emm_pid(lc->psi);
  if (!lc->cas_logged && psi_ready(lc->psi) && detect_cas_scheme(lc)) return 1;

  /* BISS 1/E signaling pid also classifies PID_ECM. guard lc->dev, not just lc->biss_ca */
  if (lc->ecm_pid && pid == lc->ecm_pid && tspack_payload(pkt, &pl, &plen, &pusi) && psi_section_asm_cc(&lc->ecm_asm, pkt)) {
    for (int got = psi_section_asm_feed(&lc->ecm_asm, pl, plen, pusi); got; got = psi_section_asm_next(&lc->ecm_asm, pl, plen)) {
      if (!lc->scr) continue;
      lc->ecm_total++;
      if (lc->biss_ca)  handle_biss_ca_ecm_section(lc);
      else if (lc->dev) handle_ecm_section(lc);
    }
  }

  if (lc->emm_pid && pid == lc->emm_pid && tspack_payload(pkt, &pl, &plen, &pusi) && psi_section_asm_cc(&lc->emm_asm, pkt)) {
    for (int got = psi_section_asm_feed(&lc->emm_asm, pl, plen, pusi); got; got = psi_section_asm_next(&lc->emm_asm, pl, plen)) {
      lc->emm_total++;
      if (lc->biss_ca) {
        biss_ca_state_on_emm(lc->biss_ca, lc->emm_asm.buf, lc->emm_asm.expect);
      } else if (lc->dev && emmcache_feed(lc->cache, lc->dev, lc->emm_asm.buf, lc->emm_asm.expect)) {
        lc->emmcache_dirty = 1;
      }
    }
  }

  /* pkt is always genuinely mutable (tspack_t's acc[] or main()'s buffer). const is just tspack_feed()'s callback contract */
  if (lc->scr) {
    /* -1: reserved control value or key not loaded yet. fwd as-is */
    if (scrambler_decrypt_packet_queued(lc->scr, (unsigned char *)pkt, EMIT(lc), lc) != 0) {
      lc->unexpected_clear_packets_total++;
      EMIT(lc)(lc, pkt);
    } else {
      lc->scrambled_packets_total++;
    }
  } else {
    EMIT(lc)(lc, pkt);
  }
  return lc->emit_failed ? 1 : 0;
}

/* drains scrambler_set_key()'s queued last batch packets through emit_downstream() at shutdown,
   then flushes any bytes still sitting in raw-fd output batch buffers */
void pipeline_flush(loop_ctx_t *lc) {
  scrambler_flush(lc->scr, EMIT(lc), lc);
  if (!lc->mkv) flush_all_outfd(lc);
}

void pipeline_push_metrics(metrics_exporter_t *mx, const loop_ctx_t *lc) {
  metrics_writer_t w;

  if (!metrics_exporter_due(mx, mono_seconds()) || metrics_exporter_begin(mx, &w, TOOL_VERSION))
    return;
  if (lc->cas_mode) {
    metrics_writer_put(&w, METRICS_ID_DESCRAMBLE_MODE, lc->cas_mode, 1);
    metrics_writer_put(&w, METRICS_ID_CAS_CRYPTOPERIOD_TRANSITIONS_TOTAL, lc->cas_mode, lc->cryptoperiod_transitions_total);
    metrics_writer_put(&w, METRICS_ID_CAS_ECM_TOTAL, lc->cas_mode, lc->ecm_total);
    metrics_writer_put(&w, METRICS_ID_CAS_ECM_ERRORS_TOTAL, lc->cas_mode, lc->ecm_errors_total);
    metrics_writer_put(&w, METRICS_ID_CAS_EMM_TOTAL, lc->cas_mode, lc->emm_total);
    metrics_writer_put(&w, METRICS_ID_CAS_EMM_DROPPED_TOTAL, lc->cas_mode, lc->dev ? emmcache_dropped_total(lc->cache) : 0);
  }
  metrics_writer_put(&w, METRICS_ID_CAS_SCRAMBLED_PACKETS_TOTAL, NULL, lc->scrambled_packets_total);
  metrics_writer_put(&w, METRICS_ID_CAS_UNEXPECTED_CLEAR_PACKETS_TOTAL, NULL, lc->unexpected_clear_packets_total);
  metrics_writer_put(&w, METRICS_ID_DESCRAMBLE_KEY_LOAD_ERRORS_TOTAL, NULL, lc->key_load_errors_total);
  metrics_writer_put(&w, METRICS_ID_DESCRAMBLE_OUTPUT_ERRORS_TOTAL, NULL, lc->output_errors_total);
  metrics_exporter_send(mx, &w);
}
