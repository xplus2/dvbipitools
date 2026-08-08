/* Copyright 2026 dvbipitools authors. Licensed under GPL-3.0-or-later.
 * See NOTICE and LICENSE for details and authorship information. */

#include <errno.h>
#include <fcntl.h>
#include <stdio.h>
#include <string.h>
#include <time.h>
#include <unistd.h>

#include "lib/demux/mpts_probe.h"
#include "lib/demux/psi.h"
#include "lib/demux/psi_section_asm.h"
#include "lib/demux/tspack.h"
#include "lib/log.h"
#include "lib/mux/mkv.h"
#include "lib/net/tssource.h"
#include "lib/scrambler/scrambler.h"
#include "lib/signal.h"

#include "args.h"
#include "device.h"
#include "emmcache.h"
#include "ipiclient.h"
#include "version.h"

#define SC_SECTION_TID_ECM_EVEN 0x80
#define SC_SECTION_TID_ECM_ODD 0x81
#define MPTS_NAME_WAIT_MS 3000

typedef struct {
  int out;
  mkv_t *mkv; /* NULL unless -f mkv|mka */
  unsigned long long mkv_bytes;
  unsigned long long packets;
  psi_t *psi;
  unsigned ecm_pid, emm_pid; /* 0 = not yet resolved */
  int cas_logged;
  device_state_t *dev;
  emmcache_t *cache;
  const char *emm_file;
  psi_section_asm_t ecm_asm, emm_asm;
  scrambler_t *scr; /* NULL until scrambling_mode resolved */
  int cw_len;
  int have_cw[2]; /* indexed by SCRAMBLE_PARITY_EVEN/_ODD */
  unsigned char last_cw[2][16];
  /* set by emit_downstream(). a void scrambler_emit_cb can't return an error code. this is how a failed mkv_feed/write reaches pkt_cb's int return */
  int emit_failed;
} loop_ctx_t;

static int open_output(const char *path) {
  int fd;
  if (strcmp(path, "-") == 0)
    return STDOUT_FILENO;
  fd = open(path, O_WRONLY | O_CREAT | O_TRUNC, 0644);
  if (fd < 0)
    log_line(TOOL_NAME ": cannot open -o %s: %s", path, strerror(errno));
  return fd;
}

static int algo_from_mode(unsigned char mode, scramble_algo_t *out) {
  if (mode == 0x02) {
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

  if (device_resolve_cw(lc->dev, sec, seclen, psi_program_number(lc->psi), lc->cw_len, cw) != 0)
    return;
  if (lc->have_cw[parity] && memcmp(lc->last_cw[parity], cw, sizeof cw) == 0)
    return; /* unchanged, same crypto-period repeat */

  memcpy(lc->last_cw[parity], cw, sizeof cw);
  lc->have_cw[parity] = 1;
  scrambler_set_key(lc->scr, parity, cw, (size_t)lc->cw_len, emit_downstream, lc);
  log_line(TOOL_NAME ": CW updated (parity=%s)", parity == SCRAMBLE_PARITY_EVEN ? "even" : "odd");
}

/* stops writing after first fail within one flush. once emit_failed is set, later packets (same batch) must not still land on disk/mux */
static void emit_downstream(void *ctx, const unsigned char pkt[188]) {
  loop_ctx_t *lc = ctx;
  if (lc->emit_failed)
    return;
  if (lc->mkv) {
    mkv_feed(lc->mkv, pkt);
    if (mkv_error(lc->mkv)) {
      lc->emit_failed = 1;
      return;
    }
  } else if (write(lc->out, pkt, 188) != 188) {
    lc->emit_failed = 1;
    return;
  }
  lc->packets++;
}

/* CAS resolve -> ECM/EMM reassembly -> CW resolve -> descramble in place */
static int pkt_cb(void *v, const unsigned char *pkt) {
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

  if (!lc->cas_logged && psi_ready(lc->psi) && psi_have_cat(lc->psi) && lc->ecm_pid && psi_emm_pid(lc->psi) && psi_scrambling_mode(lc->psi)) {
    scramble_algo_t algo;
    log_line(TOOL_NAME ": CAS parameters resolved: ecm_pid=0x%04x emm_pid=0x%04x ca_system_id=0x%04x scrambling_mode=0x%02x", lc->ecm_pid, psi_emm_pid(lc->psi), psi_ca_system_id(lc->psi), psi_scrambling_mode(lc->psi));
    lc->cas_logged = 1;
    if (algo_from_mode(psi_scrambling_mode(lc->psi), &algo)) {
      lc->scr = scrambler_new(algo);
      lc->cw_len = (int)scrambler_cw_len(algo);
    } else {
      log_line(TOOL_NAME ": unrecognized scrambling_mode 0x%02x, cannot descramble", psi_scrambling_mode(lc->psi));
    }
  }

  /* lc->scr may have just been created above - checked here so this pid's first-ever ECM section isn't missed */
  if (lc->ecm_pid && pid == lc->ecm_pid && tspack_payload(pkt, &pl, &plen, &pusi) && psi_section_asm_feed(&lc->ecm_asm, pl, plen, pusi) && lc->scr)
    handle_ecm_section(lc);

  if (lc->emm_pid && pid == lc->emm_pid && tspack_payload(pkt, &pl, &plen, &pusi) && psi_section_asm_feed(&lc->emm_asm, pl, plen, pusi)) {
    if (emmcache_feed(lc->cache, lc->dev, lc->emm_asm.buf, lc->emm_asm.expect))
      emmcache_save(lc->cache, lc->emm_file);
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

static tssrc_kind_t tssrc_kind_of(input_kind_t k) {
  switch (k) {
  case INPUT_RTP:
    return TSSRC_RTP;
  case INPUT_UDP:
    return TSSRC_UDP;
  case INPUT_STDIN:
    return TSSRC_STDIN;
  }
  return TSSRC_STDIN;
}

/* mpts discovery + -p decision. 0: proceed (pmt_pid/all_pids/n_all_pids
 * filled in). 1: abort, message already printed. */
static int resolve_pmt_selection(const config_t *cfg, tssrc_t *src, unsigned *pmt_pid,
                                  unsigned *all_pids, int *n_all_pids) {
  mpts_probe_result_t probe;
  int k;

  *pmt_pid = 0;
  *n_all_pids = 0;

  probe = mpts_probe_run(src, MPTS_NAME_WAIT_MS);
  if (probe.kind == MPTS_PROBE_FAIL) {
    log_line(TOOL_NAME ": no PAT received, giving up");
    return 1;
  }
  if (probe.kind == MPTS_PROBE_SPTS) {
    if (cfg->pmt_sel != PMT_SEL_AUTO)
      log_line(TOOL_NAME ": -p ignored, single-program source");
    return 0;
  }

  if (cfg->pmt_sel == PMT_SEL_AUTO) {
    mpts_probe_print_programs(TOOL_NAME, &probe);
    log_line(TOOL_NAME ": MPTS source - pick one with -p <pid>, or -p all");
    return 1;
  }
  if (cfg->pmt_sel == PMT_SEL_ALL) {
    if (cfg->format == FMT_MKV) {
      log_line(TOOL_NAME ": -f mkv can't hold multiple programs, pick one with -p <pid>");
      mpts_probe_print_programs(TOOL_NAME, &probe);
      return 1;
    }
    for (k = 0; k < probe.program_count; k++)
      all_pids[(*n_all_pids)++] = probe.programs[k].pmt_pid;
    return 0;
  }
  for (k = 0; k < probe.program_count; k++)
    if (probe.programs[k].pmt_pid == cfg->pmt_pid) {
      *pmt_pid = cfg->pmt_pid;
      return 0;
    }
  log_line(TOOL_NAME ": -p 0x%04x not found in this MPTS", cfg->pmt_pid);
  mpts_probe_print_programs(TOOL_NAME, &probe);
  return 1;
}

int main(int argc, char **argv) {
  config_t cfg;
  args_status_t st;
  tssrc_cfg_t tc;
  tssrc_t *src;
  tspack_t pz;
  loop_ctx_t lc;
  ipiclient_t *ipi;
  unsigned char buf[65536];
  char mkv_app_name[64];
  mkv_opts_t mkv_opts;
  double start, last_stat;
  char in_desc[128];
  unsigned pmt_pid, all_pids[PSI_MAX_PROGRAMS];
  int n_all_pids;

  log_set_color(log_color_prescan(argc, argv));
  log_line_ansi("\e[1m%s\e[0m \e[0;32mv%s\e[0m \e[0;37m%s\e[0m \e[0;37m%s\e[0m \e[0;34m%s\e[0m", TOOL_NAME, TOOL_VERSION, BUILD_ARCH, BUILD_TYPE, BUILD_LINK);
  st = args_parse(argc, argv, &cfg);
  if (st == ARGS_OK)
    log_set_color((log_color_t)cfg.color_mode);
  if (st == ARGS_HELP)
    return 0;
  if (st == ARGS_ERR) {
    fprintf(stderr, "try '%s --help' for usage\n", TOOL_NAME);
    return 2;
  }

  input_describe(&cfg.input, in_desc, sizeof in_desc);
  log_line(TOOL_NAME ": i:%s k:%s s:%s e:%s o:%s%s", in_desc, cfg.key_path, cfg.serial, cfg.emm_file, cfg.out_path, cfg.unicast_emm_uri ? " unicast-emm:yes" : "");

  lc.dev = device_state_new(cfg.key_path, cfg.serial);
  if (!lc.dev) {
    log_line(TOOL_NAME ": cannot load RSA private key from -k %s", cfg.key_path);
    return 1;
  }
  lc.cache = emmcache_new();
  if (!lc.cache) {
    device_state_free(lc.dev);
    return 1;
  }

  memset(&tc, 0, sizeof tc);
  tc.kind = tssrc_kind_of(cfg.input.kind);
  tc.family = cfg.input.family;
  tc.group = cfg.input.group;
  tc.port = cfg.input.port;
  tc.iface = cfg.iface_in;
  tc.user_agent = TOOL_NAME "/" TOOL_VERSION;

  src = tssrc_open(&tc, NULL);
  if (!src) {
    log_line(TOOL_NAME ": cannot open -i %s", in_desc);
    emmcache_free(lc.cache);
    device_state_free(lc.dev);
    return 1;
  }

  if (resolve_pmt_selection(&cfg, src, &pmt_pid, all_pids, &n_all_pids)) {
    tssrc_close(src);
    emmcache_free(lc.cache);
    device_state_free(lc.dev);
    return 1;
  }

  lc.out = open_output(cfg.out_path);
  if (lc.out < 0) {
    tssrc_close(src);
    emmcache_free(lc.cache);
    device_state_free(lc.dev);
    return 1;
  }
  lc.mkv = NULL;
  lc.mkv_bytes = 0;
  if (cfg.format == FMT_MKV || cfg.format == FMT_MKA) {
    snprintf(mkv_app_name, sizeof mkv_app_name, "%s %s", TOOL_NAME, TOOL_VERSION);
    memset(&mkv_opts, 0, sizeof mkv_opts);
    mkv_opts.audio_all = 1;
    mkv_opts.app_name = mkv_app_name;
    mkv_opts.source_desc = in_desc;
    if (n_all_pids > 0)
      lc.mkv = mkv_new(lc.out, &mkv_opts, cfg.format == FMT_MKV, &lc.mkv_bytes, all_pids, n_all_pids);
    else if (pmt_pid)
      lc.mkv = mkv_new(lc.out, &mkv_opts, cfg.format == FMT_MKV, &lc.mkv_bytes, &pmt_pid, 1);
    else
      lc.mkv = mkv_new(lc.out, &mkv_opts, cfg.format == FMT_MKV, &lc.mkv_bytes, NULL, 0);
    if (!lc.mkv) {
      log_line(TOOL_NAME ": cannot start mkv/mka mux");
      if (lc.out != STDOUT_FILENO)
        close(lc.out);
      tssrc_close(src);
      emmcache_free(lc.cache);
      device_state_free(lc.dev);
      return 1;
    }
  }
  lc.packets = 0;
  lc.ecm_pid = 0;
  lc.emm_pid = 0;
  lc.cas_logged = 0;
  lc.emm_file = cfg.emm_file;
  memset(&lc.ecm_asm, 0, sizeof lc.ecm_asm);
  memset(&lc.emm_asm, 0, sizeof lc.emm_asm);
  lc.scr = NULL;
  lc.cw_len = 0;
  lc.have_cw[0] = lc.have_cw[1] = 0;
  lc.emit_failed = 0;
  lc.psi = psi_new();
  if (!lc.psi) {
    if (lc.out != STDOUT_FILENO)
      close(lc.out);
    tssrc_close(src);
    emmcache_free(lc.cache);
    device_state_free(lc.dev);
    return 1;
  }
  if (pmt_pid)
    psi_select_pmt_pid(lc.psi, pmt_pid); /* CW derivation is mux-wide either way; nicer stats only */

  if (emmcache_load(lc.cache, lc.dev, cfg.emm_file) != 0)
    log_line(TOOL_NAME ": failed to read emm cache %s, continuing without it", cfg.emm_file);

  ipi = NULL;
  if (cfg.unicast_emm_uri) {
    ipi = ipiclient_new(cfg.unicast_emm_uri, cfg.insecure_tls);
    if (!ipi)
      log_line(TOOL_NAME ": invalid -u/--unicast-emm uri, ignoring: %s", cfg.unicast_emm_uri);
    else if (ipiclient_poll(ipi, lc.cache, lc.dev))
      emmcache_save(lc.cache, cfg.emm_file);
  }

  memset(&pz, 0, sizeof pz);
  signals_install();
  start = last_stat = mono_seconds();

  while (!signal_stop_requested()) {
    ssize_t n = tssrc_read(src, buf, sizeof buf, NULL);
    if (n < 0)
      break;
    if (n == 0)
      continue;
    if (tspack_feed(&pz, buf, (size_t)n, pkt_cb, &lc))
      break;
    if (cfg.verbose && mono_seconds() - last_stat >= 1.0) {
      log_line(TOOL_NAME ": %llu packets, %.0fs elapsed", lc.packets, mono_seconds() - start);
      last_stat = mono_seconds();
    }
  }

  scrambler_flush(lc.scr, emit_downstream, &lc);
  ipiclient_free(ipi);
  scrambler_free(lc.scr);
  psi_free(lc.psi);
  if (lc.mkv)
    mkv_close(lc.mkv);
  if (lc.out != STDOUT_FILENO)
    close(lc.out);
  tssrc_close(src);
  emmcache_free(lc.cache);
  device_state_free(lc.dev);
  return 0;
}
