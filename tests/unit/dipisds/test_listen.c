/* Copyright 2026 dvbipitools authors. Licensed under GPL-3.0-or-later.
 * See NOTICE and LICENSE for details and authorship information. */

#include <check.h>
#include <pthread.h>
#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/socket.h>
#include <time.h>
#include <unistd.h>

#include "../alloc_fail.h"
#include "../log_capture.h"
#include "../run_helper.h"
#include "dipisds/listen.c"
#include "lib/bim/bitwriter.h"
#include "lib/bim/sdnsbim.h"
#include "lib/bim/strrepo.h"
#include "lib/helper/sds_xml.h"
#include "lib/helper/xml_util.h"
#ifdef DVBIPITOOLS_SDNS_ZLIB
#include "lib/helper/zutil.h"
#endif
#include "lib/net/dvbstp.h"
#include "lib/net/multicast.h"
#include "lib/sys/signal.h"

static dvbstp_header_t make_header(unsigned payload_id, unsigned segment_id, unsigned version) {
  dvbstp_header_t h;
  memset(&h, 0, sizeof h);
  h.payload_id = payload_id;
  h.segment_id = segment_id;
  h.segment_version = version;
  return h;
}

typedef struct {
  dvbstp_sender_t sd;
  unsigned char *buf[2];
  unsigned char *bim;
} sds_sender_t;

#define SDNS2008 "urn:dvb:metadata:iptv:sdns:2008-1"

#ifdef DVBIPITOOLS_SDNS_ZLIB
/* the part of the 2008-1 schema sds_build_broadcast writes */
static const char sdns_xsd[] =
  "<xs:schema xmlns:xs=\"http://www.w3.org/2001/XMLSchema\" xmlns=\"" SDNS2008 "\" targetNamespace=\"" SDNS2008 "\" elementFormDefault=\"qualified\">"
  "<xs:complexType name=\"IPMulticastAddressType\"><xs:attribute name=\"Address\" type=\"xs:string\"/><xs:attribute name=\"Port\" type=\"xs:string\"/>"
  "<xs:attribute name=\"Streaming\" type=\"xs:string\"/></xs:complexType>"
  "<xs:complexType name=\"ServiceLocationType\"><xs:sequence><xs:element name=\"IPMulticastAddress\" type=\"IPMulticastAddressType\"/></xs:sequence></xs:complexType>"
  "<xs:complexType name=\"TextualIdentifierType\"><xs:attribute name=\"ServiceName\" type=\"xs:string\"/></xs:complexType>"
  "<xs:complexType name=\"DVBTripletType\"><xs:attribute name=\"OrigNetId\" type=\"xs:string\"/><xs:attribute name=\"TSId\" type=\"xs:string\"/>"
  "<xs:attribute name=\"ServiceId\" type=\"xs:string\"/></xs:complexType>"
  "<xs:complexType name=\"SingleServiceType\"><xs:sequence><xs:element name=\"ServiceLocation\" type=\"ServiceLocationType\"/>"
  "<xs:element name=\"TextualIdentifier\" type=\"TextualIdentifierType\"/><xs:element name=\"DVBTriplet\" type=\"DVBTripletType\"/></xs:sequence></xs:complexType>"
  "<xs:complexType name=\"ServiceListType\"><xs:sequence><xs:element name=\"SingleService\" type=\"SingleServiceType\" maxOccurs=\"unbounded\"/></xs:sequence></xs:complexType>"
  "<xs:complexType name=\"BroadcastDiscoveryType\"><xs:sequence><xs:element name=\"ServiceList\" type=\"ServiceListType\"/></xs:sequence>"
  "<xs:attribute name=\"DomainName\" type=\"xs:string\"/><xs:attribute name=\"Version\" type=\"xs:string\"/></xs:complexType>"
  "<xs:complexType name=\"ServiceDiscoveryType\"><xs:sequence><xs:element name=\"BroadcastDiscovery\" type=\"BroadcastDiscoveryType\"/></xs:sequence></xs:complexType>"
  "<xs:element name=\"ServiceDiscovery\" type=\"ServiceDiscoveryType\"/></xs:schema>";

/* docs[2]: the first document as a BiM segment of the loaded schema. 0 ok */
static size_t build_bim(sds_sender_t *sd, const char *dir) {
  char path[300];
  const char *paths[1] = {dir};
  const char *culprit = NULL;
  bim_sdnsdoc_t d;
  bim_sdnsroots_t roots;
  bim_encode_opts_t eo;
  bim_err_t err;
  xml_doc_t *x = NULL;
  dvbtva_fuu_t in;
  bitwriter_t bw;
  strrepo_writer_t sw;
  const unsigned char *p;
  size_t n;
  FILE *f;
  snprintf(path, sizeof path, "%s/sdns.xsd", dir);
  f = fopen(path, "w");
  ck_assert_ptr_nonnull(f);
  ck_assert_uint_eq(fwrite(sdns_xsd, 1, sizeof sdns_xsd - 1, f), sizeof sdns_xsd - 1);
  fclose(f);
  ck_assert_int_eq(bim_sdnsdoc_open(&d, paths, 1, SDNS2008, &culprit), 0);
  ck_assert_int_eq(xml_doc_parse((const char *)sd->buf[0], sd->sd.lens[0], &x, NULL), 0);
  roots = (bim_sdnsroots_t){d.c, SDNS2008};
  memset(&eo, 0, sizeof eo);
  eo.schemas = &d.c->schema;
  eo.nschemas = 1;
  eo.lax_schema = -1;
  eo.frag_format = 1;
  eo.mpegb = 1;
  eo.deflate = zdeflate;
  in = (dvbtva_fuu_t){BIM_SDNS_CONTEXT_PATH, xml_doc_root(x)};
  bitwriter_init(&bw);
  ck_assert_int_eq(strrepo_writer_init(&sw), 0);
  if (dvbtva_au_write(bim_sdnsbim_root, &roots, &eo, &in, 1, &bw, &sw, NULL, &err)) ck_abort_msg("%s %s", err.code, err.msg);
  p = bitwriter_data(&bw, &n);
  sd->bim = malloc(n);
  ck_assert_ptr_nonnull(sd->bim);
  memcpy(sd->bim, p, n);
  bitwriter_free(&bw);
  strrepo_writer_free(&sw);
  xml_doc_free(x);
  bim_sdnsdoc_close(&d);
  return n;
}
#endif

static void build_docs(sds_sender_t *sd) {
  sds_service_t svc[2];

  memset(svc, 0, sizeof svc);
  snprintf(svc[0].name, sizeof svc[0].name, "Svc A");
  snprintf(svc[0].address, sizeof svc[0].address, "239.1.1.1");
  svc[0].family = AF_INET;
  svc[0].port = 5000;
  svc[0].rtp = 1;
  svc[0].tsid = 1;
  svc[0].onid = 2;
  svc[0].sid = 101;
  svc[1] = svc[0];
  snprintf(svc[1].name, sizeof svc[1].name, "Svc B");
  snprintf(svc[1].address, sizeof svc[1].address, "239.1.1.2");
  svc[1].sid = 102;
  sd->sd.lens[0] = sds_build_broadcast("example.org", 1, svc, 2, NULL, NULL, NULL, NULL, &sd->buf[0]);
  sd->sd.lens[1] = sds_build_broadcast("example.org", 2, svc, 1, NULL, NULL, NULL, NULL, &sd->buf[1]);
  sd->sd.docs[0] = sd->buf[0];
  sd->sd.docs[1] = sd->buf[1];
  ck_assert_uint_gt(sd->sd.lens[0], 0u);
  ck_assert_uint_gt(sd->sd.lens[1], 0u);
}

static unsigned count_of(const char *hay, const char *needle) {
  unsigned n = 0;

  for (const char *p = hay; (p = strstr(p, needle)) != NULL; p += strlen(needle)) n++;
  return n;
}

static char *slurp_text(const char *path) {
  FILE *f = fopen(path, "r");
  char *buf = calloc(1, 65536);
  size_t n;

  ck_assert_ptr_nonnull(f);
  ck_assert_ptr_nonnull(buf);
  n = fread(buf, 1, 65535, f);
  buf[n] = '\0';
  fclose(f);
  return buf;
}

static void *provider_sender_thread(void *arg) {
  const dvbstp_sender_t *sd = arg;
  mcast_t *m;

  sleep_ms(DVBSTP_SENDER_START_MS);
  m = mcast_open_send(AF_INET, sd->group, sd->port, NULL, 0);
  if (m) {
    for (unsigned pass = 0; pass < DVBSTP_SENDER_PASSES; pass++)
      for (unsigned i = 0; i < sd->nsteps; i++) {
        const send_step_t *st = &sd->steps[i];
        dvbstp_send_t seg = {.payload_id = st->payload_id,
                             .segment_id = 1,
                             .segment_version = st->version,
                             .compr = st->compr,
                             .has_provider_id = 1,
                             .provider_id = {(unsigned char)(7 + i)},
                             .want_crc = 1};

        dvbstp_send_segment(m, &seg, sd->docs[st->doc], sd->lens[st->doc]);
        sleep_ms(DVBSTP_SENDER_GAP_MS);
      }
    mcast_close(m);
  }
  sleep_ms(DVBSTP_SENDER_TAIL_MS);
  kill(getpid(), SIGTERM);
  return NULL;
}

typedef struct {
  const send_step_t *steps;
  unsigned nsteps;
  out_fmt_t format;
  int verbose;
  unsigned segments;
  unsigned services;
  const char *log_has;
  const char *out_has;
  int schema; /* doc 2: BiM segment, decoded through a schema directory */
} listen_case_t;

static const send_step_t steps_one[] = {{DVBSTP_PAYLOAD_BROADCAST_DISCOVERY, 1, 0, 0}};
static const send_step_t steps_other_and_dup[] = {
  {DVBSTP_PAYLOAD_SP_DISCOVERY, 1, 0, 0},
  {DVBSTP_PAYLOAD_PACKAGE_DISCOVERY, 1, 0, 0},
  {DVBSTP_PAYLOAD_BROADCAST_DISCOVERY, 1, 0, 0},
  {DVBSTP_PAYLOAD_BROADCAST_DISCOVERY, 1, 0, 0},
};
static const send_step_t steps_two_versions[] = {
  {DVBSTP_PAYLOAD_BROADCAST_DISCOVERY, 1, 0, 0},
  {DVBSTP_PAYLOAD_BROADCAST_DISCOVERY, 2, 1, 0},
};

static const send_step_t steps_two_providers[] = {
  {DVBSTP_PAYLOAD_BROADCAST_DISCOVERY, 1, 0, 0},
  {DVBSTP_PAYLOAD_BROADCAST_DISCOVERY, 2, 1, 0},
};

#ifdef DVBIPITOOLS_SDNS_ZLIB
static const send_step_t steps_bim[] = {{DVBSTP_PAYLOAD_BROADCAST_DISCOVERY, 1, 2, 1}};
#endif

static const listen_case_t listen_cases[] = {
  {steps_one, 1, OUT_CSV, 0, 1, 2, "found 2 services in 1 segment", "Svc B,rtp://@239.1.1.2:5000,1,2,102", 0},
  {steps_one, 1, OUT_CSV, 1, 1, 2, "segment 1: 2 services", "Svc A,rtp://@239.1.1.1:5000,1,2,101", 0},
  {steps_other_and_dup, 4, OUT_CSV, 0, 1, 2, "found 2 services in 1 segment", "Svc A", 0},
  {steps_two_versions, 2, OUT_CSV, 0, 1, 1, "found 1 service in 1 segment", "Svc A", 0},
  {steps_two_providers, 2, OUT_CSV, 0, 2, 3, "found 3 services in 2 segments", "Svc A", 0},
  {steps_one, 1, OUT_XML, 0, 1, 2, "found 2 services in 1 segment", "<BroadcastDiscovery", 0},
#ifdef DVBIPITOOLS_SDNS_ZLIB
  {steps_bim, 1, OUT_CSV, 0, 1, 2, "found 2 services in 1 segment", "Svc B,rtp://@239.1.1.2:5000,1,2,102", 1},
#endif
};

START_TEST(listen_run_collects_broadcast_discovery_in_every_format) {
  const listen_case_t *c = &listen_cases[_i];
  sds_sender_t *sd = calloc(1, sizeof *sd);
  char group[32];
  char out_path[64];
  char msg[8192];
  char dir[] = "/tmp/dipisds_listen_schema_XXXXXX";
  char want[64];
  char *out;
  pthread_t th;
  config_t cfg;
  int fd;
  int rc;

  ck_assert_ptr_nonnull(sd);
  build_docs(sd);
  run_helper_group(group, sizeof group, 79);
  sd->sd.group = group;
  sd->sd.port = run_helper_free_udp_port();
  sd->sd.steps = c->steps;
  sd->sd.nsteps = c->nsteps;
  snprintf(out_path, sizeof out_path, "/tmp/dipisds_listen_XXXXXX");
  fd = mkstemp(out_path);
  ck_assert_int_ge(fd, 0);
  close(fd);
  memset(&cfg, 0, sizeof cfg);
  cfg.family = AF_INET;
  snprintf(cfg.mcast_group, sizeof cfg.mcast_group, "%s", group);
  cfg.mcast_port = sd->sd.port;
  cfg.timeout_s = 20;
  cfg.output_path = out_path;
  cfg.format = c->format;
  cfg.verbose = c->verbose;
#ifdef DVBIPITOOLS_SDNS_ZLIB
  if (c->schema) {
    ck_assert_ptr_nonnull(mkdtemp(dir));
    sd->sd.lens[2] = build_bim(sd, dir);
    sd->sd.docs[2] = sd->bim;
    cfg.schemas[0] = dir;
    cfg.nschemas = 1;
    cfg.sdns_ns = SDNS2008;
  }
#endif
  signals_install();
  log_capture_begin();
  ck_assert_int_eq(pthread_create(&th, NULL, c->steps == steps_two_providers ? provider_sender_thread : dvbstp_sender_thread, &sd->sd), 0);
  rc = listen_run(&cfg);
  pthread_join(th, NULL);
  log_capture_end(msg, sizeof msg);
  ck_assert_int_eq(rc, 0);
  ck_assert_ptr_nonnull(strstr(msg, c->log_has));
  out = slurp_text(out_path);
  ck_assert_ptr_nonnull(strstr(out, c->out_has));
  if (c->format == OUT_CSV) {
    ck_assert_uint_eq(count_of(out, "rtp://"), c->services);
  } else {
    ck_assert_uint_eq(count_of(out, "<BroadcastDiscovery"), c->segments);
  }
  snprintf(want, sizeof want, "in %u segment", c->segments);
  ck_assert_ptr_nonnull(strstr(msg, want));
  free(out);
  free(sd->buf[0]);
  free(sd->buf[1]);
  free(sd->bim);
  free(sd);
  unlink(out_path);
  if (c->schema) {
    char xsd[100];
    snprintf(xsd, sizeof xsd, "%s/sdns.xsd", dir);
    unlink(xsd);
    rmdir(dir);
  }
}
END_TEST

typedef enum { END_TIMEOUT, END_JOIN_FAILS, END_OUTPUT_OPEN_FAILS, END_OUTPUT_WRITE_FAILS } end_kind_t;

typedef struct {
  end_kind_t kind;
  int rc;
  const char *msg;
} end_case_t;

static const end_case_t end_cases[] = {
  {END_TIMEOUT, 0, "found 0 services in 0 segments"},
  {END_JOIN_FAILS, 1, "cannot join"},
  {END_OUTPUT_OPEN_FAILS, 1, "for writing"},
  {END_OUTPUT_WRITE_FAILS, 1, "error writing"},
};

START_TEST(listen_run_handles_timeout_and_setup_failures) {
  const end_case_t *c = &end_cases[_i];
  char group[32];
  char out_path[64];
  char msg[4096];
  config_t cfg;
  int fd;
  int rc;

  snprintf(out_path, sizeof out_path, "/tmp/dipisds_listen_XXXXXX");
  fd = mkstemp(out_path);
  ck_assert_int_ge(fd, 0);
  close(fd);
  run_helper_group(group, sizeof group, 80);
  memset(&cfg, 0, sizeof cfg);
  cfg.family = AF_INET;
  snprintf(cfg.mcast_group, sizeof cfg.mcast_group, "%s", group);
  cfg.mcast_port = run_helper_free_udp_port();
  cfg.timeout_s = 1;
  cfg.output_path = out_path;
  cfg.format = c->kind == END_OUTPUT_WRITE_FAILS ? OUT_M3U : OUT_CSV;
  if (c->kind == END_JOIN_FAILS) cfg.iface = "nosuchif0";
  if (c->kind == END_OUTPUT_OPEN_FAILS) cfg.output_path = "/nonexistent-dir/out.csv";
  if (c->kind == END_OUTPUT_WRITE_FAILS) cfg.output_path = "/dev/full";
  log_capture_begin();
  rc = listen_run(&cfg);
  log_capture_end(msg, sizeof msg);
  ck_assert_int_eq(rc, c->rc);
  ck_assert_ptr_nonnull(strstr(msg, c->msg));
  unlink(out_path);
}
END_TEST

static void *term_later(void *arg) {
  (void)arg;
  usleep(300000);
  raise(SIGTERM);
  return NULL;
}

START_TEST(listen_run_timeout_zero_runs_until_signal) {
  char group[32];
  char msg[4096];
  config_t cfg;
  pthread_t th;
  double t0;
  int rc;

  run_helper_group(group, sizeof group, 81);
  memset(&cfg, 0, sizeof cfg);
  cfg.family = AF_INET;
  snprintf(cfg.mcast_group, sizeof cfg.mcast_group, "%s", group);
  cfg.mcast_port = run_helper_free_udp_port();
  cfg.timeout_s = 0;
  cfg.output_path = "/dev/null";
  cfg.format = OUT_CSV;
  signals_install();
  ck_assert_int_eq(pthread_create(&th, NULL, term_later, NULL), 0);
  t0 = mono_seconds();
  log_capture_begin();
  rc = listen_run(&cfg);
  log_capture_end(msg, sizeof msg);
  pthread_join(th, NULL);
  ck_assert_int_eq(rc, 0);
  ck_assert_double_ge(mono_seconds() - t0, 0.25);
  ck_assert_ptr_nonnull(strstr(msg, "until stopped"));
}
END_TEST

static void fill_cfg(config_t *cfg, out_fmt_t fmt, int verbose) {
  memset(cfg, 0, sizeof *cfg);
  cfg->family = AF_INET;
  cfg->format = fmt;
  cfg->verbose = verbose;
  cfg->timeout_s = 1;
}

static dvbstp_header_t seg_header(unsigned payload, unsigned id, unsigned version, unsigned compr) {
  dvbstp_header_t h = make_header(payload, id, version);
  h.compr = compr;
  return h;
}

START_TEST(on_segment_filters_stores_and_replaces) {
  config_t cfg;
  listen_ctx_t *c = calloc(1, sizeof *c);
  sds_service_t svc[300];
  unsigned char *small = NULL;
  unsigned char *big = NULL;
  size_t slen;
  size_t blen;
  dvbstp_header_t h;
  ck_assert_ptr_nonnull(c);
  memset(svc, 0, sizeof svc);
  for (int i = 0; i < 300; i++) {
    snprintf(svc[i].name, sizeof svc[i].name, "S%d", i);
    snprintf(svc[i].address, sizeof svc[i].address, "239.5.%d.%d", i / 250, i % 250 + 1);
    svc[i].family = AF_INET;
    svc[i].port = 5000;
  }
  slen = sds_build_broadcast("d", 1, svc, 2, NULL, NULL, NULL, NULL, &small);
  blen = sds_build_broadcast("d", 1, svc, 300, NULL, NULL, NULL, NULL, &big);
  ck_assert_uint_gt(slen, 0);
  ck_assert_uint_gt(blen, 0);
  for (int fmt = 0; fmt < 2; fmt++)
    for (int verbose = 0; verbose < 2; verbose++) {
      fill_cfg(&cfg, fmt ? OUT_XML : OUT_CSV, verbose);
      memset(c, 0, sizeof *c);
      c->cfg = &cfg;
      h = seg_header(DVBSTP_PAYLOAD_SP_DISCOVERY, 1, 1, 0);
      ck_assert_int_eq(on_segment(c, &h, small, slen), 0);
      ck_assert_int_eq(c->seg_count, 0);
      h = seg_header(DVBSTP_PAYLOAD_BROADCAST_DISCOVERY, 1, 1, 0);
      ck_assert_int_eq(on_segment(c, &h, small, slen), 0);
      ck_assert_int_eq(c->seg_count, 1);
      ck_assert_int_eq(on_segment(c, &h, small, slen), 0);
      ck_assert_uint_eq(c->received, 1u);
      h = seg_header(DVBSTP_PAYLOAD_BROADCAST_DISCOVERY, 1, 2, 0);
      ck_assert_int_eq(on_segment(c, &h, big, blen), 0);
      ck_assert_uint_eq(c->received, 2u);
      ck_assert_int_eq(c->seg_count, 1);
      h = seg_header(DVBSTP_PAYLOAD_BROADCAST_DISCOVERY, 2, 1, 0);
      ck_assert_int_eq(on_segment(c, &h, big, blen), 0);
      ck_assert_int_eq(c->seg_count, 2);
      for (unsigned compr = 1; compr < 4; compr++) {
        h = seg_header(DVBSTP_PAYLOAD_BROADCAST_DISCOVERY, 50 + compr, 1, compr);
        ck_assert_int_eq(on_segment(c, &h, small, slen), 0);
      }
      for (int i = 0; i < c->seg_count; i++) seg_free(&c->segs[i]);
    }
  fill_cfg(&cfg, OUT_CSV, 0);
  memset(c, 0, sizeof *c);
  c->cfg = &cfg;
  for (unsigned id = 0; id < LISTEN_SEEN_MAX + 3; id++) {
    h = seg_header(DVBSTP_PAYLOAD_BROADCAST_DISCOVERY, id, 1, 0);
    ck_assert_int_eq(on_segment(c, &h, small, slen), 0);
  }
  ck_assert_int_eq(c->seg_count, LISTEN_SEEN_MAX);
  for (int i = 0; i < c->seg_count; i++) seg_free(&c->segs[i]);
  free(small);
  free(big);
  free(c);
}
END_TEST

static int count_substr(const char *s, const char *sub) {
  int n = 0;
  for (const char *p = strstr(s, sub); p; p = strstr(p + 1, sub)) n++;
  return n;
}

START_TEST(on_segment_reports_undecodable_once_per_version) {
  config_t cfg;
  listen_ctx_t *c = calloc(1, sizeof *c);
  static const unsigned char junk[] = {0xFF, 0x00, 0x01};
  char msg[4096];
  dvbstp_header_t h;
  ck_assert_ptr_nonnull(c);
  fill_cfg(&cfg, OUT_CSV, 0);
  c->cfg = &cfg;
  log_capture_begin();
  h = seg_header(DVBSTP_PAYLOAD_BROADCAST_DISCOVERY, 1, 1, 1);
  ck_assert_int_eq(on_segment(c, &h, junk, sizeof junk), 0);
  ck_assert_int_eq(on_segment(c, &h, junk, sizeof junk), 0);
  h = seg_header(DVBSTP_PAYLOAD_BROADCAST_DISCOVERY, 1, 2, 1);
  ck_assert_int_eq(on_segment(c, &h, junk, sizeof junk), 0);
  log_capture_end(msg, sizeof msg);
  ck_assert_int_eq(count_substr(msg, "AW400243"), 2);
  ck_assert_int_eq(c->seg_count, 0);
  free(c);
}
END_TEST

START_TEST(on_segment_survives_allocation_failures) {
  config_t cfg;
  listen_ctx_t *c = calloc(1, sizeof *c);
  sds_service_t svc[2];
  unsigned char *small = NULL;
  size_t slen;
  dvbstp_header_t h;
  ck_assert_ptr_nonnull(c);
  memset(svc, 0, sizeof svc);
  for (int i = 0; i < 2; i++) {
    snprintf(svc[i].name, sizeof svc[i].name, "S%d", i);
    snprintf(svc[i].address, sizeof svc[i].address, "239.5.5.%d", i + 1);
    svc[i].family = AF_INET;
    svc[i].port = 5000;
  }
  slen = sds_build_broadcast("d", 1, svc, 2, NULL, NULL, NULL, NULL, &small);
  ck_assert_uint_gt(slen, 0);
  for (int fmt = 0; fmt < 2; fmt++)
    for (long n = 0;; n++) {
      int hit;
      fill_cfg(&cfg, fmt ? OUT_XML : OUT_CSV, 1);
      memset(c, 0, sizeof *c);
      c->cfg = &cfg;
      h = seg_header(DVBSTP_PAYLOAD_BROADCAST_DISCOVERY, 1, 1, 0);
      alloc_fail_at(n);
      (void)on_segment(c, &h, small, slen);
      hit = alloc_fail_end();
      for (int i = 0; i < c->seg_count; i++) seg_free(&c->segs[i]);
      if (!hit) break;
    }
  free(small);
  free(c);
}
END_TEST

START_TEST(listen_run_survives_allocation_failures_and_bad_schema) {
  char group[32];
  char out_path[64];
  config_t cfg;
  int fd;
  run_helper_group(group, sizeof group, 81);
  snprintf(out_path, sizeof out_path, "/tmp/dipisds_listen_XXXXXX");
  fd = mkstemp(out_path);
  ck_assert_int_ge(fd, 0);
  close(fd);
  for (long n = 0;; n++) {
    int hit;
    unsigned port = run_helper_free_udp_port();
    fill_cfg(&cfg, OUT_CSV, 0);
    snprintf(cfg.mcast_group, sizeof cfg.mcast_group, "%s", group);
    cfg.mcast_port = port;
    cfg.output_path = out_path;
    alloc_fail_at(n);
    (void)listen_run(&cfg);
    hit = alloc_fail_end();
    if (!hit) break;
  }
  fill_cfg(&cfg, OUT_CSV, 0);
  snprintf(cfg.mcast_group, sizeof cfg.mcast_group, "%s", group);
  cfg.mcast_port = run_helper_free_udp_port();
  cfg.output_path = out_path;
  cfg.schemas[0] = "/nonexistent-schema-dir";
  cfg.nschemas = 1;
  cfg.sdns_ns = SDNS2008;
  ck_assert_int_eq(listen_run(&cfg), 1);
  unlink(out_path);
}
END_TEST

static Suite *listen_suite(void) {
  Suite *s = suite_create("dipisds_listen");
  TCase *tc = tcase_create("core");
  tcase_add_loop_test(tc, listen_run_collects_broadcast_discovery_in_every_format, 0, (int)(sizeof listen_cases / sizeof listen_cases[0]));
  tcase_add_loop_test(tc, listen_run_handles_timeout_and_setup_failures, 0, (int)(sizeof end_cases / sizeof end_cases[0]));
  tcase_add_test(tc, listen_run_timeout_zero_runs_until_signal);
  tcase_add_test(tc, on_segment_reports_undecodable_once_per_version);
  tcase_add_test(tc, on_segment_filters_stores_and_replaces);
  tcase_add_test(tc, on_segment_survives_allocation_failures);
  tcase_add_test(tc, listen_run_survives_allocation_failures_and_bad_schema);
  suite_add_tcase(s, tc);
  return s;
}

int main(void) {
  SRunner *sr = srunner_create(listen_suite());
  srunner_run_all(sr, CK_NORMAL);
  int failed = srunner_ntests_failed(sr);
  srunner_free(sr);
  return failed == 0 ? EXIT_SUCCESS : EXIT_FAILURE;
}
