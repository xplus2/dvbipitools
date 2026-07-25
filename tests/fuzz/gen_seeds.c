/* Copyright 2026 dvbipitools authors. Licensed under GPL-3.0-or-later.
 * See NOTICE and LICENSE for details and authorship information. */

/* writes one valid starter seed per fuzz target into directory argv[1].
 * not part of any normal build; run manually before afl-fuzz. */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "lib/bim/accessunit.h"
#include "lib/bim/bitwriter.h"
#include "lib/demux/rtcp.h"
#include "lib/mux/psi_build.h"
#include "lib/mux/rtcp_build.h"
#include "lib/sds_xml.h"
#include "lib/tva/epg_doc.h"

static int write_file(const char *dir, const char *name, const unsigned char *data, size_t len) {
  char path[512];
  FILE *f;
  snprintf(path, sizeof path, "%s/%s", dir, name);
  f = fopen(path, "wb");
  if (!f) {
    fprintf(stderr, "cannot open %s for writing\n", path);
    return -1;
  }
  fwrite(data, 1, len, f);
  fclose(f);
  fprintf(stderr, "wrote %s (%zu bytes)\n", path, len);
  return 0;
}

static void gen_psi(const char *dir) {
  unsigned char sec[188], pkt[188];
  size_t seclen;

  seclen = psi_build_pat(1, 0, 1, 0x0100, sec, sizeof sec);
  if (!seclen)
    return;

  memset(pkt, 0xFF, sizeof pkt);
  pkt[0] = 0x47;
  pkt[1] = 0x40;
  pkt[2] = 0x00;
  pkt[3] = 0x10;
  pkt[4] = 0x00;
  memcpy(pkt + 5, sec, seclen);
  write_file(dir, "psi_pat.bin", pkt, sizeof pkt);
}

static void gen_bim(const char *dir) {
  epg_doc_t doc;
  epg_channel_t *c;
  epg_programme_t *pr;
  bitwriter_t bw;
  strrepo_writer_t sw;
  const unsigned char *bits, *strs;
  size_t bits_len, strs_len;
  unsigned char *out;
  size_t cap, off;
  int nfuu = 0;

  epg_doc_init(&doc);
  c = epg_add_channel(&doc);
  snprintf(c->id, sizeof c->id, "orf1");
  snprintf(c->uri, sizeof c->uri, "rtp://239.1.1.1:5000");
  c->onid = 2;
  c->tsid = 1;
  c->sid = 101;
  epg_channel_add_name(c, "ORFeins");

  pr = epg_add_programme(&doc);
  snprintf(pr->channel_id, sizeof pr->channel_id, "orf1");
  snprintf(pr->start, sizeof pr->start, "2020-12-15T12:00:00Z");
  snprintf(pr->stop, sizeof pr->stop, "2020-12-15T12:30:00Z");
  snprintf(pr->title, sizeof pr->title, "News");

  bitwriter_init(&bw);
  strrepo_writer_init(&sw);
  if (accessunit_encode(&doc, &bw, &sw, &nfuu)) {
    strrepo_writer_free(&sw);
    bitwriter_free(&bw);
    epg_doc_free(&doc);
    return;
  }

  bits = bitwriter_data(&bw, &bits_len);
  strs = strrepo_writer_data(&sw, &strs_len);
  cap = 4 + bits_len + strs_len;
  out = malloc(cap);
  if (out) {
    out[0] = (unsigned char)(bits_len >> 24);
    out[1] = (unsigned char)(bits_len >> 16);
    out[2] = (unsigned char)(bits_len >> 8);
    out[3] = (unsigned char)bits_len;
    off = 4;
    memcpy(out + off, bits, bits_len);
    off += bits_len;
    memcpy(out + off, strs, strs_len);
    write_file(dir, "bim_min.bin", out, cap);
    free(out);
  }

  strrepo_writer_free(&sw);
  bitwriter_free(&bw);
  epg_doc_free(&doc);
}

static void gen_sds(const char *dir) {
  sds_service_t svc;
  unsigned char buf[4096];
  size_t n;

  memset(&svc, 0, sizeof svc);
  snprintf(svc.name, sizeof svc.name, "ORF 1 HD");
  snprintf(svc.address, sizeof svc.address, "239.1.1.1");
  svc.family = 2; /* AF_INET */
  svc.port = 5000;
  svc.rtp = 1;
  svc.tsid = 1;
  svc.onid = 2;
  svc.sid = 101;

  n = sds_build_broadcast("dvb-ip.example", 1, &svc, 1, NULL, NULL, buf, sizeof buf);
  if (n)
    write_file(dir, "sds_min.xml", buf, n);
}

static void gen_rtcp(const char *dir) {
  rtcp_nack_entry_t entry;
  unsigned char buf[64];
  size_t n;

  entry.pid = 100;
  entry.blp = 0;
  n = rtcp_build_ff(0x11111111u, 0x22222222u, &entry, 1, buf, sizeof buf);
  if (n)
    write_file(dir, "rtcp_nack.bin", buf, n);
}

int main(int argc, char **argv) {
  if (argc != 2) {
    fprintf(stderr, "usage: %s <output-dir>\n", argv[0]);
    return 1;
  }
  gen_psi(argv[1]);
  gen_bim(argv[1]);
  gen_sds(argv[1]);
  gen_rtcp(argv[1]);
  return 0;
}
