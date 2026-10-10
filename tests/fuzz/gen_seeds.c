/* Copyright 2026 dvbipitools authors. Licensed under GPL-3.0-or-later.
 * See NOTICE and LICENSE for details and authorship information. */

/* write starter seed per fuzz target into directory argv[1].
   not part of any normal build. run manually before afl-fuzz. */

#define _GNU_SOURCE

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
#include "lib/sys/ioutil.h"
#include "lib/metrics/protocol.h"
#include "lib/tva/tva_xml.h"
#include "dipibcg/container.h"
#include "dipibcg/wrapper.h"

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
  bufcpy(c->id, sizeof c->id, "channel1");
  bufcpy(c->uri, sizeof c->uri, "rtp://239.1.1.1:5000");
  c->onid = 2;
  c->tsid = 1;
  c->sid = 101;
  bcg_channel_add_name(c, "Channel One");

  pr = bcg_add_programme(&doc);
  bufcpy(pr->channel_id, sizeof pr->channel_id, "channel1");
  bufcpy(pr->start, sizeof pr->start, "2020-12-15T12:00:00Z");
  bufcpy(pr->stop, sizeof pr->stop, "2020-12-15T12:30:00Z");
  bufcpy(pr->title, sizeof pr->title, "News");

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
  bufcpy(svc.name, sizeof svc.name, "Channel One HD");
  bufcpy(svc.address, sizeof svc.address, "239.1.1.1");
  svc.family = 2; /* AF_INET */
  svc.port = 5000;
  svc.rtp = 1;
  svc.tsid = 1;
  svc.onid = 2;
  svc.sid = 101;

  n = sds_build_broadcast("dvb-ip.example", 1, &svc, 1, NULL, NULL, NULL, buf, sizeof buf);
  if (n) write_file(dir, "sds_min.xml", buf, n);
}

static void gen_rtcp(const char *dir) {
  rtcp_nack_entry_t entry;
  unsigned char buf[64];
  size_t n;

  entry.pid = 100;
  entry.blp = 0;
  n = rtcp_build_ff(0x11111111u, 0x22222222u, &entry, 1, buf, sizeof buf);
  if (n) write_file(dir, "rtcp_nack.bin", buf, n);
}

static void gen_rtcp_sdes(const char *dir) {
  static const unsigned char sdes[] = {
    0x81, 202, 0x00, 0x03,
    0x11, 0x22, 0x33, 0x44,
    0x01, 0x04, 'h', 'o', 's', 't', 0x00, 0x00
  };

  write_file(dir, "rtcp_sdes.bin", sdes, sizeof sdes);
}

static void gen_rtcp_rams_i(const char *dir) {
  rtcp_rams_i_tlvs_t tlvs;
  unsigned char buf[96];
  size_t n;

  memset(&tlvs, 0, sizeof tlvs);
  tlvs.has_media_ssrc_tlv = 1;
  tlvs.media_ssrc_tlv = 0x33333333u;
  tlvs.has_first_packet_seqnum = 1;
  tlvs.first_packet_seqnum = 1000;
  tlvs.has_earliest_join_time = 1;
  tlvs.has_burst_duration = 1;
  tlvs.burst_duration_ms = 8000;
  tlvs.has_max_transmit_bitrate = 1;
  tlvs.max_transmit_bitrate_bps = 20000000;
  n = rtcp_build_rams_i(0x11111111u, 0x22222222u, 1, 200, &tlvs, buf, sizeof buf);
  if (n) write_file(dir, "rtcp_rams_i.bin", buf, n);
}

static void gen_simulcrypt_msg(const char *dir) {
  unsigned char buf[32];
  simulcrypt_writer_t w;
  static const unsigned char val[] = {0xAA, 0xBB, 0xCC, 0xDD};
  size_t n;

  simulcrypt_writer_begin(&w, buf, sizeof buf, 3, 0x0201 /* ECMG_MSG_CW_PROVISION */);
  simulcrypt_writer_put_tlv(&w, 0x0015 /* ECMG_P_ECM_DATAGRAM */, val, sizeof val);
  n = simulcrypt_writer_finish(&w);
  if (n) write_file(dir, "simulcrypt_msg_min.bin", buf, n);
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
  pkt[1] = 0x00; pkt[2] = 0x00; pkt[3] = 0x05; /* total_segment_size */
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
  pkt[1] = 0x00; pkt[2] = 0x00; pkt[3] = 0x07;
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

static void gen_yamlcfg(const char *dir) {
  /* first byte is load mode: 0 normal, 1 check, 2 strict, 3 both */
  static const char scalars[] = "\0name: hello\non: yes\ncount: 42\nnet:\n  port: 8080\nfile: /dev/null\n";
  static const char lists[] = "\1list:\n  - a\n  - b\n  -\nitems:\n  - first:\n      opt: 5\n  - second\n";
  static const char broken[] = "\2name: &a x\nbogus: *a\ncount: 999\ncount: 1\n---\nlist: [a, [b]]\n";
  static const char flow[] = "\3{name: \"q: [1]\", list: [x, y], net: {port: 1}}\n";
  write_file(dir, "yamlcfg_scalars.yaml", (const unsigned char *)scalars, sizeof scalars - 1);
  write_file(dir, "yamlcfg_lists.yaml", (const unsigned char *)lists, sizeof lists - 1);
  write_file(dir, "yamlcfg_broken.yaml", (const unsigned char *)broken, sizeof broken - 1);
  write_file(dir, "yamlcfg_flow.yaml", (const unsigned char *)flow, sizeof flow - 1);
}

static void gen_tva_xml(const char *dir) {
  bcg_doc_t doc;
  bcg_channel_t *c;
  bcg_programme_t *pr;
  char *text = NULL;
  size_t len = 0;
  FILE *f;

  bcg_doc_init(&doc);
  c = bcg_add_channel(&doc);
  bufcpy(c->id, sizeof c->id, "ch1");
  bufcpy(c->uri, sizeof c->uri, "rtp://239.1.1.1:5000");
  bufcpy(c->names[0], sizeof c->names[0], "Channel One");
  c->name_count = 1;
  c->tsid = 1;
  c->onid = 2;
  c->sid = 101;
  pr = bcg_add_programme(&doc);
  bufcpy(pr->channel_id, sizeof pr->channel_id, "ch1");
  bufcpy(pr->start, sizeof pr->start, "2030-01-01T12:00:00Z");
  bufcpy(pr->stop, sizeof pr->stop, "2030-01-01T13:00:00Z");
  bufcpy(pr->title, sizeof pr->title, "News");
  f = open_memstream(&text, &len);
  if (f) {
    tva_xml_write(f, &doc);
    fclose(f);
    if (len) write_file(dir, "tva_min.xml", (const unsigned char *)text, len);
    free(text);
  }
  bcg_doc_free(&doc);
}

static void gen_xmltv_and_mapping(const char *dir) {
  static const char xmltv[] =
    "\0<?xml version=\"1.0\"?>\n<tv><channel id=\"c1\"><display-name>One</display-name></channel>"
    "<programme start=\"20300101120000 +0000\" stop=\"20300101130000 +0000\" channel=\"c1\"><title>News</title>"
    "<desc>d</desc><category>x</category></programme></tv>\n";
  static const char mapping[] = "\1c1,rtp://239.1.1.1:5000,1,2,101\n# comment\nc2,udp://239.1.1.2:5000,3,4,5\n";

  write_file(dir, "xmltv_min.bin", (const unsigned char *)xmltv, sizeof xmltv - 1);
  write_file(dir, "mapping_min.bin", (const unsigned char *)mapping, sizeof mapping - 1);
}

static void gen_bcg_container(const char *dir) {
  static const unsigned char au[] = {1, 2, 3, 4};
  static const unsigned char sr[] = {0, 0};
  unsigned char *cont = NULL;
  size_t cont_len = 0;
  unsigned char *wrapped = NULL;
  size_t wrapped_len = 0;
  unsigned char *seed;

  if (container_build(au, sizeof au, sr, sizeof sr, &cont, &cont_len)) return;
  seed = malloc(cont_len + 1);
  if (seed) {
    seed[0] = 0;
    memcpy(seed + 1, cont, cont_len);
    write_file(dir, "bcg_container_min.bin", seed, cont_len + 1);
    free(seed);
  }
  if (wrapper_build(cont, cont_len, 0, &wrapped, &wrapped_len) == 0) {
    seed = malloc(wrapped_len + 1);
    if (seed) {
      seed[0] = 1;
      memcpy(seed + 1, wrapped, wrapped_len);
      write_file(dir, "bcg_wrapper_min.bin", seed, wrapped_len + 1);
      free(seed);
    }
    free(wrapped);
  }
  free(cont);
}

static void gen_metrics_store(const char *dir) {
  static metrics_writer_t w;
  metrics_hdr_t hdr;
  size_t n;

  memset(&hdr, 0, sizeof hdr);
  hdr.proto_version = METRICS_PROTO_VERSION;
  hdr.component = METRICS_COMPONENT_SDS;
  bufcpy(hdr.metrics_id, sizeof hdr.metrics_id, "sds1");
  hdr.process_start_time = 1000;
  hdr.sequence = 1;
  hdr.snapshot_time = 2000;
  if (metrics_writer_begin(&w, &hdr)) return;
  metrics_writer_put(&w, METRICS_ID_HEADEND_INFO, "1.0", 1);
  metrics_writer_put(&w, METRICS_ID_SDS_SERVICES, NULL, 3);
  metrics_writer_put(&w, METRICS_ID_SDS_ANNOUNCEMENTS_TOTAL, "multicast", 7);
  n = metrics_writer_finish(&w);
  if (n) write_file(dir, "metrics_snapshot_min.bin", w.buf, n);
}

static void gen_rtmp(const char *dir) {
  static const unsigned char amf[] = {0x00, 0x03, 0x00, 0x04, 'c', 'o', 'd', 'e', 0x02, 0x00, 0x04, 't', 'e', 's', 't', 0x00, 0x00, 0x09};
  static const unsigned char chunk[] = {0x01, 0x03, 0x00, 0x00, 0x00, 0x00, 0x00, 0x14, 0x14, 0x00, 0x00, 0x00, 0x00,
                                         0x02, 0x00, 0x07, '_', 'r', 'e', 's', 'u', 'l', 't',
                                         0x00, 0x3F, 0xF0, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x05};

  write_file(dir, "amf_object_min.bin", amf, sizeof amf);
  write_file(dir, "rtmp_result_min.bin", chunk, sizeof chunk);
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
  gen_rtcp_sdes(argv[1]);
  gen_rtcp_rams_i(argv[1]);
  gen_simulcrypt_msg(argv[1]);
  gen_ecmg_channel_status(argv[1]);
  gen_emmg_datagrams(argv[1]);
  gen_dvbstp(argv[1]);
  gen_dvbstp_bcg_compressed(argv[1]);
  gen_yamlcfg(argv[1]);
  gen_tva_xml(argv[1]);
  gen_xmltv_and_mapping(argv[1]);
  gen_bcg_container(argv[1]);
  gen_metrics_store(argv[1]);
  gen_rtmp(argv[1]);
  return 0;
}
