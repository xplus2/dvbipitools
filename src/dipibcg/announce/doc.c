/* Copyright 2026 dvbipitools authors. Licensed under GPL-3.0-or-later.
 * See NOTICE and LICENSE for details and authorship information. */

#include <stdint.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

#include "dipibcg/announce.h"
#include "dipibcg/container.h"
#include "lib/bim/accessunit.h"
#include "lib/bim/bitwriter.h"
#include "lib/bim/strrepo.h"
#include "lib/sys/ioutil.h"
#include "lib/helper/log.h"
#include "lib/metrics/export.h"
#include "lib/net/announce_driver.h"
#include "lib/net/dvbstp.h"
#include "lib/net/multicast.h"
#include "lib/net/netconnect.h"
#include "lib/sys/signal.h"
#include "lib/tva/bcg_doc.h"
#include "lib/tva/mapping.h"
#include "lib/tva/xmltv.h"
#include "dipibcg/version.h"
#include "dipibcg/wrapper.h"
#include "dipibcg/announce/announce_int.h"

int load_doc(const config_t *cfg, bcg_doc_t *out) {
  mapping_t map;
  FILE *in;

  in = strcmp(cfg->input_path, "-") ? fopen(cfg->input_path, "r") : stdin;
  if (!in) {
    log_code("GE000100", "cannot open %s", cfg->input_path);
    return -1;
  }
  bcg_doc_init(out);
  if (xmltv_read(in, out)) {
    if (in != stdin) fclose(in);
    bcg_doc_free(out);
    return -1;
  }
  if (in != stdin) fclose(in);
  if (mapping_load(cfg->map_path, &map)) {
    bcg_doc_free(out);
    return -1;
  }
  for (int i = 0; i < out->channel_count; i++) {
    bcg_channel_t *c = &out->channels[i];
    char uri[BCG_ID_LEN];
    unsigned tsid, onid, sid;
    if (!mapping_lookup(&map, c->id, uri, sizeof uri, &tsid, &onid, &sid)) {
      bufcpy(c->uri, sizeof c->uri, uri);
      c->tsid = tsid;
      c->onid = onid;
      c->sid = sid;
    }
  }
  mapping_free(&map);
  return 0;
}

void version_track(bcg_version_t *v, const unsigned char *cont, size_t cont_len) {
  unsigned char *copy;
  if (v->last && v->last_len == cont_len && !memcmp(v->last, cont, cont_len)) return;
  copy = malloc(cont_len ? cont_len : 1);
  if (!copy) {
    log_code("GE500233", "out of memory, version not advanced");
    return;
  }
  v->version = v->version % 255 + 1;
  v->unsent = 1;
  memcpy(copy, cont, cont_len);
  free(v->last);
  v->last = copy;
  v->last_len = cont_len;
}
