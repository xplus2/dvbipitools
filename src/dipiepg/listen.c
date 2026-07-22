/* Copyright 2026 dvbipitools authors. Licensed under GPL-3.0-or-later.
 * See NOTICE and LICENSE for details and authorship information. */

#include <stdio.h>
#include <string.h>
#include <time.h>

#include "container.h"
#include "lib/bim/accessunit.h"
#include "lib/bim/bitreader.h"
#include "lib/bim/strrepo.h"
#include "lib/log.h"
#include "lib/net/dvbstp.h"
#include "lib/net/multicast.h"
#include "lib/signal.h"
#include "lib/tva/epg_doc.h"
#include "lib/tva/xmltv.h"
#include "listen.h"
#include "version.h"

#define RECV_BUF 65536
#define SEEN_MAX 16

typedef struct {
  unsigned payload_id, segment_id, version;
} seen_t;

static double mono(void) {
  struct timespec t;
  clock_gettime(CLOCK_MONOTONIC, &t);
  return (double)t.tv_sec + (double)t.tv_nsec / 1e9;
}

static int already_seen(seen_t *seen, int *count, const dvbstp_header_t *h) {
  int i;
  for (i = 0; i < *count; i++)
    if (seen[i].payload_id == h->payload_id && seen[i].segment_id == h->segment_id && seen[i].version == h->segment_version)
      return 1;
  if (*count < SEEN_MAX) {
    seen[*count].payload_id = h->payload_id;
    seen[*count].segment_id = h->segment_id;
    seen[*count].version = h->segment_version;
    (*count)++;
  }
  return 0;
}

static void write_csvmap(const char *path, const epg_doc_t *doc) {
  FILE *f = fopen(path, "w");
  int i;
  if (!f) {
    log_line("cannot open %s for writing", path);
    return;
  }
  for (i = 0; i < doc->channel_count; i++) {
    const epg_channel_t *c = &doc->channels[i];
    if (!c->uri[0])
      continue;
    fprintf(f, "%s,%s,%u,%u,%u\n", c->id, c->uri, c->tsid, c->onid, c->sid);
  }
  fclose(f);
}

int listen_run(const config_t *cfg) {
  char mcast[80];
  mcast_t *m;
  dvbstp_reasm_t *r;
  seen_t seen[SEEN_MAX];
  int seen_count = 0;
  unsigned segments = 0, captures = 0;
  double deadline;
  epg_doc_t doc;
  int have_doc = 0;

  mcast_describe(cfg, mcast, sizeof mcast);
  m = mcast_open(cfg->family, cfg->mcast_group, cfg->mcast_port, cfg->iface, 500);
  if (!m) {
    log_line("cannot join %s", mcast);
    return 1;
  }
  r = dvbstp_reasm_new();
  epg_doc_init(&doc);

  log_line("listening on %s for %lds", mcast, cfg->timeout_s);

  deadline = mono() + (double)cfg->timeout_s;
  while (mono() < deadline && !signal_stop_requested()) {
    unsigned char buf[RECV_BUF];
    ssize_t n = mcast_recv(m, buf, sizeof buf);
    dvbstp_header_t hdr;
    const unsigned char *data;
    size_t len;
    const unsigned char *au, *sr_bytes;
    size_t au_len, sr_len;
    bitreader_t br;
    strrepo_reader_t sr;
    epg_doc_t candidate;
    int nfuu = 0;

    if (n <= 0)
      continue;
    if (!dvbstp_reasm_feed(r, buf, (size_t)n, &hdr, &data, &len))
      continue;
    if (hdr.payload_id != DVBSTP_PAYLOAD_BCG_DATA_CONTAINER)
      continue;
    if (already_seen(seen, &seen_count, &hdr))
      continue;
    segments++;

    if (container_parse(data, len, &au, &au_len, &sr_bytes, &sr_len))
      continue;
    bitreader_init(&br, au, au_len);
    if (strrepo_reader_init(&sr, sr_bytes, sr_len))
      continue;
    epg_doc_init(&candidate);
    if (accessunit_decode(&br, &sr, &candidate, &nfuu)) {
      epg_doc_free(&candidate);
      continue;
    }
    if (have_doc)
      epg_doc_free(&doc);
    doc = candidate;
    have_doc = 1;
    captures++;
    if (cfg->verbose)
      log_line("segment %u: %d channels, %d programmes, %d fragments", segments, doc.channel_count, doc.programme_count, nfuu);
  }

  if (have_doc) {
    FILE *f = strcmp(cfg->output_path, "-") == 0 ? stdout : fopen(cfg->output_path, "w");
    if (!f) {
      log_line("cannot open %s for writing", cfg->output_path);
    } else {
      xmltv_write(f, &doc, TOOL_NAME);
      if (f != stdout)
        fclose(f);
    }
    if (cfg->csvmap_path)
      write_csvmap(cfg->csvmap_path, &doc);
  }

  dvbstp_reasm_free(r);
  mcast_close(m);
  epg_doc_free(&doc);
  log_line("captured %u time%s in %u segment%s", captures, captures == 1 ? "" : "s", segments, segments == 1 ? "" : "s");
  return have_doc ? 0 : 1;
}
