/* Copyright 2026 dvbipitools authors. Licensed under GPL-3.0-or-later.
 * See NOTICE and LICENSE for details and authorship information. */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "args.h"
#include "lib/helper/ioutil.h"
#include "lib/helper/log.h"
#include "lib/metrics/export.h"
#include "lib/helper/signal.h"
#include "lib/helper/toolmain.h"
#include "record.h"
#include "version.h"

static const char *fmt_name(out_fmt_t f) {
  switch (f) {
    case FMT_RAW:   return "raw";
    case FMT_TS:    return "ts";
    case FMT_MKV:   return "mkv";
    case FMT_MKA:   return "mka";
    case FMT_MP4:   return "mp4";
    case FMT_M4A:   return "m4a";
  }
  return "?";
}

static const char *sub_name(sub_mode_t s) {
  switch (s) {
    case SUB_KEEP:    return "keep";
    case SUB_STRIP:   return "strip";
    case SUB_SRT:     return "srt";
  }
  return "?";
}

/* banner prints before parsing: --color read early */
int main(int argc, char **argv) {
  config_t cfg;
  char src[1024];
  dstrbuf_t sb_out, sb_line;
  args_status_t st;

  log_set_color(log_color_prescan(argc, argv));

  toolmain_print_banner(TOOL_NAME, TOOL_VERSION, BUILD_ARCH, BUILD_TYPE, BUILD_LINK);
  st = args_parse(argc, argv, &cfg);
  if (st == ARGS_OK) log_set_color((log_color_t)cfg.color_mode);
  if (st == ARGS_HELP) return 0;
  if (st == ARGS_ERR) {
    fprintf(stderr, "try '%s --help' for usage\n", TOOL_NAME);
    return 2;
  }

  dstrbuf_init(&sb_out);
  for (int i = 0; i < cfg.n_out; i++) {
    char one[600];
    out_describe(&cfg.out[i], one, sizeof one);
    dstrbuf_appendf(&sb_out, "%s%s", i ? "," : "", one);
  }
  source_describe(&cfg.source, src, sizeof src);
  dstrbuf_init(&sb_line);
  dstrbuf_appendf(&sb_line, "\e[1mi:\e[0m\e[0;37m%s\e[0m \e[1mo:\e[0m\e[0;37m%s\e[0m \e[1mf:\e[0m\e[0;37m%s\e[0m", src, sb_out.buf ? sb_out.buf : "", fmt_name(cfg.format));
  if (cfg.audio_all) dstrbuf_appendf(&sb_line, " \e[1ma:\e[0m\e[0;37mall\e[0m");
  else dstrbuf_appendf(&sb_line, " \e[1ma:\e[0m\e[0;37m%u\e[0m", cfg.audio_track);
  dstrbuf_appendf(&sb_line, " \e[1ms:\e[0m\e[0;37m%s\e[0m", sub_name(cfg.subs));
  if (cfg.duration_s) dstrbuf_appendf(&sb_line, " \e[1md:\e[0m\e[0;37m%ld\e[0m s", cfg.duration_s);
  else dstrbuf_appendf(&sb_line, " \e[1md:\e[0m\e[0;37mforever\e[0m");
  if (cfg.iface_in) dstrbuf_appendf(&sb_line, " \e[1mif:\e[0m\e[0;37m%s\e[0m", cfg.iface_in);
  if (cfg.iface_out) dstrbuf_appendf(&sb_line, " \e[1mof:\e[0m\e[0;37m%s\e[0m", cfg.iface_out);
  if (cfg.ret.enabled) dstrbuf_appendf(&sb_line, " \e[1mret:\e[0m\e[0;37m%s:%u%s\e[0m", cfg.ret.addr, cfg.ret.port, cfg.ret.mc_enabled ? "+mc" : "");
  log_line_ansi("%s", sb_line.buf ? sb_line.buf : "");
  free(sb_out.buf);
  free(sb_line.buf);
  signals_install();
  {
    metrics_exporter_t mx;
    int rc;

    metrics_exporter_init(&mx, METRICS_COMPONENT_REC, cfg.metrics_id, cfg.metrics_sock, (double)cfg.metrics_interval_s);
    rc = record_run(&cfg, &mx);
    metrics_exporter_close(&mx);
    return rc;
  }
}
