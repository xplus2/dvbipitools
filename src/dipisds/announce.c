/* Copyright 2026 dvbipitools authors. Licensed under GPL-3.0-or-later.
 * See NOTICE and LICENSE for details and authorship information. */

#include <stddef.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

#include "lib/sys/ioutil.h"
#include "lib/helper/log.h"
#include "lib/helper/uriparse.h"
#include "lib/metrics/export.h"
#include "lib/net/announce_driver.h"
#include "lib/net/dvbstp.h"
#include "lib/net/multicast.h"
#include "lib/sys/signal.h"
#include "announce.h"
#include "input.h"
#include "lib/helper/sds_xml.h"
#include "version.h"

typedef struct {
  unsigned long long documents_generated_total;
  unsigned long long document_errors_total;
  unsigned long long announcements_total;
  unsigned long long announcement_errors_total;
  double last_success_time; /* unix seconds, 0 = never */
} sds_metrics_t;

static void emit_metrics(metrics_exporter_t *mx, double now, const sds_state_t *st, const sds_metrics_t *sm) {
  metrics_writer_t w;
  int service_providers = st->in.kind == INPUT_SERVICES ? 1 : st->in.raw_provider_count;

  if (!metrics_exporter_due(mx, now)) return;
  if (metrics_exporter_begin(mx, &w, TOOL_VERSION)) return;
  metrics_writer_put(&w, METRICS_ID_SDS_SERVICE_PROVIDERS, NULL, (uint64_t)service_providers);
  metrics_writer_put(&w, METRICS_ID_SDS_SERVICES, NULL, (uint64_t)st->in.service_count);
  metrics_writer_put(&w, METRICS_ID_SDS_DOCUMENTS_GENERATED_TOTAL, NULL, sm->documents_generated_total);
  metrics_writer_put(&w, METRICS_ID_SDS_DOCUMENT_ERRORS_TOTAL, NULL, sm->document_errors_total);
  metrics_writer_put(&w, METRICS_ID_SDS_ANNOUNCEMENTS_TOTAL, "multicast", sm->announcements_total);
  metrics_writer_put(&w, METRICS_ID_SDS_ANNOUNCEMENT_ERRORS_TOTAL, NULL, sm->announcement_errors_total);
  metrics_writer_put(&w, METRICS_ID_SDS_LAST_SUCCESS_TIME_SECONDS, NULL, (uint64_t)sm->last_success_time);
  metrics_exporter_send(mx, &w);
}

static void doc_failed(const char *what) {
  log_code("AE500114", "out of memory building %s document", what);
}

static const char *sds_bcg_id(const config_t *cfg) {
  return cfg->bcg_id ? cfg->bcg_id : "bcg";
}

/* --packages, --cells, loaded once per state build */
typedef struct {
  sds_package_t *pkgs;
  int pkg_count;
  sds_cell_t *cells;
  int cell_count;
} side_t;

typedef size_t (*doc_build_fn)(const config_t *cfg, const input_t *in, const side_t *sd, unsigned version, unsigned char **out);

static size_t build_broadcast(const config_t *cfg, const input_t *in, const side_t *sd, unsigned version, unsigned char **out) {
  sds_ret_t ret_val;
  const sds_ret_t *ret = NULL;
  sds_fcc_t fcc_val;
  const sds_fcc_t *fcc = NULL;
  sds_fec_t fec_val;
  const sds_fec_t *fec = NULL;
  (void)sd;

  if (cfg->ret_enabled) {
    memset(&ret_val, 0, sizeof ret_val);
    bufcpy(ret_val.addr, sizeof ret_val.addr, cfg->ret_addr);
    ret_val.port = cfg->ret_port;
    ret_val.rtx_time_ms = cfg->ret_rtx_time;
    ret_val.rtx_pt = cfg->ret_rtx_pt;
    ret_val.mc = cfg->ret_mc;
    ret_val.mc_port = cfg->ret_mc_port;
    ret_val.rsi_mc_ret = cfg->ret_rsi_mc_ret;
    ret = &ret_val;
  }
  if (cfg->fcc_enabled) {
    memset(&fcc_val, 0, sizeof fcc_val);
    bufcpy(fcc_val.addr, sizeof fcc_val.addr, cfg->fcc_addr);
    fcc_val.port = cfg->fcc_port;
    fcc_val.rtx_time_ms = cfg->fcc_rtx_time;
    fcc_val.rtx_pt = cfg->fcc_rtx_pt;
    fcc_val.resolve_by_port = cfg->fcc_resolve_by_port;
    fcc_val.resolve_base_port = (unsigned)sds_fcc_resolve_base_port(cfg);
    fcc_val.resolve_max_channels = cfg->fcc_resolve_max_channels;
    fcc = &fcc_val;
  }
  if (cfg->al_fec_enabled) {
    memset(&fec_val, 0, sizeof fec_val);
    bufcpy(fec_val.addr, sizeof fec_val.addr, cfg->al_fec_addr);
    fec_val.port = cfg->al_fec_port;
    fec_val.pt = cfg->al_fec_pt;
    fec = &fec_val;
  }
  return sds_build_broadcast(cfg->provider, version, in->services, in->service_count, ret, fcc, fec, cfg->bcg_enabled ? sds_bcg_id(cfg) : NULL, out);
}

static size_t build_sp(const config_t *cfg, const input_t *in, const side_t *sd, unsigned version, unsigned char **out) {
  unsigned extra_payload_ids[4];
  int extra_count = 0;
  (void)in;
  (void)sd;
  if (cfg->packages_path) extra_payload_ids[extra_count++] = DVBSTP_PAYLOAD_PACKAGE_DISCOVERY;
  if (cfg->cells_path) extra_payload_ids[extra_count++] = DVBSTP_PAYLOAD_REGIONALISATION_DISCOVERY;
  if (cfg->rms_enabled || cfg->fus_enabled) extra_payload_ids[extra_count++] = DVBSTP_PAYLOAD_RMSFUS_DISCOVERY;
  if (cfg->bcg_enabled) extra_payload_ids[extra_count++] = DVBSTP_PAYLOAD_BCG_DISCOVERY;
  return sds_build_sp(cfg->provider, cfg->offering, cfg->lang, version, cfg->mcast_group, cfg->mcast_port, extra_payload_ids, extra_count, out);
}

static size_t build_package(const config_t *cfg, const input_t *in, const side_t *sd, unsigned version, unsigned char **out) {
  return sds_build_package(cfg->provider, version, sd->pkgs, sd->pkg_count, in->services, in->service_count, out);
}

static size_t build_cell(const config_t *cfg, const input_t *in, const side_t *sd, unsigned version, unsigned char **out) {
  (void)in;
  return sds_build_regionalisation(cfg->provider, version, sd->cells, sd->cell_count, out);
}

static size_t build_rmsfus(const config_t *cfg, const input_t *in, const side_t *sd, unsigned version, unsigned char **out) {
  sds_rms_t rms_val;
  sds_fus_t fus_val;
  int rms_count = 0, fus_count = 0;
  (void)in;
  (void)sd;
  memset(&rms_val, 0, sizeof rms_val);
  memset(&fus_val, 0, sizeof fus_val);
  if (cfg->rms_enabled) {
    bufcpy(rms_val.name, sizeof rms_val.name, cfg->rms_name);
    memcpy(rms_val.lang, cfg->rms_lang, 3);
    rms_val.location = cfg->rms_location;
    rms_val.logo_uri = cfg->rms_logo;
    rms_count = 1;
  } else {
    bufcpy(fus_val.name, sizeof fus_val.name, cfg->fus_name);
    memcpy(fus_val.lang, cfg->fus_lang, 3);
    fus_val.fus_id = cfg->fus_id;
    fus_val.announce_addr = cfg->fus_announce_addr[0] ? cfg->fus_announce_addr : NULL;
    fus_val.announce_port = cfg->fus_announce_port;
    fus_val.logo_uri = cfg->fus_logo;
    fus_count = 1;
  }
  return sds_build_rms_fus(cfg->provider, version, &rms_val, rms_count, &fus_val, fus_count, out);
}

static size_t build_bcg(const config_t *cfg, const input_t *in, const side_t *sd, unsigned version, unsigned char **out) {
  sds_bcg_t bcg = {sds_bcg_id(cfg), cfg->bcg_name, cfg->lang, cfg->bcg_description, cfg->bcg_provider_name,
    cfg->bcg_mcast_addr, cfg->bcg_mcast_port};
  (void)in;
  (void)sd;
  return sds_build_bcg(cfg->provider, version, &bcg, out);
}

static int on_always(const config_t *cfg) {
  (void)cfg;
  return 1;
}

static int on_packages(const config_t *cfg) { return cfg->packages_path != NULL; }

static int on_cells(const config_t *cfg) { return cfg->cells_path != NULL; }

static int on_rmsfus(const config_t *cfg) { return cfg->rms_enabled || cfg->fus_enabled; }

static int on_bcg(const config_t *cfg) { return cfg->bcg_enabled; }

/* send order */
static const struct {
  size_t off;
  doc_build_fn build;
  int (*enabled)(const config_t *cfg);
  unsigned payload_id;
  const char *what;
} doc_slots[] = {
  {offsetof(sds_state_t, broadcast), build_broadcast, on_always, DVBSTP_PAYLOAD_BROADCAST_DISCOVERY, "Broadcast Discovery"},
  {offsetof(sds_state_t, sp), build_sp, on_always, DVBSTP_PAYLOAD_SP_DISCOVERY, "Service Provider Discovery"},
  {offsetof(sds_state_t, package), build_package, on_packages, DVBSTP_PAYLOAD_PACKAGE_DISCOVERY, "Package Discovery"},
  {offsetof(sds_state_t, cell), build_cell, on_cells, DVBSTP_PAYLOAD_REGIONALISATION_DISCOVERY, "Regionalisation Discovery"},
  {offsetof(sds_state_t, rmsfus), build_rmsfus, on_rmsfus, DVBSTP_PAYLOAD_RMSFUS_DISCOVERY, "RMS-FUS Discovery"},
  {offsetof(sds_state_t, bcg), build_bcg, on_bcg, DVBSTP_PAYLOAD_BCG_DISCOVERY, "BCG Discovery"},
};

#define DOC_SLOTS (sizeof doc_slots / sizeof doc_slots[0])

static sds_doc_t *slot_doc(sds_state_t *st, size_t i) { return (sds_doc_t *)((char *)st + doc_slots[i].off); }

static const sds_doc_t *slot_cdoc(const sds_state_t *st, size_t i) { return (const sds_doc_t *)((const char *)st + doc_slots[i].off); }

void state_free(sds_state_t *st) {
  for (size_t i = 0; i < DOC_SLOTS; i++) free(slot_doc(st, i)->data);
  input_free(&st->in);
}

static int check_package_services(const char *path, const sds_package_t *pkgs, int pkg_count, const sds_service_t *svcs, int svc_count) {
  for (int i = 0; i < pkg_count; i++) {
    for (int j = 0; j < pkgs[i].service_count; j++) {
      const char *name = pkgs[i].service_names[j];
      const sds_service_t *s = sds_find_service(name, svcs, svc_count);
      if (!s) {
        log_code("AE400220", "%s: package %u: unknown service: %s", path, pkgs[i].id, name);
        return -1;
      }
      if (sds_find_service(name, s + 1, svc_count - (int)(s - svcs) - 1)) {
        log_code("AE400321", "%s: package %u: ambiguous service name: %s", path, pkgs[i].id, name);
        return -1;
      }
    }
  }
  return 0;
}

static void side_free(side_t *sd) {
  free(sd->pkgs);
  free(sd->cells);
}

static int side_load(const config_t *cfg, const input_t *in, side_t *sd) {
  memset(sd, 0, sizeof *sd);
  if (cfg->packages_path) {
    if (!(sd->pkgs = malloc(sizeof *sd->pkgs * SDS_MAX_PACKAGES))) {
      doc_failed("Package Discovery");
      return -1;
    }
    if (input_load_packages(cfg->packages_path, sd->pkgs, SDS_MAX_PACKAGES, &sd->pkg_count) ||
        check_package_services(cfg->packages_path, sd->pkgs, sd->pkg_count, in->services, in->service_count))
      return -1;
  }
  if (cfg->cells_path) {
    if (!(sd->cells = malloc(sizeof *sd->cells * SDS_MAX_CELLS))) {
      doc_failed("Regionalisation Discovery");
      return -1;
    }
    if (input_load_cells(cfg->cells_path, sd->cells, SDS_MAX_CELLS, &sd->cell_count)) return -1;
  }
  return 0;
}

static int doc_same(const sds_doc_t *a, const sds_doc_t *b) {
  if (!a->data || !b->data) return a->data == b->data;
  return a->version == b->version && a->len == b->len && !memcmp(a->data, b->data, a->len);
}

/* prev NULL: version 1. else version of prev, bumped where content changed */
static int docs_build(const config_t *cfg, sds_state_t *st, const side_t *sd, const sds_state_t *prev) {
  for (size_t i = 0; i < DOC_SLOTS; i++) {
    sds_doc_t *d = slot_doc(st, i);
    const sds_doc_t *p = prev ? slot_cdoc(prev, i) : NULL;
    if (!doc_slots[i].enabled(cfg)) continue;
    d->version = p && p->data ? p->version : 1;
    d->len = doc_slots[i].build(cfg, &st->in, sd, d->version, &d->data);
    if (d->len && p && p->data && !doc_same(d, p)) {
      free(d->data);
      d->data = NULL;
      d->version = dvbstp_version_next(d->version);
      d->len = doc_slots[i].build(cfg, &st->in, sd, d->version, &d->data);
    }
    if (!d->len) {
      doc_failed(doc_slots[i].what);
      return -1;
    }
  }
  return 0;
}

static int raw_same(const sds_state_t *a, const sds_state_t *b) {
  return a->in.raw_payload_id == b->in.raw_payload_id && a->in.raw_xml_len == b->in.raw_xml_len &&
         !memcmp(a->in.raw_xml, b->in.raw_xml, a->in.raw_xml_len);
}

/* document Version wins, else bumped on change */
static unsigned raw_version(const sds_state_t *st, const sds_state_t *prev) {
  if (st->in.raw_version >= 0) return (unsigned)st->in.raw_version;
  if (!prev || prev->in.kind != INPUT_RAW_XML) return 1;
  return raw_same(st, prev) ? prev->version : dvbstp_version_next(prev->version);
}

static int state_build(const config_t *cfg, sds_state_t *st, const sds_state_t *prev) {
  side_t sd;
  int rc;
  memset(st, 0, sizeof *st);
  if (input_load(cfg->input_path, &st->in)) return -1;
  if (st->in.kind == INPUT_RAW_XML) {
    st->version = raw_version(st, prev);
    return 0;
  }
  rc = side_load(cfg, &st->in, &sd);
  if (!rc) rc = docs_build(cfg, st, &sd, prev);
  side_free(&sd);
  if (rc) state_free(st);
  return rc;
}

int state_load(const config_t *cfg, sds_state_t *st) {
  return state_build(cfg, st, NULL);
}

static int state_same(const sds_state_t *a, const sds_state_t *b) {
  if (a->in.kind != b->in.kind) return 0;
  if (a->in.kind == INPUT_RAW_XML) return a->version == b->version && raw_same(a, b);
  for (size_t i = 0; i < DOC_SLOTS; i++)
    if (!doc_same(slot_cdoc(a, i), slot_cdoc(b, i))) return 0;
  return 1;
}

int state_reload(const config_t *cfg, sds_state_t *st) {
  sds_state_t next;
  if (state_build(cfg, &next, st)) return -1;
  if (state_same(st, &next)) {
    state_free(&next);
    return 1;
  }
  state_free(st);
  *st = next;
  return 0;
}

static void reload_state(const config_t *cfg, sds_state_t *st, sds_metrics_t *sm, int metrics_on) {
  int rc = state_reload(cfg, st);
  if (rc < 0) {
    log_code("AW000175", "reload failed, keeping previous input");
    if (metrics_on) sm->document_errors_total++;
    return;
  }
  if (rc > 0) {
    log_line("reload %s: unchanged, versions kept", cfg->input_path);
    return;
  }
  log_line("reloaded %s: %d service%s", cfg->input_path, st->in.service_count, st->in.service_count == 1 ? "" : "s");
  if (metrics_on) sm->documents_generated_total++;
}

typedef struct {
  const config_t *cfg;
  metrics_exporter_t *mx;
  sds_state_t st;
  sds_metrics_t sm;
  int metrics_on;
} sds_announce_ctx_t;

static void sds_announce_ready(void *ctx_, const mcast_t *m) {
  const sds_announce_ctx_t *ctx = ctx_;
  char mcast[80];
  (void)m;
  uriparse_mcast_describe(ctx->cfg->family, ctx->cfg->mcast_group, ctx->cfg->mcast_port, mcast, sizeof mcast);
  if (ctx->st.in.kind == INPUT_RAW_XML) {
    log_line("announcing raw %s (payload 0x%02x) on %s every %lds", ctx->cfg->input_path, ctx->st.in.raw_payload_id, mcast, ctx->cfg->interval_s);
  } else {
    log_line("announcing %d service%s on %s every %lds", ctx->st.in.service_count, ctx->st.in.service_count == 1 ? "" : "s", mcast, ctx->cfg->interval_s);
  }
}

static void sds_announce_reload(void *ctx_) {
  sds_announce_ctx_t *ctx = ctx_;
  reload_state(ctx->cfg, &ctx->st, &ctx->sm, ctx->metrics_on);
}

static int send_doc(mcast_t *m, const sds_announce_ctx_t *ctx, unsigned payload_id, unsigned version, const unsigned char *doc, size_t len) {
  int64_t budget = (int64_t)ctx->cfg->interval_s * 500000000;
  return dvbstp_send_segment(m, &(dvbstp_send_t){.payload_id = payload_id, .segment_id = 1, .segment_version = version, .want_crc = 1, .pacing_budget_ns = budget}, doc, len) == 0;
}

static int sds_announce_cycle(void *ctx_, mcast_t *m, unsigned cycle) {
  sds_announce_ctx_t *ctx = ctx_;
  const sds_state_t *st = &ctx->st;
  int ok = 1;
  if (st->in.kind == INPUT_RAW_XML) {
    ok = send_doc(m, ctx, st->in.raw_payload_id, st->version, st->in.raw_xml, st->in.raw_xml_len);
  } else {
    for (size_t i = 0; i < DOC_SLOTS; i++) {
      const sds_doc_t *d = slot_cdoc(st, i);
      if (d->data) ok = send_doc(m, ctx, doc_slots[i].payload_id, d->version, d->data, d->len) && ok;
    }
  }
  if (!ok) log_code("AE100144", "cycle %u: send failed", cycle);
  if (ctx->metrics_on) {
    if (ok) {
      ctx->sm.announcements_total++;
      ctx->sm.last_success_time = (double)time(NULL);
    } else {
      ctx->sm.announcement_errors_total++;
    }
  }
  if (ok && ctx->cfg->verbose) log_code("AD100113", "cycle %u sent", cycle);
  emit_metrics(ctx->mx, mono_seconds(), &ctx->st, &ctx->sm);
  return 0;
}

static void sds_announce_cleanup(void *ctx_) {
  sds_announce_ctx_t *ctx = ctx_;
  state_free(&ctx->st);
}

int announce_run(const config_t *cfg, metrics_exporter_t *mx) {
  sds_announce_ctx_t ctx;
  announce_driver_t d;
  memset(&ctx, 0, sizeof ctx);
  ctx.cfg = cfg;
  ctx.mx = mx;
  ctx.metrics_on = metrics_exporter_enabled(mx);
  if (state_load(cfg, &ctx.st)) return 1;
  if (ctx.metrics_on) ctx.sm.documents_generated_total++;
  d.ctx = &ctx;
  d.on_ready = sds_announce_ready;
  d.reload = sds_announce_reload;
  d.run_cycle = sds_announce_cycle;
  d.cleanup = sds_announce_cleanup;
  d.family = cfg->family;
  d.mcast_group = cfg->mcast_group;
  d.mcast_port = cfg->mcast_port;
  d.iface = cfg->iface;
  d.dscp = cfg->dscp;
  d.ttl = (int)cfg->ttl;
  d.interval_s = cfg->interval_s;
  return announce_driver_run(&d);
}
