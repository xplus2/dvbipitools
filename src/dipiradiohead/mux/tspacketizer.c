/* Copyright 2026 dvbipitools authors. Licensed under GPL-3.0-or-later.
 * See NOTICE and LICENSE for details and authorship information. */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "pes.h"
#include "psi.h"
#include "tspacketizer.h"

#define PID_PAT 0x0000
#define PID_NIT 0x0010
#define PID_SDT 0x0011
#define PID_EIT 0x0012
#define PID_PMT 0x0100
#define PID_AUDIO 0x0101

#define INTERVAL_PAT_PMT 9000UL  /* 100ms @ 90kHz */
#define INTERVAL_SDT 180000UL    /* 2s */
#define INTERVAL_NIT 900000UL    /* 10s */
#define INTERVAL_EIT 90000UL     /* 1s */
#define EIT_DURATION_S 180       /* nominal placeholder, real remaining time is unknown */

struct tspacketizer {
  tspacketizer_cfg_t cfg;
  unsigned char cc_pat, cc_pmt, cc_sdt, cc_nit, cc_eit, cc_audio;
  unsigned ver_pat, ver_pmt, ver_sdt, ver_nit, ver_eit;
  char artist[256], title[256];
  int meta_changed;
  uint64_t last_pat, last_sdt, last_nit, last_eit;
};

tspacketizer_t *tspacketizer_new(const tspacketizer_cfg_t *cfg) {
  tspacketizer_t *t = calloc(1, sizeof *t);
  if (!t)
    return NULL;
  t->cfg = *cfg;
  t->last_pat = t->last_sdt = t->last_nit = t->last_eit = UINT64_MAX;
  return t;
}

void tspacketizer_free(tspacketizer_t *t) { free(t); }

void tspacketizer_set_metadata(tspacketizer_t *t, const char *artist, const char *title) {
  snprintf(t->artist, sizeof t->artist, "%s", artist);
  snprintf(t->title, sizeof t->title, "%s", title);
  t->meta_changed = 1;
}

static int due(uint64_t now, uint64_t *last, uint64_t interval) {
  if (*last == UINT64_MAX || now - *last >= interval) {
    *last = now;
    return 1;
  }
  return 0;
}

static void put_pcr(unsigned char *p, uint64_t base33, unsigned ext9) {
  uint64_t b = base33 & 0x1FFFFFFFFULL;
  p[0] = (unsigned char)(b >> 25);
  p[1] = (unsigned char)(b >> 17);
  p[2] = (unsigned char)(b >> 9);
  p[3] = (unsigned char)(b >> 1);
  p[4] = (unsigned char)(((b & 1) << 7) | 0x7E | ((ext9 >> 8) & 1));
  p[5] = (unsigned char)ext9;
}

static void write_packet(unsigned pid, unsigned char *cc, int pusi, const unsigned char *pointer_byte, const unsigned char *payload, size_t payload_len, int with_pcr, uint64_t pcr_90k, size_t pad, ts_packet_cb cb, void *ctx) {
  unsigned char pkt[188];
  size_t pos;

  pkt[0] = 0x47;
  pkt[1] = (unsigned char)((pusi ? 0x40 : 0x00) | ((pid >> 8) & 0x1F));
  pkt[2] = (unsigned char)pid;
  *cc = (unsigned char)((*cc + 1) & 0x0F);
  if (with_pcr) {
    unsigned af_len = (unsigned)(7 + pad);
    pkt[3] = (unsigned char)(0x30 | *cc);
    pkt[4] = (unsigned char)af_len;
    pkt[5] = 0x10; /* PCR_flag only */
    put_pcr(pkt + 6, pcr_90k, 0);
    pos = 12;
    memset(pkt + pos, 0xFF, pad);
    pos += pad;
  } else if (pad == 1) {
    pkt[3] = (unsigned char)(0x30 | *cc);
    pkt[4] = 0x00;
    pos = 5;
  } else if (pad > 1) {
    unsigned af_len = (unsigned)(pad - 1);
    pkt[3] = (unsigned char)(0x30 | *cc);
    pkt[4] = (unsigned char)af_len;
    pkt[5] = 0x00;
    memset(pkt + 6, 0xFF, af_len - 1);
    pos = 5 + af_len;
  } else {
    pkt[3] = (unsigned char)(0x10 | *cc);
    pos = 4;
  }

  if (pointer_byte)
    pkt[pos++] = *pointer_byte;
  memcpy(pkt + pos, payload, payload_len);
  cb(ctx, pkt);
}

static size_t emit_stream(unsigned pid, unsigned char *cc, const unsigned char *pointer_byte, const unsigned char *data, size_t len, int pcr_first, uint64_t pcr_90k, ts_packet_cb cb, void *ctx) {
  size_t sent = 0, count = 0;
  int first = 1;
  while (first || sent < len) {
    size_t remaining = len - sent;
    size_t ptr_overhead = (first && pointer_byte) ? 1 : 0;
    int with_pcr = first && pcr_first;
    size_t af_fixed = with_pcr ? 8 : 0;
    size_t space = 184 - ptr_overhead - af_fixed;
    size_t take = remaining < space ? remaining : space;
    size_t pad = space - take;
    write_packet(pid, cc, first, first ? pointer_byte : NULL, data + sent, take, with_pcr, pcr_90k, pad, cb, ctx);
    sent += take;
    first = 0;
    count++;
  }
  return count;
}

size_t tspacketizer_feed(tspacketizer_t *t, uint64_t pts_90k, const unsigned char *frame, size_t frame_len, ts_packet_cb cb, void *ctx) {
  unsigned char sec[4096], pesbuf[8192];
  unsigned char ptr0 = 0x00;
  size_t n, count = 0;
  int timer_due, meta_due;

  if (due(pts_90k, &t->last_pat, INTERVAL_PAT_PMT)) {
    n = psi_build_pat(t->cfg.tsid, t->ver_pat, t->cfg.sid, PID_PMT, sec, sizeof sec);
    if (n)
      count += emit_stream(PID_PAT, &t->cc_pat, &ptr0, sec, n, 0, 0, cb, ctx);
    n = psi_build_pmt(t->ver_pmt, t->cfg.sid, PID_AUDIO, t->cfg.stream_type, PID_AUDIO, sec, sizeof sec);
    if (n)
      count += emit_stream(PID_PMT, &t->cc_pmt, &ptr0, sec, n, 0, 0, cb, ctx);
  }
  if (due(pts_90k, &t->last_sdt, INTERVAL_SDT)) {
    n = psi_build_sdt(t->ver_sdt, t->cfg.tsid, t->cfg.onid, t->cfg.sid, "dipiradiohead", t->cfg.service_name, sec, sizeof sec);
    if (n)
      count += emit_stream(PID_SDT, &t->cc_sdt, &ptr0, sec, n, 0, 0, cb, ctx);
  }
  if (t->cfg.network_name[0] && due(pts_90k, &t->last_nit, INTERVAL_NIT)) {
    n = psi_build_nit(t->ver_nit, t->cfg.onid, t->cfg.tsid, t->cfg.network_name, sec, sizeof sec);
    if (n)
      count += emit_stream(PID_NIT, &t->cc_nit, &ptr0, sec, n, 0, 0, cb, ctx);
  }

  timer_due = due(pts_90k, &t->last_eit, INTERVAL_EIT);
  meta_due = t->meta_changed || timer_due;
  if (meta_due) {
    if (t->meta_changed) {
      t->ver_eit = (t->ver_eit + 1) & 0x1F;
      t->meta_changed = 0;
    }
    n = psi_build_eit(t->ver_eit, t->cfg.sid, t->cfg.tsid, t->cfg.onid, t->artist, t->title, EIT_DURATION_S, sec, sizeof sec);
    if (n)
      count += emit_stream(PID_EIT, &t->cc_eit, &ptr0, sec, n, 0, 0, cb, ctx);
  }
  n = pes_build(pts_90k, frame, frame_len, pesbuf, sizeof pesbuf);
  if (n)
    count += emit_stream(PID_AUDIO, &t->cc_audio, NULL, pesbuf, n, 1, pts_90k, cb, ctx);

  return count;
}
