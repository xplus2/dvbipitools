/* Copyright 2026 dvbipitools authors. Licensed under GPL-3.0-or-later.
 * See NOTICE and LICENSE for details and authorship information. */

#include <check.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>

#include "lib/cas/biss/ca.h"
#include "lib/cas/biss/ca_engine.h"
#include "lib/cas/biss/ca_sections.h"

#define MAX_RECEIVERS 15

static char g_dir[] = "/tmp/biss_ca_engine_test_XXXXXX";
static char g_priv[600];

static void noop_emit(void *ctx, const unsigned char pkt[188]) {
  (void)ctx;
  (void)pkt;
}

static void write_receiver_keypair(const char *dir, const char *name, const char *priv_path) {
  char path[512];
  char cmd[1400];
  snprintf(path, sizeof path, "%s/%s", dir, name);
  snprintf(cmd, sizeof cmd, "openssl genrsa -out %s 2048 2>/dev/null && openssl rsa -in %s -pubout -out %s 2>/dev/null", priv_path, priv_path, path);
  ck_assert_int_eq(system(cmd), 0);
}

static void write_receiver_key(const char *dir, const char *name) {
  char path[512];
  char cmd[1024];
  snprintf(path, sizeof path, "%s/%s", dir, name);
  snprintf(cmd, sizeof cmd, "openssl genrsa 2048 2>/dev/null | openssl rsa -pubout -out %s 2>/dev/null", path);
  ck_assert_int_eq(system(cmd), 0);
}

static void setup(void) {
  ck_assert_ptr_nonnull(mkdtemp(g_dir));
  snprintf(g_priv, sizeof g_priv, "%s.key", g_dir);
  write_receiver_keypair(g_dir, "r1.pem", g_priv);
  write_receiver_key(g_dir, "r2.pem");
}

static void teardown(void) {
  char cmd[600];
  snprintf(cmd, sizeof cmd, "rm -rf %s", g_dir);
  system(cmd);
  unlink(g_priv);
}

static biss_ca_engine_cfg_t base_cfg(const unsigned *pids, size_t pid_count) {
  biss_ca_engine_cfg_t cfg;
  memset(&cfg, 0, sizeof cfg);
  cfg.receivers_dir = g_dir;
  cfg.esid = 0x1234;
  cfg.onid = 1;
  cfg.sw_period_ms = 1000;
  cfg.ecm_pid = 0x1FFA;
  cfg.emm_pid = 0x1FFB;
  cfg.pids = pids;
  cfg.pid_count = pid_count;
  cfg.flush_pid = pids[0];
  return cfg;
}

START_TEST(start_loads_receivers_and_stops_cleanly) {
  const unsigned pids[] = {0x0100};
  biss_ca_engine_cfg_t cfg = base_cfg(pids, 1);
  biss_ca_engine_t *e = biss_ca_engine_start(&cfg);
  ck_assert_ptr_nonnull(e);
  ck_assert_uint_eq(biss_ca_engine_receiver_count(e), 2u);
  ck_assert_uint_eq(biss_ca_engine_ecm_pid(e), 0x1FFAu);
  ck_assert_uint_eq(biss_ca_engine_emm_pid(e), 0x1FFBu);
  biss_ca_engine_stop(e);
}
END_TEST

START_TEST(start_rejects_empty_receivers_dir) {
  char empty_dir[] = "/tmp/biss_ca_engine_empty_XXXXXX";
  const unsigned pids[] = {0x0100};
  biss_ca_engine_cfg_t cfg;
  biss_ca_engine_t *e;
  ck_assert_ptr_nonnull(mkdtemp(empty_dir));
  cfg = base_cfg(pids, 1);
  cfg.receivers_dir = empty_dir;
  e = biss_ca_engine_start(&cfg);
  ck_assert_ptr_null(e);
  rmdir(empty_dir);
}
END_TEST

START_TEST(start_rejects_short_sw_period) {
  const unsigned pids[] = {0x0100};
  biss_ca_engine_cfg_t cfg = base_cfg(pids, 1);
  cfg.sw_period_ms = 999;
  ck_assert_ptr_null(biss_ca_engine_start(&cfg));
}
END_TEST

START_TEST(ecm_and_emm_become_due_and_repeat_rate_limited) {
  const unsigned pids[] = {0x0100};
  biss_ca_engine_cfg_t cfg = base_cfg(pids, 1);
  biss_ca_engine_t *e = biss_ca_engine_start(&cfg);
  unsigned char buf[4096];
  size_t len = 0;

  ck_assert_int_eq(biss_ca_engine_ecm_due(e, 1.0, buf, sizeof buf, &len), 0);
  ck_assert_uint_gt(len, 0u);
  /* immediate re-poll: not due yet (T_ECM_MIN not elapsed) */
  ck_assert_int_eq(biss_ca_engine_ecm_due(e, 1.01, buf, sizeof buf, &len), -1);
  ck_assert_int_eq(biss_ca_engine_ecm_due(e, 1.2, buf, sizeof buf, &len), 0);

  ck_assert_int_eq(biss_ca_engine_emm_due(e, 1.0, buf, sizeof buf, &len), 0);
  ck_assert_uint_gt(len, 0u);
  ck_assert_int_eq(biss_ca_engine_emm_due(e, 1.05, buf, sizeof buf, &len), -1);
  ck_assert_int_eq(biss_ca_engine_emm_due(e, 1.3, buf, sizeof buf, &len), 0);

  biss_ca_engine_stop(e);
}
END_TEST

START_TEST(sw_rotation_changes_ecm_content) {
  const unsigned pids[] = {0x0100};
  biss_ca_engine_cfg_t cfg = base_cfg(pids, 1);
  biss_ca_engine_t *e = biss_ca_engine_start(&cfg);
  unsigned char pkt[188];
  unsigned char before[4096];
  unsigned char after[4096];
  size_t before_len;
  size_t after_len;

  memset(pkt, 0, sizeof pkt);
  pkt[0] = 0x47;
  pkt[1] = (unsigned char)(pkt[1] | (1 << 6)); /* pusi */
  pkt[1] = (unsigned char)((pkt[1] & 0xE0) | (0x0100 >> 8));
  pkt[2] = 0x00;

  biss_ca_engine_scramble_packet(e, 0x0100, 0.0, pkt, noop_emit, NULL);
  ck_assert_int_eq(biss_ca_engine_ecm_due(e, 0.0, before, sizeof before, &before_len), 0);

  biss_ca_engine_clock_tick(e, 1000);
  biss_ca_engine_scramble_packet(e, 0x0100, 1.0, pkt, noop_emit, NULL);
  ck_assert_int_eq(biss_ca_engine_ecm_due(e, 1.0, after, sizeof after, &after_len), 0);

  ck_assert_uint_eq(before_len, after_len);
  ck_assert_mem_ne(before, after, before_len);

  biss_ca_engine_stop(e);
}
END_TEST

START_TEST(prog_desc_and_cat_build_nonempty) {
  const unsigned pids[] = {0x0100};
  biss_ca_engine_cfg_t cfg = base_cfg(pids, 1);
  biss_ca_engine_t *e = biss_ca_engine_start(&cfg);
  unsigned char buf[64];
  size_t n;

  n = biss_ca_engine_prog_desc(e, buf, sizeof buf);
  ck_assert_uint_gt(n, 0u);
  ck_assert_uint_eq(buf[0], 0x09); /* CA_descriptor tag */

  n = biss_ca_engine_build_cat(e, buf, sizeof buf);
  ck_assert_uint_gt(n, 0u);
  ck_assert_uint_eq(buf[0], 0x01); /* CAT table_id */

  biss_ca_engine_stop(e);
}
END_TEST

START_TEST(reload_detects_added_and_removed_receiver) {
  const unsigned pids[] = {0x0100};
  biss_ca_engine_cfg_t cfg = base_cfg(pids, 1);
  biss_ca_engine_t *e = biss_ca_engine_start(&cfg);
  char path[512];

  /* same 2 files: unchanged */
  ck_assert_int_eq(biss_ca_engine_reload_receivers(e), 0);

  write_receiver_key(g_dir, "r3.pem");
  ck_assert_int_eq(biss_ca_engine_reload_receivers(e), 1);
  ck_assert_uint_eq(biss_ca_engine_receiver_count(e), 3u);

  snprintf(path, sizeof path, "%s/r3.pem", g_dir);
  unlink(path);
  ck_assert_int_eq(biss_ca_engine_reload_receivers(e), 1);
  ck_assert_uint_eq(biss_ca_engine_receiver_count(e), 2u);

  biss_ca_engine_stop(e);
}
END_TEST

START_TEST(force_sk_rotation_makes_next_scramble_rotate) {
  const unsigned pids[] = {0x0100};
  biss_ca_engine_cfg_t cfg = base_cfg(pids, 1);
  biss_ca_engine_t *e = biss_ca_engine_start(&cfg);
  unsigned char pkt[188];
  unsigned char before[4096];
  unsigned char after[4096];
  size_t before_len;
  size_t after_len;

  memset(pkt, 0, sizeof pkt);
  pkt[0] = 0x47;

  ck_assert_int_eq(biss_ca_engine_emm_due(e, 0.0, before, sizeof before, &before_len), 0);
  biss_ca_engine_force_sk_rotation(e);
  biss_ca_engine_scramble_packet(e, 0x0100, 1.0, pkt, noop_emit, NULL);
  ck_assert_int_eq(biss_ca_engine_emm_due(e, 1.0, after, sizeof after, &after_len), 0);
  ck_assert_mem_ne(before, after, before_len < after_len ? before_len : after_len);

  biss_ca_engine_stop(e);
}
END_TEST

static void join_path(char *out, size_t cap, const char *dir, const char *name) {
  snprintf(out, cap, "%s/%s", dir, name);
}

static size_t read_file(const char *path, unsigned char *out, size_t cap) {
  FILE *f = fopen(path, "rb");
  size_t n;

  ck_assert_ptr_nonnull(f);
  n = fread(out, 1, cap, f);
  fclose(f);
  return n;
}

static void write_file(const char *path, const void *data, size_t len) {
  FILE *f = fopen(path, "wb");

  ck_assert_ptr_nonnull(f);
  if (len) ck_assert_uint_eq(fwrite(data, 1, len, f), len);
  fclose(f);
}

static void write_named(const char *dir, const char *name, const void *data, size_t len) {
  char path[512];

  join_path(path, sizeof path, dir, name);
  write_file(path, data, len);
}

static void copy_named(const char *src_dir, const char *src, const char *dst_dir, const char *dst) {
  unsigned char buf[2048];
  char path[512];
  size_t n;

  join_path(path, sizeof path, src_dir, src);
  n = read_file(path, buf, sizeof buf);
  ck_assert_uint_gt(n, 0u);
  write_named(dst_dir, dst, buf, n);
}

enum {
  JUNK_TEXT,
  JUNK_EMPTY,
  JUNK_TRUNCATED_PEM,
  JUNK_BINARY,
  JUNK_SUBDIR,
  JUNK_PRIVATE_KEY,
  JUNK_KIND_COUNT
};

static void add_junk(const char *dir, int kind) {
  static const unsigned char bin[] = {0x00, 0xFF, 0x30, 0x82, 0x01, 0x22, 0x00, 0x00};
  unsigned char buf[2048];
  char path[512];
  size_t n;

  switch (kind) {
    case JUNK_TEXT:
      write_named(dir, "junk.pem", "not a key\n", 10);
      break;
    case JUNK_EMPTY:
      write_named(dir, "empty.pem", "", 0);
      break;
    case JUNK_TRUNCATED_PEM:
      join_path(path, sizeof path, g_dir, "r1.pem");
      n = read_file(path, buf, sizeof buf);
      write_named(dir, "cut.pem", buf, n / 2);
      break;
    case JUNK_BINARY:
      write_named(dir, "bin.pem", bin, sizeof bin);
      break;
    case JUNK_SUBDIR:
      join_path(path, sizeof path, dir, "sub.pem");
      ck_assert_int_eq(mkdir(path, 0700), 0);
      break;
    default:
      n = read_file(g_priv, buf, sizeof buf);
      write_named(dir, "private.pem", buf, n);
      break;
  }
}

static void make_subdir(const char *name, char *out, size_t cap) {
  join_path(out, cap, g_dir, name);
  ck_assert_int_eq(mkdir(out, 0700), 0);
}

START_TEST(start_and_reload_skip_unusable_entries) {
  const unsigned pids[] = {0x0100};
  biss_ca_engine_cfg_t cfg = base_cfg(pids, 1);
  biss_ca_engine_t *e;

  add_junk(g_dir, _i);
  e = biss_ca_engine_start(&cfg);
  ck_assert_ptr_nonnull(e);
  ck_assert_uint_eq(biss_ca_engine_receiver_count(e), 2u);
  ck_assert_int_eq(biss_ca_engine_reload_receivers(e), 0);
  ck_assert_uint_eq(biss_ca_engine_receiver_count(e), 2u);
  biss_ca_engine_stop(e);
}
END_TEST

START_TEST(start_rejects_dir_with_only_unusable_entries) {
  const unsigned pids[] = {0x0100};
  biss_ca_engine_cfg_t cfg = base_cfg(pids, 1);
  char sub[512];

  make_subdir("only_junk", sub, sizeof sub);
  add_junk(sub, _i);
  cfg.receivers_dir = sub;
  ck_assert_ptr_null(biss_ca_engine_start(&cfg));
}
END_TEST

START_TEST(start_rejects_missing_receivers_dir) {
  const unsigned pids[] = {0x0100};
  biss_ca_engine_cfg_t cfg = base_cfg(pids, 1);
  char missing[512];

  join_path(missing, sizeof missing, g_dir, "missing");
  cfg.receivers_dir = missing;
  ck_assert_ptr_null(biss_ca_engine_start(&cfg));
}
END_TEST

START_TEST(start_splits_receivers_over_emm_sections) {
  const unsigned pids[] = {0x0100};
  biss_ca_engine_cfg_t cfg = base_cfg(pids, 1);
  biss_ca_engine_t *e;
  biss_ca_emm_parsed_t emm;
  unsigned char sec[4096];
  size_t len = 0;
  char sub[512];
  char name[32];

  make_subdir("many", sub, sizeof sub);
  for (int i = 0; i < MAX_RECEIVERS + 5; i++) {
    snprintf(name, sizeof name, "k%02d.pem", i);
    write_receiver_key(sub, name);
  }
  cfg.receivers_dir = sub;
  e = biss_ca_engine_start(&cfg);
  ck_assert_ptr_nonnull(e);
  ck_assert_uint_eq(biss_ca_engine_receiver_count(e), (size_t)MAX_RECEIVERS + 5);

  ck_assert_int_eq(biss_ca_engine_emm_due(e, 1.0, sec, sizeof sec, &len), 0);
  ck_assert_int_eq(biss_ca_parse_emm_section(sec, len, &emm), 0);
  ck_assert_uint_eq(emm.n_entries, (size_t)MAX_RECEIVERS);
  ck_assert_uint_eq(sec[6], 0u);
  ck_assert_uint_eq(sec[7], 1u);

  ck_assert_int_eq(biss_ca_engine_emm_due(e, 1.0, sec, sizeof sec, &len), 0);
  ck_assert_int_eq(biss_ca_parse_emm_section(sec, len, &emm), 0);
  ck_assert_uint_eq(emm.n_entries, 5u);
  ck_assert_uint_eq(sec[6], 1u);
  ck_assert_uint_eq(sec[7], 1u);

  ck_assert_int_eq(biss_ca_engine_emm_due(e, 1.0, sec, sizeof sec, &len), -1);
  ck_assert_int_eq(biss_ca_engine_emm_due(e, 1.5, sec, sizeof sec, &len), 0);
  ck_assert_uint_eq(sec[6], 0u);
  biss_ca_engine_stop(e);
}
END_TEST

START_TEST(reload_shrinking_receiver_set_shrinks_emm_sections) {
  const unsigned pids[] = {0x0100};
  biss_ca_engine_cfg_t cfg = base_cfg(pids, 1);
  biss_ca_engine_t *e;
  unsigned char sec[4096];
  size_t len = 0;
  char sub[512];
  char name[32];
  char path[600];

  make_subdir("shrink", sub, sizeof sub);
  for (int i = 0; i < MAX_RECEIVERS + 5; i++) {
    snprintf(name, sizeof name, "k%02d.pem", i);
    write_receiver_key(sub, name);
  }
  cfg.receivers_dir = sub;
  e = biss_ca_engine_start(&cfg);
  ck_assert_ptr_nonnull(e);
  ck_assert_int_eq(biss_ca_engine_emm_due(e, 1.0, sec, sizeof sec, &len), 0);
  ck_assert_uint_eq(sec[7], 1u);

  for (int i = 0; i < 10; i++) {
    snprintf(path, sizeof path, "%s/k%02d.pem", sub, i);
    ck_assert_int_eq(unlink(path), 0);
  }
  ck_assert_int_eq(biss_ca_engine_reload_receivers(e), 1);
  ck_assert_uint_eq(biss_ca_engine_receiver_count(e), 10u);
  ck_assert_int_eq(biss_ca_engine_emm_due(e, 2.0, sec, sizeof sec, &len), 0);
  ck_assert_uint_eq(sec[6], 0u);
  ck_assert_uint_eq(sec[7], 0u);
  ck_assert_int_eq(biss_ca_engine_emm_due(e, 2.0, sec, sizeof sec, &len), -1);
  biss_ca_engine_stop(e);
}
END_TEST

START_TEST(multi_key_pem_file_yields_every_receiver) {
  const unsigned pids[] = {0x0100};
  biss_ca_engine_cfg_t cfg = base_cfg(pids, 1);
  biss_ca_engine_t *e;
  char sub[512];
  char cmd[1200];

  make_subdir("multi", sub, sizeof sub);
  snprintf(cmd, sizeof cmd, "cat %s/r1.pem %s/r2.pem > %s/both.pem", g_dir, g_dir, sub);
  ck_assert_int_eq(system(cmd), 0);
  cfg.receivers_dir = sub;
  e = biss_ca_engine_start(&cfg);
  ck_assert_ptr_nonnull(e);
  ck_assert_uint_eq(biss_ca_engine_receiver_count(e), 2u);
  biss_ca_engine_stop(e);
}
END_TEST

START_TEST(reload_keeps_previous_list_when_no_usable_key_remains) {
  const unsigned pids[] = {0x0100};
  biss_ca_engine_cfg_t cfg = base_cfg(pids, 1);
  biss_ca_engine_t *e = biss_ca_engine_start(&cfg);

  ck_assert_ptr_nonnull(e);
  write_named(g_dir, "r1.pem", "garbage\n", 8);
  write_named(g_dir, "r2.pem", "", 0);
  ck_assert_int_eq(biss_ca_engine_reload_receivers(e), -1);
  ck_assert_uint_eq(biss_ca_engine_receiver_count(e), 2u);

  write_receiver_key(g_dir, "r1.pem");
  ck_assert_int_eq(biss_ca_engine_reload_receivers(e), 1);
  ck_assert_uint_eq(biss_ca_engine_receiver_count(e), 1u);
  biss_ca_engine_stop(e);
}
END_TEST

START_TEST(reload_dedupes_receiver_replaced_by_duplicate) {
  const unsigned pids[] = {0x0100};
  biss_ca_engine_cfg_t cfg = base_cfg(pids, 1);
  biss_ca_engine_t *e = biss_ca_engine_start(&cfg);
  biss_ca_emm_parsed_t emm;
  unsigned char sec[4096];
  size_t len = 0;

  ck_assert_ptr_nonnull(e);
  copy_named(g_dir, "r1.pem", g_dir, "r2.pem");
  ck_assert_int_eq(biss_ca_engine_reload_receivers(e), 1);
  ck_assert_uint_eq(biss_ca_engine_receiver_count(e), 1u);
  ck_assert_int_eq(biss_ca_engine_emm_due(e, 1.0, sec, sizeof sec, &len), 0);
  ck_assert_int_eq(biss_ca_parse_emm_section(sec, len, &emm), 0);
  ck_assert_uint_eq(emm.n_entries, 1u);
  biss_ca_engine_stop(e);
}
END_TEST

typedef struct {
  unsigned char sk[BISS_CA_SK_LEN];
  int parity;
  unsigned char sw_even[BISS_CA_SW_LEN];
  unsigned char sw_odd[BISS_CA_SW_LEN];
  unsigned ecm_version;
} engine_state_t;

static void read_state(biss_ca_engine_t *e, double now, engine_state_t *st) {
  unsigned char ekid[BISS_CA_EKID_LEN];
  unsigned char sec[4096];
  unsigned char plain[BISS_CA_SESSION_DATA_MAX];
  size_t len = 0;
  size_t plain_len = 0;
  char path[512];
  biss_ca_key_t *pub;
  biss_ca_key_t *priv;
  biss_ca_emm_parsed_t emm;
  biss_ca_ecm_parsed_t ecm;
  const unsigned char *entry;

  join_path(path, sizeof path, g_dir, "r1.pem");
  pub = biss_ca_key_load_public_file(path);
  ck_assert_ptr_nonnull(pub);
  ck_assert_int_eq(biss_ca_entitlement_key_id(pub, ekid), 0);
  biss_ca_key_free(pub);
  priv = biss_ca_key_load_private_file(g_priv);
  ck_assert_ptr_nonnull(priv);

  ck_assert_int_eq(biss_ca_engine_emm_due(e, now, sec, sizeof sec, &len), 0);
  ck_assert_int_eq(biss_ca_parse_emm_section(sec, len, &emm), 0);
  ck_assert_uint_eq(emm.esid, 0x1234u);
  ck_assert_uint_eq(emm.onid, 1u);
  entry = biss_ca_emm_find_entry(&emm, ekid);
  ck_assert_ptr_nonnull(entry);
  ck_assert_int_eq(biss_ca_rsa_decrypt(priv, entry, plain, sizeof plain, &plain_len), 0);
  biss_ca_key_free(priv);
  ck_assert_int_eq(biss_ca_parse_session_data(plain, plain_len, st->sk, &st->parity), 0);

  ck_assert_int_eq(biss_ca_engine_ecm_due(e, now, sec, sizeof sec, &len), 0);
  ck_assert_int_eq(biss_ca_parse_ecm_section(sec, len, &ecm), 0);
  ck_assert_uint_eq(ecm.esid, 0x1234u);
  ck_assert_uint_eq(ecm.onid, 1u);
  ck_assert_int_eq(ecm.session_key_parity, st->parity);
  st->ecm_version = (sec[5] >> 1) & 0x1F;
  ck_assert_int_eq(biss_ca_aes_cbc_decrypt(st->sk, ecm.iv, ecm.esw_even, st->sw_even), 0);
  ck_assert_int_eq(biss_ca_aes_cbc_decrypt(st->sk, ecm.iv, ecm.esw_odd, st->sw_odd), 0);
}

static void scramble_one(biss_ca_engine_t *e, double now) {
  unsigned char pkt[188];

  memset(pkt, 0, sizeof pkt);
  pkt[0] = 0x47;
  biss_ca_engine_scramble_packet(e, 0x0100, now, pkt, noop_emit, NULL);
}

START_TEST(ecm_and_emm_decrypt_consistently) {
  const unsigned pids[] = {0x0100};
  biss_ca_engine_cfg_t cfg = base_cfg(pids, 1);
  biss_ca_engine_t *e = biss_ca_engine_start(&cfg);
  engine_state_t st;

  ck_assert_ptr_nonnull(e);
  read_state(e, 1.0, &st);
  ck_assert_mem_ne(st.sw_even, st.sw_odd, BISS_CA_SW_LEN);
  biss_ca_engine_stop(e);
}
END_TEST

START_TEST(sk_rotation_changes_sk_and_keeps_session_words) {
  const unsigned pids[] = {0x0100};
  biss_ca_engine_cfg_t cfg = base_cfg(pids, 1);
  biss_ca_engine_t *e = biss_ca_engine_start(&cfg);
  engine_state_t before;
  engine_state_t after;

  ck_assert_ptr_nonnull(e);
  read_state(e, 1.0, &before);
  biss_ca_engine_force_sk_rotation(e);
  scramble_one(e, 2.0);
  read_state(e, 3.0, &after);

  ck_assert_mem_ne(before.sk, after.sk, BISS_CA_SK_LEN);
  ck_assert_int_ne(before.parity, after.parity);
  ck_assert_mem_eq(before.sw_even, after.sw_even, BISS_CA_SW_LEN);
  ck_assert_mem_eq(before.sw_odd, after.sw_odd, BISS_CA_SW_LEN);
  biss_ca_engine_stop(e);
}
END_TEST

START_TEST(repeated_rotations_keep_ecm_consistent_and_bump_version) {
  const unsigned pids[] = {0x0100};
  biss_ca_engine_cfg_t cfg = base_cfg(pids, 1);
  biss_ca_engine_t *e = biss_ca_engine_start(&cfg);
  engine_state_t prev;
  engine_state_t cur;

  ck_assert_ptr_nonnull(e);
  read_state(e, 0.5, &prev);
  for (int i = 0; i < 40; i++) {
    int odd_rotated = (i + 1) & 1;
    int sk_rotated = ((i + 1) % 6) == 0;

    biss_ca_engine_clock_tick(e, 1000);
    scramble_one(e, 1.0 + i);
    read_state(e, 1.0 + i, &cur);

    ck_assert_uint_eq(cur.ecm_version, (prev.ecm_version + 1) & 0x1F);
    if (sk_rotated) {
      ck_assert_mem_ne(cur.sk, prev.sk, BISS_CA_SK_LEN);
      ck_assert_int_ne(cur.parity, prev.parity);
    } else {
      ck_assert_mem_eq(cur.sk, prev.sk, BISS_CA_SK_LEN);
      ck_assert_int_eq(cur.parity, prev.parity);
    }
    if (odd_rotated) {
      ck_assert_mem_eq(cur.sw_even, prev.sw_even, BISS_CA_SW_LEN);
      ck_assert_mem_ne(cur.sw_odd, prev.sw_odd, BISS_CA_SW_LEN);
    } else {
      ck_assert_mem_ne(cur.sw_even, prev.sw_even, BISS_CA_SW_LEN);
      ck_assert_mem_eq(cur.sw_odd, prev.sw_odd, BISS_CA_SW_LEN);
    }
    prev = cur;
  }
  biss_ca_engine_stop(e);
}
END_TEST

static Suite *biss_ca_engine_suite(void) {
  Suite *s = suite_create("biss_ca_engine");
  TCase *tc = tcase_create("core");
  tcase_add_checked_fixture(tc, setup, teardown);
  tcase_add_test(tc, start_loads_receivers_and_stops_cleanly);
  tcase_add_test(tc, start_rejects_empty_receivers_dir);
  tcase_add_test(tc, start_rejects_short_sw_period);
  tcase_add_test(tc, ecm_and_emm_become_due_and_repeat_rate_limited);
  tcase_add_test(tc, sw_rotation_changes_ecm_content);
  tcase_add_test(tc, prog_desc_and_cat_build_nonempty);
  tcase_add_test(tc, reload_detects_added_and_removed_receiver);
  tcase_add_test(tc, force_sk_rotation_makes_next_scramble_rotate);
  tcase_add_loop_test(tc, start_and_reload_skip_unusable_entries, 0, JUNK_KIND_COUNT);
  tcase_add_loop_test(tc, start_rejects_dir_with_only_unusable_entries, 0, JUNK_KIND_COUNT);
  tcase_add_test(tc, start_rejects_missing_receivers_dir);
  tcase_add_test(tc, start_splits_receivers_over_emm_sections);
  tcase_add_test(tc, reload_shrinking_receiver_set_shrinks_emm_sections);
  tcase_add_test(tc, multi_key_pem_file_yields_every_receiver);
  tcase_add_test(tc, reload_keeps_previous_list_when_no_usable_key_remains);
  tcase_add_test(tc, reload_dedupes_receiver_replaced_by_duplicate);
  tcase_add_test(tc, ecm_and_emm_decrypt_consistently);
  tcase_add_test(tc, sk_rotation_changes_sk_and_keeps_session_words);
  tcase_add_test(tc, repeated_rotations_keep_ecm_consistent_and_bump_version);
  suite_add_tcase(s, tc);
  return s;
}

int main(void) {
  SRunner *sr = srunner_create(biss_ca_engine_suite());
  srunner_run_all(sr, CK_NORMAL);
  int failed = srunner_ntests_failed(sr);
  srunner_free(sr);
  return failed == 0 ? EXIT_SUCCESS : EXIT_FAILURE;
}
