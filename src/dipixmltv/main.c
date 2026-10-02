/* Copyright 2026 dvbipitools authors. Licensed under GPL-3.0-or-later.
 * See NOTICE and LICENSE for details and authorship information. */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "cli/args.h"
#include "lib/helper/fileutil.h"
#include "lib/sys/ioutil.h"
#include "lib/helper/log.h"
#include "lib/helper/toolmain.h"
#include "lib/tva/bcg_doc.h"
#include "lib/tva/mapping.h"
#include "lib/tva/tva_xml.h"
#include "lib/tva/xmltv.h"
#include "revmap.h"
#include "suggest.h"
#include "version.h"

static void write_tva_xml(FILE *out, bcg_doc_t *doc, const mapping_t *map, int verbose) {
  for (int i = 0; i < doc->channel_count; i++) {
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
  if (verbose) log_line("%d channels, %d programmes read", doc->channel_count, doc->programme_count);
}

typedef struct {
  char old_id[BCG_ID_LEN];
  const char *preferred;
  int channel_idx;
} revmap_rename_t;

static int revmap_rename_cmp(const void *a, const void *b) {
  return strcmp(((const revmap_rename_t *)a)->old_id, ((const revmap_rename_t *)b)->old_id);
}

static void apply_revmap(bcg_doc_t *doc, const revmap_t *rev) {
  revmap_rename_t *renames;
  int n = 0;
  int i;

  if (doc->channel_count == 0) return;
  renames = malloc(sizeof *renames * (size_t)doc->channel_count);
  if (!renames) return;

  for (i = 0; i < doc->channel_count; i++) {
    const bcg_channel_t *c = &doc->channels[i];
    const char *preferred = revmap_lookup(rev, c->uri);
    if (!preferred) continue;
    bufcpy(renames[n].old_id, sizeof renames[n].old_id, c->id);
    renames[n].preferred = preferred;
    renames[n].channel_idx = i;
    n++;
  }
  if (n > 0) {
    qsort(renames, (size_t)n, sizeof *renames, revmap_rename_cmp);
    for (int j = 0; j < doc->programme_count; j++) {
      int lo = 0;
      int hi = n - 1;
      while (lo <= hi) {
        int mid = (lo + hi) / 2;
        int c2 = strcmp(doc->programmes[j].channel_id, renames[mid].old_id);
        if (c2 == 0) {
          bufcpy(doc->programmes[j].channel_id, sizeof doc->programmes[j].channel_id, renames[mid].preferred);
          break;
        }
        if (c2 < 0) hi = mid - 1; else lo = mid + 1;
      }
    }
    for (i = 0; i < n; i++)
      bufcpy(doc->channels[renames[i].channel_idx].id, sizeof doc->channels[renames[i].channel_idx].id, renames[i].preferred);
  }
  free(renames);
}

static void write_xmltv(FILE *out, bcg_doc_t *doc, int have_rev, const revmap_t *rev, int verbose) {
  if (have_rev) apply_revmap(doc, rev);
  xmltv_write(out, doc, TOOL_NAME);
  if (verbose) log_line("%d channels, %d programmes read", doc->channel_count, doc->programme_count);
}

int main(int argc, char **argv) {
  config_t cfg;
  bcg_doc_t doc;
  FILE *in, *out;
  int rc = 0;

  TOOLMAIN_STARTUP(argc, argv, &cfg, args_parse);
  in = fileutil_open_std(cfg.input_path, "r");
  if (!in) {
    fprintf(stderr, TOOL_NAME ": cannot open %s\n", cfg.input_path);
    return 1;
  }
  out = fileutil_open_std(cfg.output_path, "w");
  if (!out) {
    fprintf(stderr, TOOL_NAME ": cannot open %s\n", cfg.output_path);
    if (in != stdin) fclose(in);
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
    if (in != stdin) fclose(in);
    if (out != stdout && fclose(out) && rc == 0) {
      fprintf(stderr, TOOL_NAME ": error writing %s\n", cfg.output_path);
      rc = 1;
    }
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
      if (tva_xml_read(in, &doc)) rc = 1;
      else write_xmltv(out, &doc, have_rev, &rev, cfg.verbose);
    }
    if (have_rev) revmap_free(&rev);
  }
  bcg_doc_free(&doc);
  if (in != stdin) fclose(in);
  if (out != stdout && fclose(out) && rc == 0) {
    fprintf(stderr, TOOL_NAME ": error writing %s\n", cfg.output_path);
    rc = 1;
  }
  return rc;
}
