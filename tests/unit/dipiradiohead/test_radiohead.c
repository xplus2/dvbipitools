/* Copyright 2026 dvbipitools authors. Licensed under GPL-3.0-or-later.
 * See NOTICE and LICENSE for details and authorship information. */

#include <arpa/inet.h>
#include <check.h>
#include <poll.h>
#include <pthread.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/socket.h>
#include <sys/un.h>
#include <unistd.h>

#include "lib/cas/cas_core.h"
#include "lib/sys/ioutil.h"
#include "lib/sys/signal.h"

#include "dipiradiohead/radiohead/priv.h"
#include "input/http_fixture.h"
#include "../run_helper.h"

static void make_packet(unsigned char pkt[188], unsigned char marker) {
  memset(pkt, 0xAB, 188);
  pkt[0] = 0x47;
  pkt[1] = 0x00;
  pkt[2] = marker;
  pkt[3] = 0x10;
}

START_TEST(meta_cb_copies_artist_and_title_and_marks_dirty) {
  meta_state_t m;
  radio_metrics_t rm;
  memset(&m, 0, sizeof m);
  memset(&rm, 0, sizeof rm);
  m.rm = &rm;

  meta_cb(&m, "The Artist", "The Title");
  ck_assert_str_eq(m.artist, "The Artist");
  ck_assert_str_eq(m.title, "The Title");
  ck_assert_int_eq(m.dirty, 1);
  ck_assert_uint_eq(rm.metadata_updates_total, 1u);
}
END_TEST

START_TEST(meta_cb_tolerates_null_metrics) {
  meta_state_t m;
  memset(&m, 0, sizeof m);
  m.rm = NULL;
  meta_cb(&m, "A", "B");
  ck_assert_int_eq(m.dirty, 1);
}
END_TEST

START_TEST(source_codec_name_maps_every_known_codec) {
  ck_assert_str_eq(source_codec_name(SRC_MPEG_AUDIO), "mpeg-audio");
  ck_assert_str_eq(source_codec_name(SRC_AAC_ADTS), "aac-adts");
  ck_assert_str_eq(source_codec_name(SRC_AAC_LATM), "aac-latm");
}
END_TEST

START_TEST(packet_cb_batches_until_ts_per_dgram_then_flushes) {
  mcast_t *send = mcast_open_send(AF_INET, run_helper_group_n(61), run_helper_port(61), NULL, 1);
  mcast_t *recv = mcast_open(AF_INET, run_helper_group_n(61), run_helper_port(61), NULL, 500);
  out_ctx_t o;
  unsigned char pkt[188];
  unsigned char rbuf[4096];
  int i;
  ssize_t n;

  ck_assert_ptr_nonnull(send);
  ck_assert_ptr_nonnull(recv);
  memset(&o, 0, sizeof o);
  o.mc = send;

  for (i = 0; i < TS_PER_DGRAM - 1; i++) {
    make_packet(pkt, (unsigned char)i);
    packet_cb(&o, pkt);
  }
  ck_assert_int_eq(o.batch_count, TS_PER_DGRAM - 1);

  make_packet(pkt, (unsigned char)(TS_PER_DGRAM - 1));
  packet_cb(&o, pkt);
  ck_assert_int_eq(o.batch_count, 0);
  ck_assert_uint_eq(o.packets, (unsigned)TS_PER_DGRAM);
  ck_assert_int_eq(o.mc_had_error, 0);

  n = mcast_recv(recv, rbuf, sizeof rbuf, NULL);
  ck_assert_int_eq(n, TS_PER_DGRAM * 188);
  for (i = 0; i < TS_PER_DGRAM; i++) ck_assert_uint_eq(rbuf[i * 188 + 2], (unsigned char)i);
  mcast_close(send);
  mcast_close(recv);
}
END_TEST

START_TEST(flush_batch_is_a_no_op_when_empty) {
  mcast_t *send = mcast_open_send(AF_INET, run_helper_group_n(62), run_helper_port(62), NULL, 1);
  out_ctx_t o;
  ck_assert_ptr_nonnull(send);
  memset(&o, 0, sizeof o);
  o.mc = send;
  flush_batch(&o);
  ck_assert_int_eq(o.batch_count, 0);
  ck_assert_uint_eq(o.packets, 0u);
  ck_assert_int_eq(o.mc_had_error, 0);

  mcast_close(send);
}
END_TEST

START_TEST(flush_batch_prefixes_rtp_header_when_rtp_enabled) {
  mcast_t *send = mcast_open_send(AF_INET, run_helper_group_n(63), run_helper_port(63), NULL, 1);
  mcast_t *recv = mcast_open(AF_INET, run_helper_group_n(63), run_helper_port(63), NULL, 500);
  rtpheader_t *rtph = rtpheader_new();
  out_ctx_t o;
  unsigned char pkt[188];
  unsigned char rbuf[4096];
  ssize_t n;
  int i;

  ck_assert_ptr_nonnull(send);
  ck_assert_ptr_nonnull(recv);
  ck_assert_ptr_nonnull(rtph);
  memset(&o, 0, sizeof o);
  o.mc = send;
  o.rtp = 1;
  o.rtph = rtph;
  o.cur_pts = 12345;

  for (i = 0; i < TS_PER_DGRAM; i++) {
    make_packet(pkt, (unsigned char)i);
    packet_cb(&o, pkt);
  }
  ck_assert_int_eq(o.batch_count, 0);

  n = mcast_recv(recv, rbuf, sizeof rbuf, NULL);
  ck_assert_int_eq(n, 12 + TS_PER_DGRAM * 188);
  ck_assert_int_eq(rbuf[0] >> 6, 2); /* RTP version 2 */
  ck_assert_uint_eq(rbuf[12], 0x47);

  rtpheader_free(rtph);
  mcast_close(send);
  mcast_close(recv);
}
END_TEST

#define SEEN_MAX 128

typedef struct {
  metrics_id_t id;
  char label[METRICS_LABEL_MAX + 1];
  uint64_t value;
} seen_t;

typedef struct {
  char dir[64];
  char path[96];
  int fd;
  seen_t seen[SEEN_MAX];
  int n_seen;
} collector_t;

static void collector_open(collector_t *c) {
  struct sockaddr_un addr;

  memset(c, 0, sizeof *c);
  bufcpy(c->dir, sizeof c->dir, "/tmp/dipiradiohead_mx_XXXXXX");
  ck_assert_ptr_nonnull(mkdtemp(c->dir));
  snprintf(c->path, sizeof c->path, "%s/mx.sock", c->dir);
  c->fd = socket(AF_UNIX, SOCK_DGRAM | SOCK_NONBLOCK, 0);
  ck_assert_int_ge(c->fd, 0);
  memset(&addr, 0, sizeof addr);
  addr.sun_family = AF_UNIX;
  bufcpy(addr.sun_path, sizeof addr.sun_path, c->path);
  ck_assert_int_eq(bind(c->fd, (struct sockaddr *)&addr, sizeof addr), 0);
}

static void collector_close(const collector_t *c) {
  close(c->fd);
  unlink(c->path);
  rmdir(c->dir);
}

static int collector_drain(collector_t *c) {
  unsigned char buf[METRICS_MAX_SNAPSHOT_BYTES];
  int datagrams = 0;
  ssize_t n;

  c->n_seen = 0;
  while ((n = recv(c->fd, buf, sizeof buf, 0)) > 0) {
    metrics_reader_t r;
    metrics_hdr_t hdr;
    seen_t e;

    datagrams++;
    ck_assert_int_eq(metrics_reader_init(&r, buf, (size_t)n, &hdr), 0);
    while (metrics_reader_next(&r, &e.id, e.label, sizeof e.label, &e.value) == 1)
      if (c->n_seen < SEEN_MAX) c->seen[c->n_seen++] = e;
  }
  return datagrams;
}

static int seen_count(const collector_t *c, metrics_id_t id) {
  int n = 0;

  for (int i = 0; i < c->n_seen; i++)
    if (c->seen[i].id == id) n++;
  return n;
}

static int seen_value(const collector_t *c, metrics_id_t id, const char *label, uint64_t *value) {
  for (int i = 0; i < c->n_seen; i++) {
    if (c->seen[i].id == id && strcmp(c->seen[i].label, label) == 0) {
      *value = c->seen[i].value;
      return 1;
    }
  }
  return 0;
}

static cas_t *start_test_cas(config_t *cfg, unsigned n_vendors) {
  static unsigned pids[] = {0x0100};

  memset(cfg, 0, sizeof *cfg);
  cfg->cas_algo = CAS_ALGO_CSA2;
  cfg->cas_cp_duration_ms = 10000;
  cfg->n_cas_vendors = n_vendors;
  for (unsigned i = 0; i < n_vendors; i++) {
    cas_vendor_t *v = &cfg->cas_vendors[i];

    strcpy(v->ecmg_host, "127.0.0.1");
    v->ecmg_port = 1;
    v->super_cas_id = ((0x4A75u + i) << 16) | 0x0001u;
    v->ecm_id = 1;
    v->ecm_pid = 0x1FF0 + 2 * i;
    v->emm_pid = 0x1FF1 + 2 * i;
  }
  return cas_start(cfg, pids, 1);
}

START_TEST(emit_metrics_reports_output_service_and_radio_counters) {
  collector_t col;
  metrics_exporter_t mx;
  out_ctx_t out;
  radio_metrics_t rm;
  input_metrics_t im[1];
  uint64_t v = 0;

  collector_open(&col);
  metrics_exporter_init(&mx, METRICS_COMPONENT_RADIOHEAD, "rh", col.path, 5.0);
  memset(&out, 0, sizeof out);
  memset(&rm, 0, sizeof rm);
  memset(im, 0, sizeof im);
  out.packets = 10;
  out.errors = 2;
  rm.frames_total[SRC_MPEG_AUDIO] = 11;
  rm.frames_total[SRC_AAC_ADTS] = 12;
  rm.frames_total[SRC_AAC_LATM] = 13;
  rm.framing_errors_total = 4;
  rm.metadata_updates_total = 5;
  im[0].up = 1;

  emit_metrics(&mx, 100.0, &out, 3, 2, im, 1, &rm, NULL);
  ck_assert_int_eq(collector_drain(&col), 1);
  ck_assert(seen_value(&col, METRICS_ID_OUTPUT_PACKETS_TOTAL, "", &v));
  ck_assert_uint_eq(v, 10u);
  ck_assert(seen_value(&col, METRICS_ID_OUTPUT_BYTES_TOTAL, "", &v));
  ck_assert_uint_eq(v, 1880u);
  ck_assert(seen_value(&col, METRICS_ID_OUTPUT_ERRORS_TOTAL, "", &v));
  ck_assert_uint_eq(v, 2u);
  ck_assert(seen_value(&col, METRICS_ID_CONFIGURED_SERVICES, "", &v));
  ck_assert_uint_eq(v, 3u);
  ck_assert(seen_value(&col, METRICS_ID_ACTIVE_SERVICES, "", &v));
  ck_assert_uint_eq(v, 2u);
  ck_assert(seen_value(&col, METRICS_ID_RADIO_AUDIO_FRAMING_ERRORS_TOTAL, "", &v));
  ck_assert_uint_eq(v, 4u);
  ck_assert(seen_value(&col, METRICS_ID_RADIO_METADATA_UPDATES_TOTAL, "", &v));
  ck_assert_uint_eq(v, 5u);
  for (unsigned codec = 0; codec <= SRC_AAC_LATM; codec++) {
    ck_assert(seen_value(&col, METRICS_ID_RADIO_AUDIO_FRAMES_TOTAL, source_codec_name((source_codec_t)codec), &v));
    ck_assert_uint_eq(v, 11u + codec);
  }
  ck_assert(seen_value(&col, METRICS_ID_INPUT_UP, "i0", &v));
  ck_assert_uint_eq(v, 1u);
  ck_assert_int_eq(seen_count(&col, METRICS_ID_CAS_SCRAMBLED_PACKETS_TOTAL), 0);

  emit_metrics(&mx, 101.0, &out, 3, 2, im, 1, &rm, NULL);
  ck_assert_int_eq(collector_drain(&col), 0);
  emit_metrics(&mx, 105.0, &out, 3, 2, im, 1, &rm, NULL);
  ck_assert_int_eq(collector_drain(&col), 1);

  metrics_exporter_close(&mx);
  collector_close(&col);
}
END_TEST

START_TEST(emit_metrics_adds_one_labeled_block_per_cas_vendor) {
  collector_t col;
  metrics_exporter_t mx;
  out_ctx_t out;
  radio_metrics_t rm;
  config_t cas_cfg;
  cas_t *cas = start_test_cas(&cas_cfg, 2);
  uint64_t v = 1;
  char label[16];

  ck_assert_ptr_nonnull(cas);
  collector_open(&col);
  metrics_exporter_init(&mx, METRICS_COMPONENT_RADIOHEAD, "rh", col.path, 5.0);
  memset(&out, 0, sizeof out);
  memset(&rm, 0, sizeof rm);

  emit_metrics(&mx, 100.0, &out, 1, 1, NULL, 0, &rm, cas);
  ck_assert_int_ge(collector_drain(&col), 1);
  ck_assert(seen_value(&col, METRICS_ID_CAS_SCRAMBLED_PACKETS_TOTAL, "", &v));
  ck_assert_uint_eq(v, 0u);
  ck_assert(seen_value(&col, METRICS_ID_CAS_UNEXPECTED_CLEAR_PACKETS_TOTAL, "", &v));
  ck_assert_int_eq(seen_count(&col, METRICS_ID_CAS_ECMG_CONNECTED), 2);
  ck_assert_int_eq(seen_count(&col, METRICS_ID_CAS_ECM_TOTAL), 2);
  ck_assert_int_eq(seen_count(&col, METRICS_ID_CAS_EMM_TOTAL), 2);
  ck_assert_int_eq(seen_count(&col, METRICS_ID_CAS_EMMG_CLIENTS), 2);
  for (unsigned i = 0; i < 2; i++) {
    cas_core_format_super_cas_id(cas_vendor_super_cas_id(cas, i), label);
    v = 1;
    ck_assert_msg(seen_value(&col, METRICS_ID_CAS_ECMG_CONNECTED, label, &v), "vendor %u label '%s' missing", i, label);
    ck_assert_uint_eq(v, 0u);
  }

  metrics_exporter_close(&mx);
  collector_close(&col);
  cas_stop(cas);
}
END_TEST

START_TEST(timeline_stays_monotonic_across_a_sample_rate_change) {
  uint64_t t = 0;
  uint64_t before;

  for (int i = 0; i < 100; i++) timeline_add(&t, 1152, 44100);
  before = timeline_pts(t);
  timeline_add(&t, 1024, 48000);
  ck_assert_uint_gt(timeline_pts(t), before);
  ck_assert_uint_le(llabs((long long)(timeline_pts(t) - before) - 1920), 1);
}
END_TEST

START_TEST(emit_metrics_is_a_no_op_for_a_disabled_exporter) {
  metrics_exporter_t mx;
  out_ctx_t out;
  radio_metrics_t rm;

  metrics_exporter_init(&mx, METRICS_COMPONENT_RADIOHEAD, NULL, NULL, 0);
  memset(&out, 0, sizeof out);
  memset(&rm, 0, sizeof rm);
  emit_metrics(&mx, 100.0, &out, 1, 1, NULL, 0, &rm, NULL);
  ck_assert_int_eq(metrics_exporter_enabled(&mx), 0);
  ck_assert_uint_eq(mx.sequence, 0u);
}
END_TEST

START_TEST(mpts_cas_relay_helpers_forward_to_the_cas) {
  config_t cas_cfg;
  cas_t *cas = start_test_cas(&cas_cfg, 2);
  unsigned char via_relay[512];
  unsigned char direct[512];
  size_t via_len;
  size_t direct_len;
  size_t out_len = 99;

  ck_assert_ptr_nonnull(cas);
  via_len = mpts_cas_build_cat(cas, via_relay, sizeof via_relay);
  direct_len = cas_build_cat(cas, direct, sizeof direct);
  ck_assert_uint_gt(direct_len, 0u);
  ck_assert_uint_eq(via_len, direct_len);
  ck_assert_mem_eq(via_relay, direct, direct_len);
  ck_assert_uint_eq(mpts_cas_build_cat(cas, via_relay, 3), 0u);
  for (size_t i = 0; i < 2; i++) {
    ck_assert_int_eq(mpts_cas_ecm_due(cas, i, 1.0, via_relay, sizeof via_relay, &out_len), -1);
    ck_assert_int_eq(mpts_cas_next_emm(cas, i, via_relay, sizeof via_relay, &out_len), -1);
  }
  ck_assert_uint_eq(out_len, 99u);
  cas_stop(cas);
}
END_TEST

static unsigned char g_cat_pkt[188];
static int g_cat_seen;

static void catch_cat_cb(void *ctx, const unsigned char *pkt) {
  (void)ctx;
  if ((((unsigned)pkt[1] & 0x1F) << 8 | pkt[2]) == 0x0001 && !g_cat_seen) {
    memcpy(g_cat_pkt, pkt, 188);
    g_cat_seen = 1;
  }
}

START_TEST(radiohead_mpts_set_cas_makes_the_mux_send_the_cas_cat) {
  static const mpts_program_ops_t ops = {NULL, NULL, NULL};
  psi_pat_entry_t entries[1] = {{1, 0x0100}};
  config_t cas_cfg;
  cas_t *cas = start_test_cas(&cas_cfg, 2);
  mpts_t *m = mpts_new(1, 1, "", entries, 1, &ops);
  unsigned char cat[512];
  size_t cat_len;

  ck_assert_ptr_nonnull(cas);
  ck_assert_ptr_nonnull(m);
  g_cat_seen = 0;
  mpts_tick(m, 1.0, catch_cat_cb, NULL);
  ck_assert_int_eq(g_cat_seen, 0);

  radiohead_mpts_set_cas(m, cas);
  mpts_tick(m, 2.0, catch_cat_cb, NULL);
  ck_assert_int_eq(g_cat_seen, 1);
  cat_len = cas_build_cat(cas, cat, sizeof cat);
  ck_assert_uint_gt(cat_len, 0u);
  ck_assert_mem_eq(g_cat_pkt + 188 - cat_len, cat, cat_len);
  mpts_free(m);
  cas_stop(cas);
}
END_TEST

typedef struct {
  const char *name;
  int mcast;
  int rtp;
  int al_fec;
  unsigned n_rist;
  unsigned n_srt;
  int want_rc;
} output_case_t;

static const output_case_t output_cases[] = {
    {"no output configured", 0, 0, 0, 0, 0, 0},
    {"plain multicast", 1, 0, 0, 0, 0, 0},
    {"multicast with rtp", 1, 1, 0, 0, 0, 0},
    {"multicast with rtp and al-fec", 1, 1, 1, 0, 0, 0},
    {"rist peers without librist", 1, 0, 0, 1, 0, -1},
    {"srt peers without libsrt", 1, 0, 0, 0, 1, -1},
    {"rist peers only", 0, 0, 0, 1, 0, -1},
};

START_TEST(output_open_builds_the_configured_senders_and_close_releases_them) {
  const output_case_t *c = &output_cases[_i];
  config_t cfg;
  out_ctx_t o;
  int rc;

  memset(&cfg, 0, sizeof cfg);
  memset(&o, 0, sizeof o);
  cfg.family = AF_INET;
  cfg.ttl = 1;
  cfg.n_inputs = 1;
  if (c->mcast) {
    bufcpy(cfg.mcast_group, sizeof cfg.mcast_group, "239.7.9.70");
    cfg.mcast_port = 15370;
  }
  cfg.rtp = c->rtp;
  if (c->al_fec) {
    cfg.al_fec_l = 5;
    cfg.al_fec_d = 5;
    cfg.al_fec_port = 15372;
  }
  cfg.n_rist = c->n_rist;
  cfg.n_srt = c->n_srt;

  rc = radiohead_output_open(&cfg, &o);
  ck_assert_msg(rc == c->want_rc, "%s: rc %d", c->name, rc);
  ck_assert_msg((o.mc != NULL) == (c->mcast != 0), "%s: mc %p", c->name, (void *)o.mc);
  ck_assert_msg((o.rtph != NULL) == (c->mcast && c->rtp), "%s: rtph %p", c->name, (void *)o.rtph);
  ck_assert_msg((o.fec_enc != NULL) == (c->mcast && c->rtp && c->al_fec && rc == 0), "%s: fec_enc %p", c->name, (void *)o.fec_enc);
  ck_assert_msg((o.fec_mc != NULL) == (c->mcast && c->rtp && c->al_fec && rc == 0), "%s: fec_mc %p", c->name, (void *)o.fec_mc);
  ck_assert_ptr_null(o.rist);
  ck_assert_ptr_null(o.srt);
  radiohead_output_close(&o);
  ck_assert_ptr_null(o.insp);
}
END_TEST

START_TEST(output_open_creates_the_inspector_only_when_inspection_is_on) {
  config_t cfg;
  out_ctx_t o;

  memset(&cfg, 0, sizeof cfg);
  cfg.n_inputs = 1;
  cfg.metrics_inspect_ts = METRICS_INSPECT_TS_OFF;
  memset(&o, 0, sizeof o);
  ck_assert_int_eq(radiohead_output_open(&cfg, &o), 0);
  ck_assert_ptr_null(o.insp);
  radiohead_output_close(&o);

  cfg.metrics_inspect_ts = METRICS_INSPECT_TS_BASIC;
  memset(&o, 0, sizeof o);
  ck_assert_int_eq(radiohead_output_open(&cfg, &o), 0);
  ck_assert_ptr_nonnull(o.insp);
  radiohead_output_close(&o);
  ck_assert_ptr_null(o.insp);
}
END_TEST

START_TEST(rist_and_srt_open_fail_cleanly_without_the_transport_libraries) {
  config_t cfg;

  memset(&cfg, 0, sizeof cfg);
  cfg.n_rist = 2;
  bufcpy(cfg.rist_uri[0], sizeof cfg.rist_uri[0], "rist://127.0.0.1:15380");
  bufcpy(cfg.rist_uri[1], sizeof cfg.rist_uri[1], "rist://127.0.0.1:15381");
  cfg.rist_profile = RIST_PROF_MAIN;
  ck_assert_ptr_null(radiohead_rist_open(&cfg));

  cfg.n_srt = 2;
  bufcpy(cfg.srt_host[0], sizeof cfg.srt_host[0], "127.0.0.1");
  cfg.srt_port[0] = 15390;
  bufcpy(cfg.srt_host[1], sizeof cfg.srt_host[1], "127.0.0.1");
  cfg.srt_port[1] = 15391;
  cfg.srt_group_mode = SRT_BOND_BACKUP;
  ck_assert_ptr_null(radiohead_srt_open(&cfg));
  cfg.srt_group_mode = SRT_BOND_BROADCAST;
  ck_assert_ptr_null(radiohead_srt_open(&cfg));
  cfg.srt_group_mode = SRT_BOND_NONE;
  ck_assert_ptr_null(radiohead_srt_open(&cfg));
}
END_TEST

START_TEST(srt_service_is_a_no_op_without_a_sink) {
  out_ctx_t o;

  memset(&o, 0, sizeof o);
  o.srt_connected = 1;
  radiohead_srt_service(&o);
  ck_assert_int_eq(o.srt_connected, 1);
}
END_TEST

START_TEST(srt_service_tracks_the_link_state_edge) {
  out_ctx_t o;
  int token = 0;

  memset(&o, 0, sizeof o);
  o.srt = (srtsink_t *)&token;
  o.srt_connected = 1;
  radiohead_srt_service(&o);
  ck_assert_int_eq(o.srt_connected, 0);
  radiohead_srt_service(&o);
  ck_assert_int_eq(o.srt_connected, 0);
  o.srt = NULL;
}
END_TEST

#define SINGLE_FRAMES 20

typedef struct {
  int listen_fd;
  pthread_t th;
  http_fixture_t fx;
  unsigned char resp[24576];
  source_t *src;
  config_t cfg;
  meta_state_t meta;
  out_ctx_t out;
  input_metrics_t im;
  radio_metrics_t rm;
  metrics_exporter_t mx;
  uint64_t timeline;
  double pace_start;
  double pace_deadline;
  double last_stat;
  unsigned long long last_synced;
  tspacketizer_t *tsp;
  single_tick_t tk;
} single_rig_t;

static unsigned single_rig_open(single_rig_t *r, const unsigned char *body, size_t len, int listen_fd) {
  static const char head[] = "HTTP/1.1 200 OK\r\nContent-Type: audio/mpeg\r\nConnection: close\r\n\r\n";
  unsigned port = 0;
  char uri[64];

  memset(r, 0, sizeof *r);
  ck_assert_uint_le(sizeof head - 1 + len, sizeof r->resp);
  memcpy(r->resp, head, sizeof head - 1);
  memcpy(r->resp + sizeof head - 1, body, len);
  if (listen_fd >= 0) {
    struct sockaddr_in addr;
    socklen_t alen = sizeof addr;

    r->listen_fd = listen_fd;
    ck_assert_int_eq(getsockname(listen_fd, (struct sockaddr *)&addr, &alen), 0);
    port = ntohs(addr.sin_port);
  } else {
    r->listen_fd = fixture_listener(&port);
  }
  fixture_single(&r->fx, r->listen_fd, (const char *)r->resp, sizeof head - 1 + len);
  ck_assert_int_eq(pthread_create(&r->th, NULL, fixture_serve, &r->fx), 0);
  snprintf(uri, sizeof uri, "http://127.0.0.1:%u/stream", port);
  r->src = source_open(uri, 0, "test", 0, meta_cb, &r->meta, NULL, NULL);
  ck_assert_ptr_nonnull(r->src);

  r->cfg.n_inputs = 1;
  r->cfg.tsid = 1;
  r->cfg.onid = 1;
  r->cfg.inputs[0].sid = 3;
  bufcpy(r->cfg.inputs[0].sdt_text, sizeof r->cfg.inputs[0].sdt_text, "Radio");
  bufcpy(r->cfg.default_provider_text, sizeof r->cfg.default_provider_text, "Prov");
  r->meta.rm = &r->rm;
  metrics_exporter_init(&r->mx, METRICS_COMPONENT_RADIOHEAD, NULL, NULL, 0);
  r->pace_start = mono_seconds();
  r->pace_deadline = r->pace_start;
  r->tk.tsp = &r->tsp;
  r->tk.cfg = &r->cfg;
  r->tk.meta = &r->meta;
  r->tk.out = &r->out;
  r->tk.im = &r->im;
  r->tk.rm = &r->rm;
  r->tk.metrics_on = 1;
  r->tk.mx = &r->mx;
  r->tk.timeline = &r->timeline;
  r->tk.pace_deadline = &r->pace_deadline;
  r->tk.last_stat = &r->last_stat;
  r->tk.last_synced_bytes = &r->last_synced;
  return port;
}

static void single_rig_close(single_rig_t *r) {
  source_close(r->src);
  pthread_join(r->th, NULL);
  close(r->listen_fd);
  if (r->tsp) tspacketizer_free(r->tsp);
}

static int single_step(single_rig_t *r) {
  struct pollfd pfd;

  pfd.fd = source_fd(r->src);
  pfd.events = POLLIN;
  pfd.revents = 0;
  if (pfd.fd >= 0) poll(&pfd, 1, 50);
  return process_single_frame(&r->tk, r->src);
}

START_TEST(single_frame_creates_the_standalone_program_and_paces_the_stream) {
  static unsigned char body[SINGLE_FRAMES * FIXTURE_MP3_FRAME_LEN];
  single_rig_t *r = calloc(1, sizeof *r);
  double before = mono_seconds();
  unsigned long long frames;

  if (!r) abort();
  fixture_mp3_frames(body, SINGLE_FRAMES);
  single_rig_open(r, body, sizeof body, -1);
  bufcpy(r->meta.artist, sizeof r->meta.artist, "Artist");
  bufcpy(r->meta.title, sizeof r->meta.title, "Title");
  r->meta.dirty = 1;
  for (int i = 0; i < 50 && !r->tsp; i++) ck_assert_int_eq(single_step(r), 0);
  ck_assert_ptr_nonnull(r->tsp);
  frames = r->rm.frames_total[SRC_MPEG_AUDIO];
  ck_assert_uint_ge(frames, 1u);
  ck_assert_uint_le(frames, 13u);
  ck_assert_uint_le(llabs((long long)timeline_pts(r->timeline) - (long long)(frames * 1152u * 90000u / 44100u)), 1);
  ck_assert_double_eq_tol(r->pace_deadline - r->pace_start, (double)(frames * 1152u) / 44100.0, 1e-6);
  ck_assert_int_eq(r->meta.dirty, 0);
  ck_assert_int_eq(tspacketizer_eit_pending(r->tsp), 0);
  ck_assert_uint_ge(r->out.packets, frames + 4);
  ck_assert_uint_gt(r->im.bytes_total, 0u);
  ck_assert(r->im.last_data_time >= before);
  ck_assert_uint_eq(r->rm.framing_errors_total, 0u);
  single_rig_close(r);
  free(r);
}
END_TEST

START_TEST(single_frame_ahead_of_the_pacing_clock_is_deferred) {
  static unsigned char body[SINGLE_FRAMES * FIXTURE_MP3_FRAME_LEN];
  single_rig_t *r = calloc(1, sizeof *r);

  if (!r) abort();
  fixture_mp3_frames(body, SINGLE_FRAMES);
  single_rig_open(r, body, sizeof body, -1);
  r->pace_deadline = mono_seconds() + 10.0;
  ck_assert_int_eq(single_step(r), 0);
  ck_assert_uint_eq(r->rm.frames_total[SRC_MPEG_AUDIO], 0u);
  ck_assert_ptr_null(r->tsp);
  ck_assert_uint_eq(r->out.packets, 0u);
  single_rig_close(r);
  free(r);
}
END_TEST

START_TEST(single_frame_attaches_the_cas_and_keeps_running_while_it_is_healthy) {
  static unsigned char body[SINGLE_FRAMES * FIXTURE_MP3_FRAME_LEN];
  single_rig_t *r = calloc(1, sizeof *r);
  config_t cas_cfg;
  cas_t *cas = start_test_cas(&cas_cfg, 1);

  if (!r) abort();
  ck_assert_ptr_nonnull(cas);
  fixture_mp3_frames(body, SINGLE_FRAMES);
  single_rig_open(r, body, sizeof body, -1);
  r->tk.cas = cas;
  for (int i = 0; i < 50 && !r->tsp; i++) ck_assert_int_eq(single_step(r), 0);
  ck_assert_ptr_nonnull(r->tsp);
  ck_assert_int_eq(cas_failed(cas), 0);
  ck_assert_uint_ge(r->out.packets, r->rm.frames_total[SRC_MPEG_AUDIO]);
  single_rig_close(r);
  cas_stop(cas);
  free(r);
}
END_TEST

START_TEST(single_frame_reports_a_fatal_cas_failure) {
  static unsigned char body[SINGLE_FRAMES * FIXTURE_MP3_FRAME_LEN];
  single_rig_t *r = calloc(1, sizeof *r);
  unsigned port;
  int blocker = fixture_listener(&port);
  cas_t *cas;
  config_t cfg;
  unsigned pids[] = {TSPACKETIZER_PID_AUDIO};
  int rc = 0;

  if (!r) abort();
  memset(&cfg, 0, sizeof cfg);
  cfg.cas_algo = CAS_ALGO_CSA2;
  cfg.cas_cp_duration_ms = 10000;
  cfg.n_cas_vendors = 1;
  strcpy(cfg.cas_vendors[0].ecmg_host, "127.0.0.1");
  cfg.cas_vendors[0].ecmg_port = 1;
  cfg.cas_vendors[0].super_cas_id = 0x4A750001u;
  cfg.cas_vendors[0].ecm_id = 1;
  cfg.cas_vendors[0].ecm_pid = 0x1FF0;
  cfg.cas_vendors[0].emm_pid = 0x1FF1;
  cfg.cas_vendors[0].emmg_port = port;
  cas = cas_start(&cfg, pids, 1);
  ck_assert_ptr_nonnull(cas);

  fixture_mp3_frames(body, SINGLE_FRAMES);
  single_rig_open(r, body, sizeof body, -1);
  r->tk.cas = cas;
  for (int i = 0; i < 50 && rc == 0; i++) rc = single_step(r);
  ck_assert_int_eq(rc, -2);
  ck_assert_int_eq(cas_failed(cas), 1);
  single_rig_close(r);
  cas_stop(cas);
  close(blocker);
  free(r);
}
END_TEST

START_TEST(single_frame_reports_a_source_error_and_counts_the_framing_failure) {
  static unsigned char body[20000];
  single_rig_t *r = calloc(1, sizeof *r);
  int rc = 0;

  if (!r) abort();
  single_rig_open(r, body, sizeof body, -1);
  for (int i = 0; i < 100 && rc == 0; i++) rc = single_step(r);
  ck_assert_int_eq(rc, -1);
  ck_assert_uint_eq(r->rm.framing_errors_total, 1u);
  ck_assert_uint_eq(r->im.errors_total[NET_ERR_FORMAT], 1u);
  ck_assert_ptr_null(r->tsp);
  single_rig_close(r);
  free(r);
}
END_TEST

static Suite *radiohead_suite(void) {
  Suite *s = suite_create("dipiradiohead_radiohead");
  TCase *tc = tcase_create("core");
  tcase_set_timeout(tc, 10);
  tcase_add_test(tc, meta_cb_copies_artist_and_title_and_marks_dirty);
  tcase_add_test(tc, meta_cb_tolerates_null_metrics);
  tcase_add_test(tc, source_codec_name_maps_every_known_codec);
  tcase_add_test(tc, packet_cb_batches_until_ts_per_dgram_then_flushes);
  tcase_add_test(tc, flush_batch_is_a_no_op_when_empty);
  tcase_add_test(tc, flush_batch_prefixes_rtp_header_when_rtp_enabled);
  tcase_add_test(tc, emit_metrics_reports_output_service_and_radio_counters);
  tcase_add_test(tc, emit_metrics_adds_one_labeled_block_per_cas_vendor);
  tcase_add_test(tc, timeline_stays_monotonic_across_a_sample_rate_change);
  tcase_add_test(tc, emit_metrics_is_a_no_op_for_a_disabled_exporter);
  tcase_add_test(tc, mpts_cas_relay_helpers_forward_to_the_cas);
  tcase_add_test(tc, radiohead_mpts_set_cas_makes_the_mux_send_the_cas_cat);
  tcase_add_loop_test(tc, output_open_builds_the_configured_senders_and_close_releases_them, 0, (int)(sizeof output_cases / sizeof output_cases[0]));
  tcase_add_test(tc, output_open_creates_the_inspector_only_when_inspection_is_on);
  tcase_add_test(tc, rist_and_srt_open_fail_cleanly_without_the_transport_libraries);
  tcase_add_test(tc, srt_service_is_a_no_op_without_a_sink);
  tcase_add_test(tc, srt_service_tracks_the_link_state_edge);
  tcase_add_test(tc, single_frame_creates_the_standalone_program_and_paces_the_stream);
  tcase_add_test(tc, single_frame_ahead_of_the_pacing_clock_is_deferred);
  tcase_add_test(tc, single_frame_attaches_the_cas_and_keeps_running_while_it_is_healthy);
  tcase_add_test(tc, single_frame_reports_a_fatal_cas_failure);
  tcase_add_test(tc, single_frame_reports_a_source_error_and_counts_the_framing_failure);
  suite_add_tcase(s, tc);
  return s;
}

int main(void) {
  SRunner *sr = srunner_create(radiohead_suite());
  srunner_run_all(sr, CK_NORMAL);
  int failed = srunner_ntests_failed(sr);
  srunner_free(sr);
  return failed == 0 ? EXIT_SUCCESS : EXIT_FAILURE;
}
