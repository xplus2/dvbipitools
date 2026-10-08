/* Copyright 2026 dvbipitools authors. Licensed under GPL-3.0-or-later.
 * See NOTICE and LICENSE for details and authorship information. */

#include "cas_fixture.h"

#include <fcntl.h>
#include <poll.h>
#include <stdint.h>
#include <sys/stat.h>

#include "../log_capture.h"
#include "../metrics_sink.h"
#include "dipidescramble/pipeline.h"
#include "lib/demux/crc32.h"
#include "lib/scrambler/scrambler.h"
#include "lib/sys/signal.h"

#define PMT_PID 0x1000
#define ECM_PID 0x0064
#define EMM_PID 0x0065

static void wrap_section_packet(unsigned char pkt[188], unsigned pid, const unsigned char *section, size_t slen) {
  pkt[0] = 0x47;
  pkt[1] = (unsigned char)(0x40 | ((pid >> 8) & 0x1F));
  pkt[2] = (unsigned char)pid;
  pkt[3] = 0x10;
  pkt[4] = 0x00;
  memcpy(pkt + 5, section, slen);
  for (size_t i = 5 + slen; i < 188; i++)
    pkt[i] = 0xFF;
}

static size_t build_pat(unsigned char *out, unsigned prog_num, unsigned pmt_pid) {
  unsigned char body[16];
  size_t n = 0, hdr, crc_at;
  uint32_t crc;

  body[n++] = 0x12;
  body[n++] = 0x34;
  body[n++] = 0xC1;
  body[n++] = 0x00;
  body[n++] = 0x00;
  body[n++] = (unsigned char)(prog_num >> 8);
  body[n++] = (unsigned char)prog_num;
  body[n++] = (unsigned char)(0xE0 | ((pmt_pid >> 8) & 0x1F));
  body[n++] = (unsigned char)pmt_pid;

  hdr = n + 4;
  out[0] = 0x00;
  out[1] = 0xB0;
  out[2] = (unsigned char)hdr;
  memcpy(out + 3, body, n);

  crc_at = 3 + n;
  crc = crc32_mpeg(out, crc_at);
  out[crc_at + 0] = (unsigned char)(crc >> 24);
  out[crc_at + 1] = (unsigned char)(crc >> 16);
  out[crc_at + 2] = (unsigned char)(crc >> 8);
  out[crc_at + 3] = (unsigned char)crc;
  return crc_at + 4;
}

/* one-program, zero-ES PMT with a CA_descriptor (tag 0x09) in program_info, CRC included */
static size_t build_pmt_with_ca(unsigned char *out, unsigned prog_num, unsigned ca_system_id, unsigned ca_pid, unsigned scrambling_mode) {
  unsigned char body[40];
  size_t n = 0, hdr, crc_at;
  uint32_t crc;

  body[n++] = (unsigned char)(prog_num >> 8);
  body[n++] = (unsigned char)prog_num;
  body[n++] = 0xC1;
  body[n++] = 0x00;
  body[n++] = 0x00;
  body[n++] = 0xE1; /* pcr_pid = 0x0101 */
  body[n++] = 0x01;
  body[n++] = 0xF0;
  body[n++] = scrambling_mode ? 0x09 : 0x06;
  body[n++] = 0x09; /* CA_descriptor tag */
  body[n++] = 0x04;
  body[n++] = (unsigned char)(ca_system_id >> 8);
  body[n++] = (unsigned char)ca_system_id;
  body[n++] = (unsigned char)(0xE0 | ((ca_pid >> 8) & 0x1F));
  body[n++] = (unsigned char)ca_pid;
  if (scrambling_mode) {
    body[n++] = 0x65;
    body[n++] = 0x01;
    body[n++] = (unsigned char)scrambling_mode;
  }

  hdr = n + 4;
  out[0] = 0x02;
  out[1] = (unsigned char)(0xB0 | ((hdr >> 8) & 0x0F));
  out[2] = (unsigned char)hdr;
  memcpy(out + 3, body, n);

  crc_at = 3 + n;
  crc = crc32_mpeg(out, crc_at);
  out[crc_at + 0] = (unsigned char)(crc >> 24);
  out[crc_at + 1] = (unsigned char)(crc >> 16);
  out[crc_at + 2] = (unsigned char)(crc >> 8);
  out[crc_at + 3] = (unsigned char)crc;
  return crc_at + 4;
}

/* CAT (table_id 0x01) with one CA_descriptor, CRC included */
static size_t build_cat(unsigned char *out, unsigned ca_system_id, unsigned emm_pid) {
  unsigned char body[16];
  size_t n = 0, hdr, crc_at;
  uint32_t crc;

  body[n++] = 0xFF;
  body[n++] = 0xFF;
  body[n++] = 0xC1;
  body[n++] = 0x00;
  body[n++] = 0x00;
  body[n++] = 0x09;
  body[n++] = 0x04;
  body[n++] = (unsigned char)(ca_system_id >> 8);
  body[n++] = (unsigned char)ca_system_id;
  body[n++] = (unsigned char)(0xE0 | ((emm_pid >> 8) & 0x1F));
  body[n++] = (unsigned char)emm_pid;

  hdr = n + 4;
  out[0] = 0x01;
  out[1] = 0xB0;
  out[2] = (unsigned char)hdr;
  memcpy(out + 3, body, n);

  crc_at = 3 + n;
  crc = crc32_mpeg(out, crc_at);
  out[crc_at + 0] = (unsigned char)(crc >> 24);
  out[crc_at + 1] = (unsigned char)(crc >> 16);
  out[crc_at + 2] = (unsigned char)(crc >> 8);
  out[crc_at + 3] = (unsigned char)crc;
  return crc_at + 4;
}

/* minimal section frame (table_id + 12-bit length, no CRC): psi_section_asm_feed only
   frames by declared length, doesn't check CRC. handle_ecm_section/emmcache_feed don't
   either - the fixed dispatch is gated on lc->dev, not on section well-formedness. */
static size_t build_bare_section(unsigned char *out, unsigned char table_id, size_t payload_len) {
  size_t n = 0;
  out[n++] = table_id;
  out[n++] = (unsigned char)(0x70 | ((payload_len >> 8) & 0x0F));
  out[n++] = (unsigned char)payload_len;
  memset(out + n, 0, payload_len);
  n += payload_len;
  return n;
}

static loop_ctx_t g_lc;
static config_t g_cfg;
static int g_devnull;

static void setup_biss1e(void) {
  memset(&g_lc, 0, sizeof g_lc);
  memset(&g_cfg, 0, sizeof g_cfg);
  g_cfg.biss2_sw_given = 1;
  memset(g_cfg.biss2_sw, 0xAB, sizeof g_cfg.biss2_sw);

  g_lc.cfg = &g_cfg;
  g_lc.psi = psi_new();
  ck_assert_ptr_nonnull(g_lc.psi);
  g_devnull = open("/dev/null", O_WRONLY);
  ck_assert_int_ge(g_devnull, 0);
  g_lc.outfd[0] = g_devnull;
  g_lc.n_outfd = 1;
}

static void teardown(void) {
  close(g_devnull);
  psi_free(g_lc.psi);
  ipiclient_free(g_lc.ipi);
  device_state_free(g_lc.dev);
  biss_ca_state_free(g_lc.biss_ca);
}

START_TEST(emit_downstream_writes_identical_bytes_to_every_outfd) {
  loop_ctx_t lc;
  config_t cfg;
  char path0[] = "/tmp/dipidescramble_test_outA_XXXXXX";
  char path1[] = "/tmp/dipidescramble_test_outB_XXXXXX";
  int fd0 = mkstemp(path0);
  int fd1 = mkstemp(path1);
  unsigned char pkt[188];
  unsigned char rd0[188 * 3], rd1[188 * 3];
  ssize_t n0, n1;

  ck_assert_int_ge(fd0, 0);
  ck_assert_int_ge(fd1, 0);
  memset(&lc, 0, sizeof lc);
  memset(&cfg, 0, sizeof cfg);
  lc.cfg = &cfg;
  lc.psi = psi_new();
  ck_assert_ptr_nonnull(lc.psi);
  lc.outfd[0] = fd0;
  lc.outfd[1] = fd1;
  lc.n_outfd = 2;

  for (int i = 0; i < 3; i++) {
    memset(pkt, (unsigned char)(0x10 + i), sizeof pkt);
    pkt[0] = 0x47;
    pkt[1] = 0x1F;
    pkt[2] = 0xFF;
    ck_assert_int_eq(pkt_cb(&lc, pkt), 0);
  }
  pipeline_flush(&lc);

  n0 = pread(fd0, rd0, sizeof rd0, 0);
  n1 = pread(fd1, rd1, sizeof rd1, 0);
  ck_assert_int_eq((int)n0, 188 * 3);
  ck_assert_int_eq((int)n1, 188 * 3);
  ck_assert_mem_eq(rd0, rd1, sizeof rd0);

  close(fd0);
  close(fd1);
  unlink(path0);
  unlink(path1);
  psi_free(lc.psi);
}
END_TEST

START_TEST(biss1e_ecm_on_null_dev_does_not_crash) {
  unsigned char pkt[188], sec[64];
  size_t slen;

  setup_biss1e();

  slen = build_pat(sec, 1, PMT_PID);
  wrap_section_packet(pkt, 0x0000, sec, slen);
  ck_assert_int_eq(pkt_cb(&g_lc, pkt), 0);

  slen = build_pmt_with_ca(sec, 1, 0x2602, ECM_PID, 0);
  wrap_section_packet(pkt, PMT_PID, sec, slen);
  ck_assert_int_eq(pkt_cb(&g_lc, pkt), 0);

  ck_assert_ptr_nonnull(g_lc.scr); /* BISS 1/E resolved */
  ck_assert_ptr_null(g_lc.biss_ca);
  ck_assert_ptr_null(g_lc.dev); /* never set on this path - the crash's precondition */

  slen = build_bare_section(sec, 0x80, 24);
  wrap_section_packet(pkt, ECM_PID, sec, slen);
  ck_assert_int_eq(pkt_cb(&g_lc, pkt), 0);

  ck_assert_uint_eq(g_lc.ecm_pid, ECM_PID);
  ck_assert_int_eq(g_lc.fatal, 0);

  teardown();
}
END_TEST

START_TEST(biss1e_emm_on_null_dev_does_not_crash) {
  unsigned char pkt[188];
  unsigned char sec[64];
  size_t slen;
  setup_biss1e();
  slen = build_pat(sec, 1, PMT_PID);
  wrap_section_packet(pkt, 0x0000, sec, slen);
  ck_assert_int_eq(pkt_cb(&g_lc, pkt), 0);

  slen = build_pmt_with_ca(sec, 1, 0x2602, ECM_PID, 0);
  wrap_section_packet(pkt, PMT_PID, sec, slen);
  ck_assert_int_eq(pkt_cb(&g_lc, pkt), 0);

  slen = build_cat(sec, 0x0B75, EMM_PID); /* unrelated ca_system_id, just needs a CAT present */
  wrap_section_packet(pkt, 0x0001, sec, slen);
  ck_assert_int_eq(pkt_cb(&g_lc, pkt), 0);

  ck_assert_ptr_nonnull(g_lc.scr);
  ck_assert_ptr_null(g_lc.dev);
  ck_assert_uint_eq(g_lc.emm_pid, EMM_PID);

  slen = build_bare_section(sec, 0x82, 16);
  wrap_section_packet(pkt, EMM_PID, sec, slen);
  ck_assert_int_eq(pkt_cb(&g_lc, pkt), 0);

  ck_assert_int_eq(g_lc.fatal, 0);

  teardown();
}
END_TEST

#define VIDEO_PID 0x0100
#define CAS_ID 0x0B75
#define MODE_CISSA 0x10
#define KEY_TEMPLATE "/tmp/dipidescramble_pipe_key_XXXXXX"
#define EMM_TEMPLATE "/tmp/dipidescramble_pipe_emm_XXXXXX"
#define OUT_TEMPLATE "/tmp/dipidescramble_pipe_out_XXXXXX"

typedef struct {
  loop_ctx_t lc;
  config_t cfg;
  EVP_PKEY *key;
  char keypath[64];
  char emmpath[64];
  char outpath[64];
  int outfd;
} cls_t;

static void cls_open(cls_t *c) {
  int fd;

  memset(c, 0, sizeof *c);
  c->key = make_rsa_key();
  strcpy(c->keypath, KEY_TEMPLATE);
  write_key_pem(c->key, c->keypath);
  strcpy(c->emmpath, EMM_TEMPLATE);
  fd = mkstemp(c->emmpath);
  ck_assert_int_ge(fd, 0);
  close(fd);
  unlink(c->emmpath);
  strcpy(c->outpath, OUT_TEMPLATE);
  c->outfd = mkstemp(c->outpath);
  ck_assert_int_ge(c->outfd, 0);

  c->cfg.key_path = c->keypath;
  c->cfg.serial = TEST_SERIAL;
  c->cfg.emm_file = c->emmpath;
  c->lc.cfg = &c->cfg;
  c->lc.psi = psi_new();
  ck_assert_ptr_nonnull(c->lc.psi);
  c->lc.cache = emmcache_new(0);
  ck_assert_ptr_nonnull(c->lc.cache);
  c->lc.emm_file = c->emmpath;
  c->lc.outfd[0] = c->outfd;
  c->lc.n_outfd = 1;
}

static void cls_close(cls_t *c) {
  ipiclient_poll_free(c->lc.ipi_pending);
  ipiclient_free(c->lc.ipi);
  scrambler_free(c->lc.scr);
  device_state_free(c->lc.dev);
  biss_ca_state_free(c->lc.biss_ca);
  emmcache_free(c->lc.cache);
  psi_free(c->lc.psi);
  close(c->outfd);
  unlink(c->outpath);
  unlink(c->emmpath);
  unlink(c->keypath);
  EVP_PKEY_free(c->key);
}

static int feed_psi(loop_ctx_t *lc, unsigned pid, const unsigned char *sec, size_t len) {
  unsigned char pkt[188];

  wrap_section_packet(pkt, pid, sec, len);
  return pkt_cb(lc, pkt);
}

static int feed_section(loop_ctx_t *lc, unsigned pid, const unsigned char *sec, size_t len) {
  static unsigned char cc[0x2000];
  unsigned char pkt[188];
  size_t off = 0;
  int first = 1;
  int rc = 0;

  while (off < len) {
    size_t room = first ? 183 : 184;
    size_t n = len - off < room ? len - off : room;
    size_t at = 4;

    pkt[0] = 0x47;
    pkt[1] = (unsigned char)((first ? 0x40 : 0x00) | ((pid >> 8) & 0x1F));
    pkt[2] = (unsigned char)pid;
    pkt[3] = (unsigned char)(0x10 | (cc[pid & 0x1FFF]++ & 0x0F));
    if (first)
      pkt[at++] = 0;
    memcpy(pkt + at, sec + off, n);
    memset(pkt + at + n, 0xFF, 188 - at - n);
    rc |= pkt_cb(lc, pkt);
    off += n;
    first = 0;
  }
  return rc;
}

static void feed_garbage_packet(loop_ctx_t *lc, unsigned pid) {
  unsigned char pkt[188];

  memset(pkt, 0xFF, sizeof pkt);
  pkt[0] = 0x47;
  pkt[1] = (unsigned char)((pid >> 8) & 0x1F);
  pkt[2] = (unsigned char)pid;
  pkt[3] = 0x10;
  pkt_cb(lc, pkt);
}

static int prime_cas(loop_ctx_t *lc, unsigned ca_system_id, unsigned scrambling_mode) {
  unsigned char sec[64];
  size_t slen;
  int rc = 0;

  slen = build_pat(sec, 1, PMT_PID);
  rc |= feed_psi(lc, 0x0000, sec, slen);
  slen = build_pmt_with_ca(sec, 1, ca_system_id, ECM_PID, scrambling_mode);
  rc |= feed_psi(lc, PMT_PID, sec, slen);
  feed_garbage_packet(lc, ECM_PID);
  slen = build_cat(sec, ca_system_id, EMM_PID);
  rc |= feed_psi(lc, 0x0001, sec, slen);
  return rc;
}

static void fill_bytes(unsigned char *p, size_t n, unsigned char base) {
  for (size_t i = 0; i < n; i++) p[i] = (unsigned char)(base + i);
}

static void make_ts(unsigned char pkt[188], unsigned pid, unsigned char fill) {
  memset(pkt, fill, 188);
  pkt[0] = 0x47;
  pkt[1] = (unsigned char)((pid >> 8) & 0x1F);
  pkt[2] = (unsigned char)pid;
  pkt[3] = 0x10;
}

static size_t out_size(cls_t *c) {
  struct stat sb;

  pipeline_flush(&c->lc);
  ck_assert_int_eq(fstat(c->outfd, &sb), 0);
  return (size_t)sb.st_size;
}

static void out_read(const cls_t *c, size_t off, unsigned char *dst, size_t n) {
  ck_assert_int_eq(pread(c->outfd, dst, n, (off_t)off), (ssize_t)n);
}

START_TEST(classic_cas_resolves_and_updates_cw_for_both_parities_once_each) {
  cls_t c;
  unsigned char emm[1024];
  unsigned char ecm[64];
  unsigned char bk[CRYPTO_KEY_LEN];
  unsigned char sk[CRYPTO_KEY_LEN];
  unsigned char cw_a[16];
  unsigned char cw_b[16];
  size_t n;

  cls_open(&c);
  fill_bytes(bk, sizeof bk, 1);
  fill_bytes(sk, sizeof sk, 100);
  fill_bytes(cw_a, sizeof cw_a, 0x10);
  fill_bytes(cw_b, sizeof cw_b, 0x50);
  ck_assert_int_eq(prime_cas(&c.lc, CAS_ID, MODE_CISSA), 0);
  ck_assert_uint_eq(c.lc.ecm_pid, ECM_PID);
  ck_assert_uint_eq(c.lc.emm_pid, EMM_PID);
  ck_assert_ptr_nonnull(c.lc.dev);
  ck_assert_ptr_nonnull(c.lc.scr);
  ck_assert_str_eq(c.lc.cas_mode, "classic");
  ck_assert_int_eq(c.lc.cw_len, 16);

  n = build_emm_u(c.key, bk, emm, sizeof emm);
  ck_assert_int_eq(feed_section(&c.lc, EMM_PID, emm, n), 0);
  n = build_emm_g(bk, sk, 0x0064, emm, sizeof emm);
  ck_assert_int_eq(feed_section(&c.lc, EMM_PID, emm, n), 0);
  ck_assert_uint_eq(c.lc.emm_total, 2u);
  ck_assert_int_eq(c.lc.emmcache_dirty, 1);

  n = build_ecm(sk, cw_a, 16, ecm, sizeof ecm);
  feed_section(&c.lc, ECM_PID, ecm, n);
  ck_assert_int_eq(c.lc.have_cw[0], 1);
  ck_assert_int_eq(c.lc.have_cw[1], 0);
  ck_assert_mem_eq(c.lc.last_cw[0], cw_a, 16);
  ck_assert_uint_eq(c.lc.cryptoperiod_transitions_total, 1u);
  feed_section(&c.lc, ECM_PID, ecm, n);
  ck_assert_uint_eq(c.lc.cryptoperiod_transitions_total, 1u);
  ck_assert_uint_eq(c.lc.ecm_total, 2u);

  n = build_ecm(sk, cw_b, 16, ecm, sizeof ecm);
  ecm[0] = 0x81;
  feed_section(&c.lc, ECM_PID, ecm, n);
  ck_assert_int_eq(c.lc.have_cw[1], 1);
  ck_assert_mem_eq(c.lc.last_cw[1], cw_b, 16);
  ck_assert_uint_eq(c.lc.cryptoperiod_transitions_total, 2u);
  ck_assert_uint_eq(c.lc.ecm_errors_total, 0u);
  cls_close(&c);
}
END_TEST

START_TEST(classic_cas_counts_ecm_errors_and_ignores_unrelated_table_ids) {
  cls_t c;
  unsigned char bad[64];
  unsigned char other[64];
  size_t n;

  cls_open(&c);
  ck_assert_int_eq(prime_cas(&c.lc, CAS_ID, MODE_CISSA), 0);
  n = build_bare_section(bad, 0x80, 24);
  feed_section(&c.lc, ECM_PID, bad, n);
  ck_assert_uint_eq(c.lc.ecm_errors_total, 1u);
  ck_assert_int_eq(c.lc.have_cw[0], 0);
  n = build_bare_section(other, 0x4E, 24);
  feed_section(&c.lc, ECM_PID, other, n);
  ck_assert_uint_eq(c.lc.ecm_errors_total, 1u);
  ck_assert_uint_eq(c.lc.ecm_total, 2u);
  cls_close(&c);
}
END_TEST

START_TEST(classic_cas_descrambles_packets_with_the_resolved_control_words) {
  cls_t c;
  unsigned char emm[1024];
  unsigned char ecm[64];
  unsigned char bk[CRYPTO_KEY_LEN];
  unsigned char sk[CRYPTO_KEY_LEN];
  unsigned char cw_even[16];
  unsigned char cw_odd[16];
  unsigned char clear_even[188];
  unsigned char clear_odd[188];
  unsigned char clear_plain[188];
  unsigned char in_even[188];
  unsigned char in_odd[188];
  unsigned char in_reserved[188];
  unsigned char out[188 * 4];
  scrambler_t *tx = scrambler_new(SCRAMBLE_ALGO_CISSA);
  size_t base;
  size_t n;

  cls_open(&c);
  fill_bytes(bk, sizeof bk, 3);
  fill_bytes(sk, sizeof sk, 90);
  fill_bytes(cw_even, sizeof cw_even, 0x21);
  fill_bytes(cw_odd, sizeof cw_odd, 0x61);
  ck_assert_int_eq(prime_cas(&c.lc, CAS_ID, MODE_CISSA), 0);
  n = build_emm_u(c.key, bk, emm, sizeof emm);
  feed_section(&c.lc, EMM_PID, emm, n);
  n = build_emm_g(bk, sk, 0x0064, emm, sizeof emm);
  feed_section(&c.lc, EMM_PID, emm, n);
  n = build_ecm(sk, cw_even, 16, ecm, sizeof ecm);
  feed_section(&c.lc, ECM_PID, ecm, n);
  n = build_ecm(sk, cw_odd, 16, ecm, sizeof ecm);
  ecm[0] = 0x81;
  feed_section(&c.lc, ECM_PID, ecm, n);

  ck_assert_int_eq(scrambler_set_key(tx, SCRAMBLE_PARITY_EVEN, cw_even, 16, NULL, NULL), 0);
  ck_assert_int_eq(scrambler_set_key(tx, SCRAMBLE_PARITY_ODD, cw_odd, 16, NULL, NULL), 0);
  make_ts(clear_even, VIDEO_PID, 0x5A);
  make_ts(clear_odd, VIDEO_PID, 0xA5);
  make_ts(clear_plain, VIDEO_PID, 0x3C);
  memcpy(in_even, clear_even, 188);
  memcpy(in_odd, clear_odd, 188);
  ck_assert_int_eq(scrambler_encrypt_packet(tx, in_even, SCRAMBLE_PARITY_EVEN), 0);
  ck_assert_int_eq(scrambler_encrypt_packet(tx, in_odd, SCRAMBLE_PARITY_ODD), 0);
  ck_assert_uint_eq((in_even[3] >> 6) & 3u, 2u);
  ck_assert_uint_eq((in_odd[3] >> 6) & 3u, 3u);

  base = (size_t)c.lc.packets * 188u;
  ck_assert_int_eq(pkt_cb(&c.lc, in_even), 0);
  ck_assert_int_eq(pkt_cb(&c.lc, in_odd), 0);
  memcpy(in_reserved, clear_plain, 188);
  in_reserved[3] = (unsigned char)((in_reserved[3] & 0x3F) | 0x40);
  ck_assert_int_eq(pkt_cb(&c.lc, in_reserved), 0);
  ck_assert_uint_eq(c.lc.unexpected_clear_packets_total, 1u);
  ck_assert_int_eq(pkt_cb(&c.lc, clear_plain), 0);

  ck_assert_uint_eq(out_size(&c), base + 188u * 4u);
  out_read(&c, base, out, sizeof out);
  ck_assert_mem_eq(out, clear_even, 188);
  ck_assert_mem_eq(out + 188, clear_odd, 188);
  ck_assert_mem_eq(out + 376 + 4, clear_plain + 4, 184);
  ck_assert_uint_eq((out[376 + 3] >> 6) & 3u, 1u);
  ck_assert_mem_eq(out + 564, clear_plain, 188);
  scrambler_free(tx);
  cls_close(&c);
}
END_TEST

START_TEST(scrambled_packets_before_any_control_word_are_forwarded_and_counted_unexpected) {
  cls_t c;
  unsigned char pkt[188];
  unsigned char out[188];
  size_t base;

  cls_open(&c);
  ck_assert_int_eq(prime_cas(&c.lc, CAS_ID, MODE_CISSA), 0);
  base = (size_t)c.lc.packets * 188u;
  make_ts(pkt, VIDEO_PID, 0x77);
  pkt[3] = (unsigned char)((pkt[3] & 0x3F) | 0x80);
  ck_assert_int_eq(pkt_cb(&c.lc, pkt), 0);
  ck_assert_uint_eq(c.lc.unexpected_clear_packets_total, 1u);
  ck_assert_uint_eq(out_size(&c), base + 188u);
  out_read(&c, base, out, sizeof out);
  ck_assert_mem_eq(out, pkt, 188);
  cls_close(&c);
}
END_TEST

START_TEST(classic_cas_without_key_serial_or_cache_file_is_fatal) {
  cls_t c;
  int rc;

  cls_open(&c);
  c.cfg.key_path = NULL;
  rc = prime_cas(&c.lc, CAS_ID, MODE_CISSA);
  ck_assert_int_eq(rc, 1);
  ck_assert_int_eq(c.lc.fatal, 1);
  ck_assert_ptr_null(c.lc.dev);
  cls_close(&c);

  cls_open(&c);
  c.cfg.serial = NULL;
  ck_assert_int_eq(prime_cas(&c.lc, CAS_ID, MODE_CISSA), 1);
  ck_assert_int_eq(c.lc.fatal, 1);
  cls_close(&c);

  cls_open(&c);
  c.cfg.emm_file = NULL;
  ck_assert_int_eq(prime_cas(&c.lc, CAS_ID, MODE_CISSA), 1);
  ck_assert_int_eq(c.lc.fatal, 1);
  cls_close(&c);
}
END_TEST

START_TEST(classic_cas_with_unreadable_key_is_fatal_and_counted) {
  cls_t c;

  cls_open(&c);
  c.cfg.key_path = "/nonexistent-dir-dipidescramble/key.pem";
  ck_assert_int_eq(prime_cas(&c.lc, CAS_ID, MODE_CISSA), 1);
  ck_assert_int_eq(c.lc.fatal, 1);
  ck_assert_uint_eq(c.lc.key_load_errors_total, 1u);
  ck_assert_ptr_null(c.lc.dev);
  cls_close(&c);
}
END_TEST

START_TEST(classic_cas_with_unrecognized_scrambling_mode_is_fatal) {
  cls_t c;

  cls_open(&c);
  ck_assert_int_eq(prime_cas(&c.lc, CAS_ID, 0x7F), 1);
  ck_assert_int_eq(c.lc.fatal, 1);
  ck_assert_ptr_nonnull(c.lc.dev);
  ck_assert_ptr_null(c.lc.scr);
  cls_close(&c);
}
END_TEST

START_TEST(classic_cas_accepts_csa_scrambling_modes) {
  const unsigned modes[2] = {0x01, 0x02};
  unsigned i;

  for (i = 0; i < 2; i++) {
    cls_t c;

    cls_open(&c);
    ck_assert_int_eq(prime_cas(&c.lc, CAS_ID, modes[i]), 0);
    ck_assert_int_eq(c.lc.fatal, 0);
    ck_assert_int_eq(c.lc.cw_len, 8);
    cls_close(&c);
  }
}
END_TEST

START_TEST(biss1e_without_any_key_is_fatal) {
  cls_t c;

  cls_open(&c);
  ck_assert_int_eq(prime_cas(&c.lc, 0x2602, 0), 1);
  ck_assert_int_eq(c.lc.fatal, 1);
  ck_assert_ptr_null(c.lc.scr);
  cls_close(&c);
}
END_TEST

START_TEST(biss1e_with_esw_and_id_resolves_a_cissa_descrambler) {
  cls_t c;

  cls_open(&c);
  c.cfg.biss2_esw_given = 1;
  c.cfg.biss2_id_given = 1;
  memset(c.cfg.biss2_esw, 0x11, sizeof c.cfg.biss2_esw);
  memset(c.cfg.biss2_id, 0x22, sizeof c.cfg.biss2_id);
  ck_assert_int_eq(prime_cas(&c.lc, 0x2602, 0), 0);
  ck_assert_ptr_nonnull(c.lc.scr);
  ck_assert_str_eq(c.lc.cas_mode, "biss1e");
  ck_assert_int_eq(c.lc.cw_len, 16);
  cls_close(&c);
}
END_TEST

START_TEST(biss1e_with_biss1_sw_resolves_a_csa_descrambler) {
  cls_t c;

  cls_open(&c);
  c.cfg.biss1_sw_given = 1;
  memset(c.cfg.biss1_sw, 0x33, sizeof c.cfg.biss1_sw);
  ck_assert_int_eq(prime_cas(&c.lc, 0x2602, 0), 0);
  ck_assert_ptr_nonnull(c.lc.scr);
  ck_assert_int_eq(c.lc.cw_len, 8);
  cls_close(&c);
}
END_TEST

START_TEST(biss_ca_without_a_private_key_is_fatal) {
  cls_t c;

  cls_open(&c);
  ck_assert_int_eq(prime_cas(&c.lc, 0x2610, 0), 1);
  ck_assert_int_eq(c.lc.fatal, 1);
  ck_assert_ptr_null(c.lc.biss_ca);
  cls_close(&c);
}
END_TEST

START_TEST(biss_ca_with_an_unreadable_private_key_is_fatal_and_counted) {
  cls_t c;

  cls_open(&c);
  c.cfg.biss2_ca_key[c.cfg.n_biss2_ca_key++] = "/nonexistent-dir-dipidescramble/ca.pem";
  ck_assert_int_eq(prime_cas(&c.lc, 0x2610, 0), 1);
  ck_assert_int_eq(c.lc.fatal, 1);
  ck_assert_uint_eq(c.lc.key_load_errors_total, 1u);
  cls_close(&c);
}
END_TEST

START_TEST(biss_ca_with_a_private_key_resolves_a_cissa_descrambler) {
  cls_t c;

  cls_open(&c);
  c.cfg.biss2_ca_key[c.cfg.n_biss2_ca_key++] = c.keypath;
  ck_assert_int_eq(prime_cas(&c.lc, 0x2610, 0), 0);
  ck_assert_ptr_nonnull(c.lc.biss_ca);
  ck_assert_ptr_nonnull(c.lc.scr);
  ck_assert_str_eq(c.lc.cas_mode, "biss-ca");
  ck_assert_int_eq(c.lc.cw_len, 16);
  cls_close(&c);
}
END_TEST

START_TEST(biss_ca_ecm_that_does_not_parse_counts_an_error) {
  cls_t c;
  unsigned char ecm[64];
  size_t n;

  cls_open(&c);
  c.cfg.biss2_ca_key[c.cfg.n_biss2_ca_key++] = c.keypath;
  ck_assert_int_eq(prime_cas(&c.lc, 0x2610, 0), 0);
  n = build_bare_section(ecm, 0x80, 40);
  feed_section(&c.lc, ECM_PID, ecm, n);
  ck_assert_uint_eq(c.lc.ecm_total, 1u);
  ck_assert_uint_eq(c.lc.ecm_errors_total, 1u);
  cls_close(&c);
}
END_TEST

START_TEST(emm_cache_is_saved_after_the_debounce_and_flushed_on_demand) {
  cls_t c;
  unsigned char emm[1024];
  unsigned char bk[CRYPTO_KEY_LEN];
  struct stat sb;
  size_t n;

  cls_open(&c);
  fill_bytes(bk, sizeof bk, 7);
  ck_assert_int_eq(prime_cas(&c.lc, CAS_ID, MODE_CISSA), 0);
  pipeline_service_emmcache(&c.lc);
  ck_assert_int_ne(stat(c.emmpath, &sb), 0);

  n = build_emm_u(c.key, bk, emm, sizeof emm);
  feed_section(&c.lc, EMM_PID, emm, n);
  ck_assert_int_eq(c.lc.emmcache_dirty, 1);
  c.lc.emmcache_last_save = mono_seconds();
  pipeline_service_emmcache(&c.lc);
  ck_assert_int_eq(c.lc.emmcache_dirty, 1);
  ck_assert_int_ne(stat(c.emmpath, &sb), 0);

  c.lc.emmcache_last_save = mono_seconds() - 10.0;
  pipeline_service_emmcache(&c.lc);
  ck_assert_int_eq(c.lc.emmcache_dirty, 0);
  ck_assert_int_eq(stat(c.emmpath, &sb), 0);
  ck_assert_uint_eq((size_t)sb.st_size, n);

  unlink(c.emmpath);
  pipeline_flush_emmcache(&c.lc);
  ck_assert_int_ne(stat(c.emmpath, &sb), 0);
  c.lc.emmcache_dirty = 1;
  pipeline_flush_emmcache(&c.lc);
  ck_assert_int_eq(c.lc.emmcache_dirty, 0);
  ck_assert_int_eq(stat(c.emmpath, &sb), 0);
  cls_close(&c);
}
END_TEST

START_TEST(unicast_emm_with_an_invalid_uri_is_ignored) {
  cls_t c;

  cls_open(&c);
  c.cfg.unicast_emm_uri = "not-a-valid-uri";
  ck_assert_int_eq(prime_cas(&c.lc, CAS_ID, MODE_CISSA), 0);
  ck_assert_ptr_null(c.lc.ipi);
  ck_assert_ptr_null(c.lc.ipi_pending);
  pipeline_service_unicast_emm(&c.lc);
  ck_assert_int_eq(c.lc.fatal, 0);
  cls_close(&c);
}
END_TEST

static void drive_unicast(loop_ctx_t *lc, int max_iters) {

  for (int i = 0; i < max_iters && lc->ipi_pending; i++) {
    struct pollfd pfd;

    pfd.fd = ipiclient_poll_fd(lc->ipi_pending);
    pfd.events = ipiclient_poll_events(lc->ipi_pending);
    pfd.revents = 0;
    poll(&pfd, 1, 100);
    pipeline_service_unicast_emm(lc);
  }
}

START_TEST(unicast_emm_fetch_feeds_the_cache_and_marks_it_dirty) {
  cls_t c;
  unsigned port;
  int listen_fd = make_listener(&port);
  pthread_t th;
  server_arg_t sarg;
  unsigned char emm[1024];
  unsigned char body[1100];
  unsigned char bk[CRYPTO_KEY_LEN];
  char resp[2048];
  char uri[96];
  size_t n;
  int rl;

  cls_open(&c);
  fill_bytes(bk, sizeof bk, 9);
  n = build_emm_u(c.key, bk, emm, sizeof emm);
  body[0] = 0;
  body[1] = (unsigned char)(n >> 8);
  body[2] = (unsigned char)n;
  memcpy(body + 3, emm, n);
  rl = snprintf(resp, sizeof resp, "HTTP/1.1 200 OK\r\nContent-Length: %zu\r\nConnection: close\r\n\r\n", n + 3);
  memcpy(resp + rl, body, n + 3);
  sarg.listen_fd = listen_fd;
  sarg.response = resp;
  sarg.response_len = (size_t)rl + n + 3;
  ck_assert_int_eq(pthread_create(&th, NULL, serve_once, &sarg), 0);

  snprintf(uri, sizeof uri, "http://tok@127.0.0.1:%u/emm", port);
  c.cfg.unicast_emm_uri = uri;
  ck_assert_int_eq(prime_cas(&c.lc, CAS_ID, MODE_CISSA), 0);
  ck_assert_ptr_nonnull(c.lc.ipi);
  ck_assert_ptr_nonnull(c.lc.ipi_pending);
  ck_assert_int_eq(c.lc.emmcache_dirty, 0);
  drive_unicast(&c.lc, 300);
  ck_assert_ptr_null(c.lc.ipi_pending);
  ck_assert_int_eq(c.lc.emmcache_dirty, 1);
  pthread_join(th, NULL);
  close(listen_fd);
  cls_close(&c);
}
END_TEST

START_TEST(unicast_emm_fetch_error_clears_the_pending_poll) {
  cls_t c;
  char uri[] = "http://tok@127.0.0.1:1/emm";

  cls_open(&c);
  c.cfg.unicast_emm_uri = uri;
  ck_assert_int_eq(prime_cas(&c.lc, CAS_ID, MODE_CISSA), 0);
  ck_assert_ptr_nonnull(c.lc.ipi_pending);
  drive_unicast(&c.lc, 300);
  ck_assert_ptr_null(c.lc.ipi_pending);
  ck_assert_int_eq(c.lc.emmcache_dirty, 0);
  pipeline_service_unicast_emm(&c.lc);
  cls_close(&c);
}
END_TEST

START_TEST(unicast_emm_fetch_without_a_response_stays_pending) {
  cls_t c;
  unsigned port;
  int listen_fd = make_listener(&port);
  char uri[96];
  int i;

  cls_open(&c);
  snprintf(uri, sizeof uri, "http://tok@127.0.0.1:%u/emm", port);
  c.cfg.unicast_emm_uri = uri;
  ck_assert_int_eq(prime_cas(&c.lc, CAS_ID, MODE_CISSA), 0);
  ck_assert_ptr_nonnull(c.lc.ipi_pending);
  for (i = 0; i < 5; i++) {
    usleep(10000);
    pipeline_service_unicast_emm(&c.lc);
  }
  ck_assert_ptr_nonnull(c.lc.ipi_pending);
  close(listen_fd);
  cls_close(&c);
}
END_TEST

START_TEST(output_flush_failure_sets_emit_failed_and_stops_the_batch) {
  cls_t c;
  unsigned char pkt[188];
  int i;
  int stopped_at = -1;

  cls_open(&c);
  close(c.outfd);
  c.outfd = open("/dev/full", O_WRONLY);
  if (c.outfd < 0) {
    c.outfd = open("/dev/null", O_WRONLY);
    cls_close(&c);
    return;
  }
  c.lc.outfd[0] = c.outfd;
  for (i = 0; i < PIPELINE_OUT_BATCH_PKTS + 2; i++) {
    make_ts(pkt, 0x1FFF, (unsigned char)i);
    if (pkt_cb(&c.lc, pkt) == 1 && stopped_at < 0)
      stopped_at = i;
  }
  ck_assert_int_eq(stopped_at, PIPELINE_OUT_BATCH_PKTS);
  ck_assert_int_eq(c.lc.emit_failed, 1);
  ck_assert_uint_eq(c.lc.output_errors_total, 1u);
  ck_assert_uint_eq(c.lc.packets, (unsigned)PIPELINE_OUT_BATCH_PKTS);
  pipeline_flush(&c.lc);
  cls_close(&c);
}
END_TEST

START_TEST(rtmp_write_results_are_edge_logged) {
  int had_error = 0;
  char log[LOG_CAPTURE_BUF];

  log_capture_begin();
  descramble_rtmp_note_result(1, &had_error, 0);
  descramble_rtmp_note_result(0, &had_error, 0);
  descramble_rtmp_note_result(0, &had_error, 0);
  descramble_rtmp_note_result(1, &had_error, 0);
  descramble_rtmp_note_result(1, &had_error, 0);
  log_capture_end(log, sizeof log);
  ck_assert_int_eq(had_error, 0);
  ck_assert_int_eq(log_count_of(log, "write failed"), 1);
  ck_assert_int_eq(log_count_of(log, "recovered"), 1);
}
END_TEST

START_TEST(inspect_callbacks_feed_the_input_and_output_inspectors) {
  cls_t c;
  unsigned char pkt[188];
  int i;

  cls_open(&c);
  c.lc.insp_in = tsinspect_new(METRICS_INSPECT_TS_BASIC);
  c.lc.insp_out = tsinspect_new(METRICS_INSPECT_TS_BASIC);
  ck_assert_ptr_nonnull(c.lc.insp_in);
  ck_assert_ptr_nonnull(c.lc.insp_out);
  for (i = 0; i < 5; i++) {
    make_ts(pkt, 0x1FFF, (unsigned char)i);
    ck_assert_int_eq(pkt_cb_inspect(&c.lc, pkt), 0);
  }
  ck_assert_uint_eq(tsinspect_counters(c.lc.insp_in)->packets, 5u);
  ck_assert_uint_eq(tsinspect_counters(c.lc.insp_out)->packets, 5u);
  ck_assert_uint_eq(c.lc.packets, 5u);
  tsinspect_free(c.lc.insp_in);
  tsinspect_free(c.lc.insp_out);
  c.lc.insp_in = NULL;
  c.lc.insp_out = NULL;
  cls_close(&c);
}
END_TEST

START_TEST(push_metrics_reports_counters_without_a_cas_mode) {
  cls_t c;
  sink_t sink;
  seen_t seen;
  uint64_t v = 99;

  cls_open(&c);
  sink_open(&sink, METRICS_COMPONENT_DESCRAMBLE, "dscr1", 5.0);
  c.lc.scrambled_packets_total = 11;
  c.lc.unexpected_clear_packets_total = 3;
  c.lc.key_load_errors_total = 2;
  c.lc.output_errors_total = 5;
  pipeline_push_metrics(&sink.mx, &c.lc);
  ck_assert_int_eq(sink_read(&sink, &seen), 1);
  ck_assert_int_eq(seen_has(&seen, METRICS_ID_CAS_SCRAMBLED_PACKETS_TOTAL, &v), 1);
  ck_assert_uint_eq(v, 11u);
  ck_assert_int_eq(seen_has(&seen, METRICS_ID_CAS_UNEXPECTED_CLEAR_PACKETS_TOTAL, &v), 1);
  ck_assert_uint_eq(v, 3u);
  ck_assert_int_eq(seen_has(&seen, METRICS_ID_DESCRAMBLE_KEY_LOAD_ERRORS_TOTAL, &v), 1);
  ck_assert_uint_eq(v, 2u);
  ck_assert_int_eq(seen_has(&seen, METRICS_ID_DESCRAMBLE_OUTPUT_ERRORS_TOTAL, &v), 1);
  ck_assert_uint_eq(v, 5u);
  ck_assert_int_eq(seen_has(&seen, METRICS_ID_DESCRAMBLE_MODE, NULL), 0);
  ck_assert_int_eq(seen_has(&seen, METRICS_ID_CAS_ECM_TOTAL, NULL), 0);
  sink_close(&sink);
  cls_close(&c);
}
END_TEST

START_TEST(push_metrics_adds_the_cas_counters_once_a_mode_is_resolved) {
  cls_t c;
  sink_t sink;
  seen_t seen;
  uint64_t v = 99;

  cls_open(&c);
  ck_assert_int_eq(prime_cas(&c.lc, CAS_ID, MODE_CISSA), 0);
  c.lc.ecm_total = 7;
  c.lc.ecm_errors_total = 2;
  c.lc.emm_total = 4;
  c.lc.cryptoperiod_transitions_total = 6;
  sink_open(&sink, METRICS_COMPONENT_DESCRAMBLE, "dscr1", 5.0);
  pipeline_push_metrics(&sink.mx, &c.lc);
  ck_assert_int_eq(sink_read(&sink, &seen), 1);
  ck_assert_int_eq(seen_has(&seen, METRICS_ID_DESCRAMBLE_MODE, &v), 1);
  ck_assert_uint_eq(v, 1u);
  ck_assert_int_eq(seen_has(&seen, METRICS_ID_CAS_ECM_TOTAL, &v), 1);
  ck_assert_uint_eq(v, 7u);
  ck_assert_int_eq(seen_has(&seen, METRICS_ID_CAS_ECM_ERRORS_TOTAL, &v), 1);
  ck_assert_uint_eq(v, 2u);
  ck_assert_int_eq(seen_has(&seen, METRICS_ID_CAS_EMM_TOTAL, &v), 1);
  ck_assert_uint_eq(v, 4u);
  ck_assert_int_eq(seen_has(&seen, METRICS_ID_CAS_CRYPTOPERIOD_TRANSITIONS_TOTAL, &v), 1);
  ck_assert_uint_eq(v, 6u);
  ck_assert_int_eq(seen_has(&seen, METRICS_ID_CAS_EMM_DROPPED_TOTAL, &v), 1);
  ck_assert_uint_eq(v, 0u);
  sink_close(&sink);
  cls_close(&c);
}
END_TEST

START_TEST(push_metrics_is_gated_by_the_exporter_interval) {
  cls_t c;
  sink_t sink;
  seen_t seen;

  cls_open(&c);
  sink_open(&sink, METRICS_COMPONENT_DESCRAMBLE, "dscr1", 1000.0);
  pipeline_push_metrics(&sink.mx, &c.lc);
  ck_assert_int_eq(sink_read(&sink, &seen), 1);
  pipeline_push_metrics(&sink.mx, &c.lc);
  ck_assert_int_eq(sink_read(&sink, &seen), 0);
  sink_close(&sink);
  cls_close(&c);
}
END_TEST

static Suite *pipeline_suite(void) {
  Suite *s = suite_create("pipeline");
  TCase *tc = tcase_create("core");
  tcase_set_timeout(tc, 30);
  tcase_add_test(tc, emit_downstream_writes_identical_bytes_to_every_outfd);
  tcase_add_test(tc, biss1e_ecm_on_null_dev_does_not_crash);
  tcase_add_test(tc, biss1e_emm_on_null_dev_does_not_crash);
  tcase_add_test(tc, classic_cas_resolves_and_updates_cw_for_both_parities_once_each);
  tcase_add_test(tc, classic_cas_counts_ecm_errors_and_ignores_unrelated_table_ids);
  tcase_add_test(tc, classic_cas_descrambles_packets_with_the_resolved_control_words);
  tcase_add_test(tc, scrambled_packets_before_any_control_word_are_forwarded_and_counted_unexpected);
  tcase_add_test(tc, classic_cas_without_key_serial_or_cache_file_is_fatal);
  tcase_add_test(tc, classic_cas_with_unreadable_key_is_fatal_and_counted);
  tcase_add_test(tc, classic_cas_with_unrecognized_scrambling_mode_is_fatal);
  tcase_add_test(tc, classic_cas_accepts_csa_scrambling_modes);
  tcase_add_test(tc, biss1e_without_any_key_is_fatal);
  tcase_add_test(tc, biss1e_with_esw_and_id_resolves_a_cissa_descrambler);
  tcase_add_test(tc, biss1e_with_biss1_sw_resolves_a_csa_descrambler);
  tcase_add_test(tc, biss_ca_without_a_private_key_is_fatal);
  tcase_add_test(tc, biss_ca_with_an_unreadable_private_key_is_fatal_and_counted);
  tcase_add_test(tc, biss_ca_with_a_private_key_resolves_a_cissa_descrambler);
  tcase_add_test(tc, biss_ca_ecm_that_does_not_parse_counts_an_error);
  tcase_add_test(tc, emm_cache_is_saved_after_the_debounce_and_flushed_on_demand);
  tcase_add_test(tc, unicast_emm_with_an_invalid_uri_is_ignored);
  tcase_add_test(tc, unicast_emm_fetch_feeds_the_cache_and_marks_it_dirty);
  tcase_add_test(tc, unicast_emm_fetch_error_clears_the_pending_poll);
  tcase_add_test(tc, unicast_emm_fetch_without_a_response_stays_pending);
  tcase_add_test(tc, output_flush_failure_sets_emit_failed_and_stops_the_batch);
  tcase_add_test(tc, rtmp_write_results_are_edge_logged);
  tcase_add_test(tc, inspect_callbacks_feed_the_input_and_output_inspectors);
  tcase_add_test(tc, push_metrics_reports_counters_without_a_cas_mode);
  tcase_add_test(tc, push_metrics_adds_the_cas_counters_once_a_mode_is_resolved);
  tcase_add_test(tc, push_metrics_is_gated_by_the_exporter_interval);
  suite_add_tcase(s, tc);
  return s;
}

int main(void) {
  SRunner *sr = srunner_create(pipeline_suite());
  srunner_run_all(sr, CK_NORMAL);
  int failed = srunner_ntests_failed(sr);
  srunner_free(sr);
  return failed == 0 ? EXIT_SUCCESS : EXIT_FAILURE;
}
