/* Copyright 2026 dvbipitools authors. Licensed under GPL-3.0-or-later.
 * See NOTICE and LICENSE for details and authorship information. */

/* write starter seed per fuzz target into directory argv[1].
   not part of any normal build. run manually before afl-fuzz. */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "lib/cas/simulcrypt_msg.h"
#include "lib/bim/accessunit.h"
#include "lib/bim/bitwriter.h"
#include "lib/demux/rtcp.h"
#include "lib/mux/psi_build.h"
#include "lib/mux/rtcp_build.h"
#include "lib/net/dvbstp.h"
#include "lib/helper/sds_xml.h"
#include "lib/tva/bcg_doc.h"

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
  bcg_doc_t doc;
  bcg_channel_t *c;
  bcg_programme_t *pr;
  bitwriter_t bw;
  strrepo_writer_t sw;
  accessunit_scratch_t sc;
  const unsigned char *bits, *strs;
  size_t bits_len, strs_len;
  unsigned char *out;
  size_t cap, off;
  int nfuu = 0;

  bcg_doc_init(&doc);
  c = bcg_add_channel(&doc);
  snprintf(c->id, sizeof c->id, "channel1");
  snprintf(c->uri, sizeof c->uri, "rtp://239.1.1.1:5000");
  c->onid = 2;
  c->tsid = 1;
  c->sid = 101;
  bcg_channel_add_name(c, "Channel One");

  pr = bcg_add_programme(&doc);
  snprintf(pr->channel_id, sizeof pr->channel_id, "channel1");
  snprintf(pr->start, sizeof pr->start, "2020-12-15T12:00:00Z");
  snprintf(pr->stop, sizeof pr->stop, "2020-12-15T12:30:00Z");
  snprintf(pr->title, sizeof pr->title, "News");

  bitwriter_init(&bw);
  strrepo_writer_init(&sw);
  accessunit_scratch_init(&sc);
  if (accessunit_encode(&sc, &doc, &bw, &sw, &nfuu)) {
    accessunit_scratch_free(&sc);
    strrepo_writer_free(&sw);
    bitwriter_free(&bw);
    bcg_doc_free(&doc);
    return;
  }
  accessunit_scratch_free(&sc);

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
  bcg_doc_free(&doc);
}

static void gen_sds(const char *dir) {
  sds_service_t svc;
  unsigned char buf[4096];
  size_t n;

  memset(&svc, 0, sizeof svc);
  snprintf(svc.name, sizeof svc.name, "Channel One HD");
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

static void gen_simulcrypt_msg(const char *dir) {
  unsigned char buf[32];
  simulcrypt_writer_t w;
  static const unsigned char val[] = {0xAA, 0xBB, 0xCC, 0xDD};
  size_t n;

  simulcrypt_writer_begin(&w, buf, sizeof buf, 3, 0x0201 /* ECMG_MSG_CW_PROVISION */);
  simulcrypt_writer_put_tlv(&w, 0x0015 /* ECMG_P_ECM_DATAGRAM */, val, sizeof val);
  n = simulcrypt_writer_finish(&w);
  if (n)
    write_file(dir, "simulcrypt_msg_min.bin", buf, n);
}

static void gen_ecmg_channel_status(const char *dir) {
  /* ecmg_parse_channel_status() takes message BODY directly, no generic_message
     header. ECMG_P_CW_PER_MSG (tag 0x000B) is only required field */
  static const unsigned char body[] = {0x00, 0x0B, 0x00, 0x01, 0x01};
  write_file(dir, "ecmg_channel_status_min.bin", body, sizeof body);
}

static void gen_dvbstp(const char *dir) {
  /* single-section segment, no CRC, no provider id, no private words, per clause 5.4.1.3 */
  static const unsigned char payload[] = "hello";
  unsigned char pkt[12 + sizeof payload - 1];

  pkt[0] = 0x00; /* version 0, crc_present 0 */
  pkt[1] = 0x00; pkt[2] = 0x00; pkt[3] = 0x00; /* total_segment_size, informational */
  pkt[4] = DVBSTP_PAYLOAD_BROADCAST_DISCOVERY;
  pkt[5] = 0x00; pkt[6] = 0x01; /* segment_id */
  pkt[7] = 0x01; /* segment_version */
  pkt[8] = 0x00; pkt[9] = 0x00; /* section_number 0, last_section_number top nibble 0 */
  pkt[10] = 0x00; /* last_section_number low byte 0 */
  pkt[11] = 0x00; /* compr 0, has_provider_id 0, priv_words 0 */
  memcpy(pkt + 12, payload, sizeof payload - 1);

  write_file(dir, "dvbstp_min.bin", pkt, sizeof pkt);
}

static void gen_dvbstp_bcg_compressed(const char *dir) {
  /* BCG payload id with compr=1 (BiM/binary, TS 102 539 table 3). parse_header
     only rejects nonzero compr for payload ids 0x01/0x02, this path is reachable */
  static const unsigned char payload[] = "wrapped";
  unsigned char pkt[12 + sizeof payload - 1];

  pkt[0] = 0x00;
  pkt[1] = 0x00; pkt[2] = 0x00; pkt[3] = 0x00;
  pkt[4] = DVBSTP_PAYLOAD_BCG_DATA_CONTAINER;
  pkt[5] = 0x00; pkt[6] = 0x01;
  pkt[7] = 0x01;
  pkt[8] = 0x00; pkt[9] = 0x00;
  pkt[10] = 0x00;
  pkt[11] = 0x20; /* compr 1, has_provider_id 0, priv_words 0 */
  memcpy(pkt + 12, payload, sizeof payload - 1);

  write_file(dir, "dvbstp_bcg_compr_min.bin", pkt, sizeof pkt);
}

static void gen_emmg_datagrams(const char *dir) {
  /* emmg_extract_datagrams() takes data_provision BODY directly: one EMMG_P_DATAGRAM (tag 0x0005) TLV */
  static const unsigned char body[] = {0x00, 0x05, 0x00, 0x03, 0xAA, 0xBB, 0xCC};
  write_file(dir, "emmg_datagrams_min.bin", body, sizeof body);
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
  gen_simulcrypt_msg(argv[1]);
  gen_ecmg_channel_status(argv[1]);
  gen_emmg_datagrams(argv[1]);
  gen_dvbstp(argv[1]);
  gen_dvbstp_bcg_compressed(argv[1]);
  return 0;
}
