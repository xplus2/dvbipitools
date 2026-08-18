/* Copyright 2026 dvbipitools authors. Licensed under GPL-3.0-or-later.
 * See NOTICE and LICENSE for details and authorship information. */

#include <stdio.h>
#include <string.h>

#include "args.h"
#include "lib/ioutil.h"
#include "lib/log.h"
#include "lib/tva/bcg_doc.h"
#include "lib/tva/mapping.h"
#include "lib/tva/tva_xml.h"
#include "lib/tva/xmltv.h"
#include "revmap.h"
#include "suggest.h"
#include "version.h"

static FILE *open_input(const char *path) { return strcmp(path, "-") ? fopen(path, "r") : stdin; }
static FILE *open_output(const char *path) { return strcmp(path, "-") ? fopen(path, "w") : stdout; }

static void write_tva_xml(FILE *out, bcg_doc_t *doc, const mapping_t *map, int verbose) {
  int i;
  for (i = 0; i < doc->channel_count; i++) {
    bcg_channel_t *c = &doc->channels[i];
    char uri[BCG_ID_LEN];
    unsigned tsid, onid, sid;
    if (!mapping_lookup(map, c->id, uri, sizeof uri, &tsid, &onid, &sid)) {
      bufcpy(c->uri, sizeof c->uri, uri);
      c->tsid = tsid;
      c->onid = onid;
      c->sid = sid;
    }
  }
  tva_xml_write(out, doc);
  if (verbose)
    log_line("%d channels, %d programmes read", doc->channel_count, doc->programme_count);
}

static void apply_revmap(bcg_doc_t *doc, const revmap_t *rev) {
  int i;
  for (i = 0; i < doc->channel_count; i++) {
    bcg_channel_t *c = &doc->channels[i];
    const char *preferred = revmap_lookup(rev, c->uri);
    char old_id[BCG_ID_LEN];
    int j;
    if (!preferred)
      continue;
    bufcpy(old_id, sizeof old_id, c->id);
    bufcpy(c->id, sizeof c->id, preferred);
    for (j = 0; j < doc->programme_count; j++)
      if (!strcmp(doc->programmes[j].channel_id, old_id))
        bufcpy(doc->programmes[j].channel_id, sizeof doc->programmes[j].channel_id, preferred);
  }
}

static void write_xmltv(FILE *out, bcg_doc_t *doc, int have_rev, const revmap_t *rev, int verbose) {
  if (have_rev)
    apply_revmap(doc, rev);
  xmltv_write(out, doc, TOOL_NAME);
  if (verbose)
    log_line("%d channels, %d programmes read", doc->channel_count, doc->programme_count);
}

int main(int argc, char **argv) {
  config_t cfg;
  args_status_t st;
  bcg_doc_t doc;
  FILE *in, *out;
  int rc = 0;

  log_set_color(log_color_prescan(argc, argv));
  log_line_ansi("\e[1m%s\e[0m \e[0;32mv%s\e[0m \e[0;37m%s\e[0m \e[0;37m%s\e[0m \e[0;34m%s\e[0m", TOOL_NAME, TOOL_VERSION, BUILD_ARCH, BUILD_TYPE, BUILD_LINK);
  st = args_parse(argc, argv, &cfg);
  if (st == ARGS_OK)
    log_set_color((log_color_t)cfg.color_mode);
  if (st == ARGS_HELP)
    return 0;
  if (st == ARGS_ERR) {
    fprintf(stderr, "try '%s --help' for usage\n", TOOL_NAME);
    return 2;
  }

  in = open_input(cfg.input_path);
  if (!in) {
    fprintf(stderr, TOOL_NAME ": cannot open %s\n", cfg.input_path);
    return 1;
  }
  out = open_output(cfg.output_path);
  if (!out) {
    fprintf(stderr, TOOL_NAME ": cannot open %s\n", cfg.output_path);
    if (in != stdin)
      fclose(in);
    return 1;
  }

  if (cfg.suggest_scan_path) {
    FILE *scan = fopen(cfg.suggest_scan_path, "r");
    if (!scan) {
      fprintf(stderr, TOOL_NAME ": cannot open %s\n", cfg.suggest_scan_path);
      rc = 1;
    } else {
      rc = suggest_map(in, scan, out) ? 1 : 0;
      fclose(scan);
    }
    if (in != stdin)
      fclose(in);
    if (out != stdout)
      fclose(out);
    return rc;
  }

  bcg_doc_init(&doc);

  if (cfg.format == FMT_XMLTV) {
    mapping_t map;
    if (mapping_load(cfg.map_path, &map)) {
      rc = 1;
    } else {
      if (xmltv_read(in, &doc))
        rc = 1;
      else
        write_tva_xml(out, &doc, &map, cfg.verbose);
      mapping_free(&map);
    }
  } else {
    revmap_t rev;
    int have_rev = 0;
    if (cfg.revmap_path) {
      if (revmap_load(cfg.revmap_path, &rev)) {
        rc = 1;
      } else {
        have_rev = 1;
      }
    }
    if (rc == 0) {
      if (tva_xml_read(in, &doc))
        rc = 1;
      else
        write_xmltv(out, &doc, have_rev, &rev, cfg.verbose);
    }
    if (have_rev)
      revmap_free(&rev);
  }

  bcg_doc_free(&doc);
  if (in != stdin)
    fclose(in);
  if (out != stdout)
    fclose(out);
  return rc;
}
