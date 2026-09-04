/* Copyright 2026 dvbipitools authors. Licensed under GPL-3.0-or-later.
 * See NOTICE and LICENSE for details and authorship information. */

#include <stdio.h>
#include <unistd.h>

#include "lib/helper/log.h"
#include "lib/helper/signal.h"
#include "lib/helper/toolmain.h"

#include "args.h"
#include "core/htdocs.h"
#include "core/metrics.h"
#include "core/status.h"
#include "dlna/ssdp.h"
#include "reactor/reactor.h"
#include "ts/capture/capture.h"
#include "ts/channels/channels.h"
#include "version.h"

static const char *source_kind_name(source_kind_t k) {
  switch (k) {
    case SRC_SDS:  return "sds";
    case SRC_M3U:  return "m3u";
    case SRC_XSPF: return "xspf";
    case SRC_CSV:  return "csv";
    case SRC_XML:  return "xml";
    case SRC_HTTP: return "http";
  }
  return "?";
}

int main(int argc, char **argv) {
  config_t cfg;
  args_status_t st;
  channels_t *channels;
  metrics_exporter_t mx;
  int workers;
  int rc;

  dipixy_status_init(argc, argv);
  log_set_color(log_color_prescan(argc, argv));
  log_line_ansi("\e[1m%s\e[0m \e[0;32mv%s\e[0m \e[0;37m%s\e[0m \e[0;37m%s\e[0m \e[0;34m%s\e[0m", TOOL_NAME, TOOL_VERSION, BUILD_ARCH, BUILD_TYPE, BUILD_LINK);
  st = args_parse(argc, argv, &cfg);
  if (st == ARGS_NOARGS) {
    fprintf(stderr, "try '%s --help' for usage\n", TOOL_NAME);
    return 0;
  }
  if (st == ARGS_HELP)
    return 0;
  if (st == ARGS_ERR) {
    fprintf(stderr, "try '%s --help' for usage\n", TOOL_NAME);
    return 2;
  }
  log_set_color((log_color_t)cfg.color_mode);
  if (toolmain_daemonize(cfg.daemonize, TOOL_NAME)) {
    args_free(&cfg);
    return 1;
  }
  workers = reactor_resolve_workers(cfg.workers_spec, (int)sysconf(_SC_NPROCESSORS_ONLN));
  log_line(TOOL_NAME ": iface=%s workers=%d sources=%d", cfg.iface ? cfg.iface : "(kernel default)", workers, cfg.n_sources);
  capture_set_ring_cap((size_t)cfg.capture_ring_kib * 1024);
  signals_install();
  htdocs_template_init(&cfg);
  channels = channels_build(&cfg);
  if (!channels) {
    log_line(TOOL_NAME ": out of mem building channel lists");
    args_free(&cfg);
    return 1;
  }
  {
    int max_ord = 0;
    int i, j;
    for (i = 0; i < cfg.n_sources; i++)
      if (cfg.sources[i].ordinal > max_ord)
        max_ord = cfg.sources[i].ordinal;
    if (cfg.stdin_ordinal > max_ord)
      max_ord = cfg.stdin_ordinal;
    if (cfg.rist_ordinal > max_ord)
      max_ord = cfg.rist_ordinal;
    for (i = 1; i <= max_ord; i++) {
      const source_def_t *src = NULL;
      for (j = 0; j < cfg.n_sources; j++)
        if (cfg.sources[j].ordinal == i) {
          src = &cfg.sources[j];
          break;
        }
      if (src) {
        channel_list_t *l = atomic_load_explicit(&channels->lists[i - 1], memory_order_relaxed);
        int count = l ? l->count : 0;
        const char *kind = source_kind_name(src->kind);
        if (src->name)
          log_line(TOOL_NAME ": input #%d: %d channel%s [%s] \"%s\"", i, count, count == 1 ? "" : "s", kind, src->name);
        else
          log_line(TOOL_NAME ": input #%d: %d channel%s [%s]", i, count, count == 1 ? "" : "s", kind);
      } else if (i == cfg.stdin_ordinal) {
        if (cfg.stdin_name)
          log_line(TOOL_NAME ": input #%d: 1 channel [stdin] \"%s\"", i, cfg.stdin_name);
        else
          log_line(TOOL_NAME ": input #%d: 1 channel [stdin]", i);
      } else if (i == cfg.rist_ordinal) {
        if (cfg.rist_name)
          log_line(TOOL_NAME ": input #%d: 1 channel [rist] \"%s\"", i, cfg.rist_name);
        else
          log_line(TOOL_NAME ": input #%d: 1 channel [rist]", i);
      }
    }
  }
  capture_rist_init(cfg.rist_uri);
  if (cfg.stdin_path)
    capture_stdin_init();
  dipixy_metrics_init(&mx, &cfg);
  channels_start_refresh(channels, &cfg);
  rc = reactor_run(&cfg, channels, &mx, ssdp_start);
  ssdp_stop();
  channels_stop_refresh();
  dipixy_metrics_close(&mx);
  channels_free(channels);
  args_free(&cfg);
  log_line(TOOL_NAME ": shutdown complete");
  return rc ? 1 : 0;
}
