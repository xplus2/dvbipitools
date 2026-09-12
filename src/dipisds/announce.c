/* Copyright 2026 dvbipitools authors. Licensed under GPL-3.0-or-later.
 * See NOTICE and LICENSE for details and authorship information. */

#include <stdint.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

#include "lib/helper/ioutil.h"
#include "lib/helper/log.h"
#include "lib/metrics/export.h"
#include "lib/net/announce_driver.h"
#include "lib/net/dvbstp.h"
#include "lib/net/multicast.h"
#include "lib/helper/signal.h"
#include "announce.h"
#include "input.h"
#include "lib/helper/sds_xml.h"
#include "version.h"

#define DOC_CAP 65536

typedef struct {
  unsigned long long documents_generated_total;
  unsigned long long document_errors_total;
  unsigned long long announcements_total;
  unsigned long long announcement_errors_total;
  double last_success_time; /* unix seconds, 0 = never */
} sds_metrics_t;

static void emit_metrics(metrics_exporter_t *mx, double now, const sds_state_t *st, const sds_metrics_t *sm) {
  metrics_writer_t w;
  int service_providers = st->in.kind == INPUT_SERVICES ? 1 : 0;

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

static void *doc_alloc(sds_state_t *st, const char *what) {
  void *p = malloc(DOC_CAP);
  if (!p) {
    log_line("out of memory building %s document", what);
    state_free(st);
  }
  return p;
}

void state_free(sds_state_t *st) {
  free(st->broadcast_doc);
  free(st->sp_doc);
  free(st->package_doc);
  free(st->cell_doc);
  free(st->rmsfus_doc);
  input_free(&st->in);
}

int state_load(const config_t *cfg, sds_state_t *st) {
  unsigned extra_payload_ids[3];
  int extra_count = 0;

  memset(st, 0, sizeof *st);
  if (input_load(cfg->input_path, &st->in)) return -1;
  if (st->in.kind == INPUT_SERVICES) {
    sds_ret_t ret_val;
    const sds_ret_t *ret = NULL;
    sds_fcc_t fcc_val;
    const sds_fcc_t *fcc = NULL;
    sds_fec_t fec_val;
    const sds_fec_t *fec = NULL;
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
      fcc_val.resolve_base_port = cfg->fcc_resolve_base_port ? cfg->fcc_resolve_base_port : cfg->fcc_port + 1;
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
    if (cfg->packages_path) extra_payload_ids[extra_count++] = DVBSTP_PAYLOAD_PACKAGE_DISCOVERY;
    if (cfg->cells_path) extra_payload_ids[extra_count++] = DVBSTP_PAYLOAD_REGIONALISATION_DISCOVERY;
    if (cfg->rms_enabled || cfg->fus_enabled) extra_payload_ids[extra_count++] = DVBSTP_PAYLOAD_RMSFUS_DISCOVERY;

    st->broadcast_doc = doc_alloc(st, "Broadcast Discovery");
    if (!st->broadcast_doc) return -1;
    st->sp_doc = doc_alloc(st, "Service Provider Discovery");
    if (!st->sp_doc) return -1;
    st->broadcast_len = sds_build_broadcast(cfg->provider, 1, st->in.services, st->in.service_count, ret, fcc, fec, st->broadcast_doc, DOC_CAP);
    st->sp_len = sds_build_sp(cfg->provider, cfg->offering, cfg->lang, 1, cfg->mcast_group, cfg->mcast_port, extra_payload_ids, extra_count, st->sp_doc, DOC_CAP);
    if (!st->broadcast_len || !st->sp_len) {
      log_line("SD&S document too large (max %d bytes), reduce the service list", DOC_CAP);
      state_free(st);
      return -1;
    }
    if (cfg->packages_path) {
      sds_package_t *pkgs = malloc(sizeof *pkgs * SDS_MAX_PACKAGES);
      int pkg_count;
      if (!pkgs || input_load_packages(cfg->packages_path, pkgs, SDS_MAX_PACKAGES, &pkg_count)) {
        free(pkgs);
        state_free(st);
        return -1;
      }
      st->package_doc = doc_alloc(st, "Package Discovery");
      if (!st->package_doc) {
        free(pkgs);
        return -1;
      }
      st->package_len = sds_build_package(cfg->provider, 1, pkgs, pkg_count, st->in.services, st->in.service_count, st->package_doc, DOC_CAP);
      free(pkgs);
      if (!st->package_len) {
        log_line("Package Discovery document too large (max %d bytes)", DOC_CAP);
        state_free(st);
        return -1;
      }
    }

    if (cfg->cells_path) {
      sds_cell_t *cells = malloc(sizeof *cells * SDS_MAX_CELLS);
      int cell_count;
      if (!cells || input_load_cells(cfg->cells_path, cells, SDS_MAX_CELLS, &cell_count)) {
        free(cells);
        state_free(st);
        return -1;
      }
      st->cell_doc = doc_alloc(st, "Regionalisation Discovery");
      if (!st->cell_doc) {
        free(cells);
        return -1;
      }
      st->cell_len = sds_build_regionalisation(cfg->provider, 1, cells, cell_count, st->cell_doc, DOC_CAP);
      free(cells);
      if (!st->cell_len) {
        log_line("Regionalisation Discovery document too large (max %d bytes)", DOC_CAP);
        state_free(st);
        return -1;
      }
    }

    if (cfg->rms_enabled || cfg->fus_enabled) {
      sds_rms_t rms_val;
      sds_fus_t fus_val;
      int rms_count = 0, fus_count = 0;
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
      st->rmsfus_doc = doc_alloc(st, "RMS-FUS Discovery");
      if (!st->rmsfus_doc) return -1;
      st->rmsfus_len = sds_build_rms_fus(cfg->provider, 1, &rms_val, rms_count, &fus_val, fus_count, st->rmsfus_doc, DOC_CAP);
      if (!st->rmsfus_len) {
        log_line("RMS-FUS Discovery document too large (max %d bytes)", DOC_CAP);
        state_free(st);
        return -1;
      }
    }
  }
  return 0;
}

static void reload_state(const config_t *cfg, sds_state_t *st, sds_metrics_t *sm, int metrics_on) {
  sds_state_t next;
  if (state_load(cfg, &next)) {
    log_line("reload failed, keeping previous input");
    if (metrics_on) sm->document_errors_total++;
    return;
  }
  state_free(st);
  *st = next;
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

static void sds_announce_ready(void *ctx_, mcast_t *m) {
  sds_announce_ctx_t *ctx = ctx_;
  (void)m;
  if (ctx->st.in.kind == INPUT_RAW_XML) {
    log_line("announcing raw %s (payload 0x%02x) on %s:%u every %lds", ctx->cfg->input_path, ctx->st.in.raw_payload_id, ctx->cfg->mcast_group,
             ctx->cfg->mcast_port, ctx->cfg->interval_s);
  } else {
    log_line("announcing %d service%s on %s:%u every %lds", ctx->st.in.service_count, ctx->st.in.service_count == 1 ? "" : "s",
             ctx->cfg->mcast_group, ctx->cfg->mcast_port, ctx->cfg->interval_s);
  }
}

static void sds_announce_reload(void *ctx_) {
  sds_announce_ctx_t *ctx = ctx_;
  reload_state(ctx->cfg, &ctx->st, &ctx->sm, ctx->metrics_on);
}

static int sds_announce_cycle(void *ctx_, mcast_t *m, unsigned cycle) {
  sds_announce_ctx_t *ctx = ctx_;
  int ok;
  if (ctx->st.in.kind == INPUT_RAW_XML) {
    ok = dvbstp_send_segment(m, ctx->st.in.raw_payload_id, 1, 1, 0, 0, 0, 1, ctx->st.in.raw_xml, ctx->st.in.raw_xml_len) == 0;
  } else {
    ok = dvbstp_send_segment(m, DVBSTP_PAYLOAD_BROADCAST_DISCOVERY, 1, 1, 0, 0, 0, 1, ctx->st.broadcast_doc, ctx->st.broadcast_len) == 0;
    ok = dvbstp_send_segment(m, DVBSTP_PAYLOAD_SP_DISCOVERY, 1, 1, 0, 0, 0, 1, ctx->st.sp_doc, ctx->st.sp_len) == 0 && ok;
    if (ctx->st.package_doc) ok = dvbstp_send_segment(m, DVBSTP_PAYLOAD_PACKAGE_DISCOVERY, 1, 1, 0, 0, 0, 1, ctx->st.package_doc, ctx->st.package_len) == 0 && ok;
    if (ctx->st.cell_doc) ok = dvbstp_send_segment(m, DVBSTP_PAYLOAD_REGIONALISATION_DISCOVERY, 1, 1, 0, 0, 0, 1, ctx->st.cell_doc, ctx->st.cell_len) == 0 && ok;
    if (ctx->st.rmsfus_doc) ok = dvbstp_send_segment(m, DVBSTP_PAYLOAD_RMSFUS_DISCOVERY, 1, 1, 0, 0, 0, 1, ctx->st.rmsfus_doc, ctx->st.rmsfus_len) == 0 && ok;
  }
  if (ctx->metrics_on) {
    if (ok) {
      ctx->sm.announcements_total++;
      ctx->sm.last_success_time = (double)time(NULL);
    } else {
      ctx->sm.announcement_errors_total++;
    }
  }
  if (ctx->cfg->verbose) log_line("cycle %u sent", cycle);
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
  d.interval_s = cfg->interval_s;
  return announce_driver_run(&d);
}
