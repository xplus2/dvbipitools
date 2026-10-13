/* Copyright 2026 dvbipitools authors. Licensed under GPL-3.0-or-later.
 * See NOTICE and LICENSE for details and authorship information. */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

#include "container.h"
#include "lib/bim/accessunit.h"
#include "lib/bim/bimreader.h"
#include "lib/bim/strrepo.h"
#include "lib/helper/fileutil.h"
#include "lib/helper/log.h"
#include "lib/net/dvbstp.h"
#include "lib/net/multicast.h"
#include "lib/sys/signal.h"
#include "lib/tva/bcg_doc.h"
#include "lib/tva/xmltv.h"
#include "listen.h"
#include "version.h"
#include "wrapper.h"

#define RECV_BUF 65536
#define LISTEN_RECV_ERR_MAX 20

static int csv_needs_quote(const char *s) { return strpbrk(s, ",\"") != NULL; }

static void csv_put(FILE *f, const char *s, int quote) {
  if (!quote) {
    fputs(s, f);
    return;
  }
  fputc('"', f);
  for (; *s; s++) {
    if (*s == '"') fputc('"', f);
    fputc(*s, f);
  }
  fputc('"', f);
}

void write_csvmap(const char *path, const bcg_doc_t *doc) {
  FILE *f = fopen(path, "w");
  if (!f) {
    log_code("GS100101", "cannot open %s for writing", path);
    return;
  }
  for (int i = 0; i < doc->channel_count; i++) {
    const bcg_channel_t *c = &doc->channels[i];
    if (!c->uri[0]) continue;
    if (strpbrk(c->id, "\r\n") || strpbrk(c->uri, "\r\n")) {
      log_code("GW100146", "channel %s: line break in id or uri, skipped in csv map", c->id);
      continue;
    }
    int quote = csv_needs_quote(c->id) || csv_needs_quote(c->uri);
    csv_put(f, c->id, quote);
    fputc(',', f);
    csv_put(f, c->uri, quote);
    fprintf(f, ",%u,%u,%u\n", c->tsid, c->onid, c->sid);
  }
  if (fclose(f)) log_code("GE100102", "error writing %s", path);
}

typedef struct {
  unsigned segment_id;
  int has_provider_id;
  unsigned provider_id;
  bcg_doc_t doc;
} seg_slot_t;

/* latest decode per (provider, segment_id) replaces the previous one. takes ownership of *doc on success, 0 ok, -1 oom */
static int slot_store(seg_slot_t **slots, int *n, const dvbstp_header_t *h, const bcg_doc_t *doc) {
  for (int i = 0; i < *n; i++) {
    if ((*slots)[i].segment_id != h->segment_id || (*slots)[i].has_provider_id != h->has_provider_id || (*slots)[i].provider_id != h->provider_id) continue;
    bcg_doc_free(&(*slots)[i].doc);
    (*slots)[i].doc = *doc;
    return 0;
  }
  seg_slot_t *np = realloc(*slots, sizeof **slots * (size_t)(*n + 1));
  if (!np) return -1;
  *slots = np;
  np[*n].segment_id = h->segment_id;
  np[*n].has_provider_id = h->has_provider_id;
  np[*n].provider_id = h->provider_id;
  np[*n].doc = *doc;
  (*n)++;
  return 0;
}

/* out := all slots' channels and programmes in slot order. 0 ok, -1 oom (out freed) */
static int slots_merge(const seg_slot_t *slots, int n, bcg_doc_t *out) {
  bcg_doc_init(out);
  for (int i = 0; i < n; i++) {
    const bcg_doc_t *d = &slots[i].doc;
    for (int k = 0; k < d->channel_count; k++) {
      bcg_channel_t *c = bcg_add_channel(out);
      if (!c) goto oom;
      *c = d->channels[k];
    }
    for (int k = 0; k < d->programme_count; k++) {
      bcg_programme_t *p = bcg_add_programme(out);
      if (!p) goto oom;
      if (bcg_programme_copy(p, &d->programmes[k])) goto oom;
    }
  }
  return 0;
oom:
  bcg_doc_free(out);
  return -1;
}

int listen_run(const config_t *cfg) {
  char mcast[80];
  mcast_t *m;
  dvbstp_reasm_t *r;
  dvbstp_seen_t seen[LISTEN_SEEN_MAX];
  int seen_count = 0;
  unsigned segments = 0;
  unsigned captures = 0;
  double deadline;
  bcg_doc_t doc;
  int have_doc = 0;
  seg_slot_t *slots = NULL;
  int nslots = 0;
  int recv_errors = 0;
  accessunit_scratch_t sc;
  accessunit_scratch_init(&sc);
  mcast_describe(cfg, mcast, sizeof mcast);
  m = mcast_open(cfg->family, cfg->mcast_group, cfg->mcast_port, cfg->iface, 500);
  if (!m) {
    log_code("GS500100", "cannot join %s", mcast);
    return 1;
  }
  r = dvbstp_reasm_new();
  if (!r) {
    log_code("GS500101", "out of memory");
    mcast_close(m);
    return 1;
  }
  bcg_doc_init(&doc);
  log_line("listening on %s for %lds", mcast, cfg->timeout_s);
  deadline = mono_seconds() + (double)cfg->timeout_s;
  while (mono_seconds() < deadline && !signal_stop_requested()) {
    unsigned char buf[RECV_BUF];
    ssize_t n = mcast_recv(m, buf, sizeof buf, NULL);
    dvbstp_header_t hdr;
    const unsigned char *data;
    size_t len;
    const unsigned char *au;
    const unsigned char *sr_bytes;
    size_t au_len;
    size_t sr_len;
    unsigned char *unwrapped;
    size_t unwrapped_len;
    bitreader_t br;
    strrepo_reader_t sr;
    bcg_doc_t candidate;
    accessunit_ref_t *refs = NULL;
    int nrefs = 0;
    int nfuu = 0;
    if (n < 0) {
      if (++recv_errors >= LISTEN_RECV_ERR_MAX) {
        log_code("GE500240", "receive on %s failed, giving up", mcast);
        break;
      }
      sleep_interruptible(0.1);
      continue;
    }
    recv_errors = 0;
    if (n == 0) continue;
    if (!dvbstp_reasm_feed(r, buf, (size_t)n, &hdr, &data, &len)) continue;
    if (hdr.payload_id != DVBSTP_PAYLOAD_BCG_DATA_CONTAINER) continue;
    if (dvbstp_already_seen(seen, &seen_count, &hdr)) continue;
    segments++;

    if (wrapper_parse(data, len, &unwrapped, &unwrapped_len)) continue;
    if (container_parse(unwrapped, unwrapped_len, &au, &au_len, &sr_bytes, &sr_len) || container_parse_refs(unwrapped, unwrapped_len, &refs, &nrefs)) {
      free(unwrapped);
      continue;
    }
    bitreader_init(&br, au, au_len);
    if (strrepo_reader_init(&sr, sr_bytes, sr_len)) {
      free(refs);
      free(unwrapped);
      continue;
    }
    bcg_doc_init(&candidate);
    if (accessunit_decode_refs(&sc, &br, &sr, refs, nrefs, &candidate, &nfuu)) {
      bcg_doc_free(&candidate);
      free(refs);
      free(unwrapped);
      continue;
    }
    free(refs);
    free(unwrapped);
    captures++;
    if (cfg->verbose) log_code("GD700100", "segment %u: %d channels, %d programmes, %d fragments", segments, candidate.channel_count, candidate.programme_count, nfuu);
    if (slot_store(&slots, &nslots, &hdr, &candidate)) {
      bcg_doc_free(&candidate);
      log_code("GS500101", "out of memory");
    }
  }

  if (nslots > 0) {
    if (slots_merge(slots, nslots, &doc)) log_code("GS500101", "out of memory");
    else have_doc = 1;
  }
  for (int i = 0; i < nslots; i++) bcg_doc_free(&slots[i].doc);
  free(slots);

  int write_failed = 0;
  if (have_doc) {
    FILE *f = fileutil_open_std(cfg->output_path, "w");
    if (!f) {
      log_code("GE100101", "cannot open %s for writing", cfg->output_path);
      write_failed = 1;
    } else {
      xmltv_write(f, &doc, TOOL_NAME);
      if (f != stdout && fclose(f)) {
        log_code("GE100102", "error writing %s", cfg->output_path);
        write_failed = 1;
      }
    }
    if (cfg->csvmap_path) write_csvmap(cfg->csvmap_path, &doc);
  }

  accessunit_scratch_free(&sc);
  dvbstp_reasm_free(r);
  mcast_close(m);
  if (have_doc) bcg_doc_free(&doc);
  log_line("captured %u time%s in %u segment%s", captures, captures == 1 ? "" : "s", segments, segments == 1 ? "" : "s");
  return have_doc && !write_failed ? 0 : 1;
}
