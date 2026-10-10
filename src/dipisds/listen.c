/* Copyright 2026 dvbipitools authors. Licensed under GPL-3.0-or-later.
 * See NOTICE and LICENSE for details and authorship information. */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

#include "lib/helper/fileutil.h"
#include "lib/helper/log.h"
#include "lib/net/dvbstp.h"
#include "lib/net/multicast.h"
#include "lib/sys/signal.h"
#include "format_out.h"
#include "input.h"
#include "listen.h"
#include "lib/helper/sds_xml.h"
#include "version.h"

#define RECV_BUF 65536

typedef struct {
  unsigned payload_id, segment_id, version;
  sds_service_t *entries; /* NULL for xml output */
  int count;
  unsigned char *xml; /* NULL unless xml output */
  size_t xml_len;
} listen_seg_t;

static void seg_free(listen_seg_t *s) {
  free(s->entries);
  free(s->xml);
  s->entries = NULL;
  s->xml = NULL;
}

/* 1 stored or replaced, 0 same version already held, -1 table full or oom (old entry kept) */
static int seg_store(listen_seg_t *segs, int *n, const dvbstp_header_t *h, const sds_service_t *entries, int count, const unsigned char *xml,
                     size_t xml_len) {
  listen_seg_t next;
  int i;

  for (i = 0; i < *n; i++)
    if (segs[i].payload_id == h->payload_id && segs[i].segment_id == h->segment_id) break;
  if (i < *n && segs[i].version == h->segment_version) return 0;
  if (i == *n && *n >= LISTEN_SEEN_MAX) return -1;

  memset(&next, 0, sizeof next);
  next.payload_id = h->payload_id;
  next.segment_id = h->segment_id;
  next.version = h->segment_version;
  next.count = count;
  if (entries && count > 0) {
    next.entries = malloc(sizeof *entries * (size_t)count);
    if (!next.entries) return -1;
    memcpy(next.entries, entries, sizeof *entries * (size_t)count);
  }
  if (xml) {
    next.xml = malloc(xml_len);
    if (!next.xml) {
      seg_free(&next);
      return -1;
    }
    memcpy(next.xml, xml, xml_len);
    next.xml_len = xml_len;
  }
  if (i < *n) seg_free(&segs[i]);
  else (*n)++;
  segs[i] = next;
  return 1;
}

int listen_run(const config_t *cfg) {
  char invocation[128], mcast[80];
  FILE *f;
  mcast_t *m;
  dvbstp_reasm_t *r;
  listen_seg_t segs[LISTEN_SEEN_MAX];
  int seg_count = 0;
  unsigned received = 0, total_services = 0;
  double deadline;

  mcast_describe(cfg, mcast, sizeof mcast);
  f = fileutil_open_std(cfg->output_path, "w");
  if (!f) {
    log_line("cannot open %s for writing", cfg->output_path);
    return 1;
  }
  m = mcast_open(cfg->family, cfg->mcast_group, cfg->mcast_port, cfg->iface, 500);
  if (!m) {
    log_line("cannot join %s", mcast);
    if (f != stdout) fclose(f);
    return 1;
  }
  r = dvbstp_reasm_new();
  if (!r) {
    log_line("out of memory");
    mcast_close(m);
    if (f != stdout) fclose(f);
    return 1;
  }

  snprintf(invocation, sizeof invocation, "%s --listen --mcast %s --timeout %ld", TOOL_NAME, mcast, cfg->timeout_s);
  format_out_init(f, cfg->format, invocation);
  log_line("listening on %s for %lds", mcast, cfg->timeout_s);
  deadline = mono_seconds() + (double)cfg->timeout_s;
  while (mono_seconds() < deadline && !signal_stop_requested()) {
    unsigned char buf[RECV_BUF];
    ssize_t n = mcast_recv(m, buf, sizeof buf, NULL);
    dvbstp_header_t hdr;
    const unsigned char *data;
    size_t len;
    if (n <= 0) continue;
    if (!dvbstp_reasm_feed(r, buf, (size_t)n, &hdr, &data, &len)) continue;
    if (hdr.payload_id != DVBSTP_PAYLOAD_BROADCAST_DISCOVERY) continue;

    {
      sds_service_t entries[SDS_MAX_SERVICES];
      int count, truncated, stored;
      count = sds_parse_broadcast((const char *)data, entries, SDS_MAX_SERVICES, &truncated);
      stored = cfg->format == OUT_XML ? seg_store(segs, &seg_count, &hdr, NULL, count, data, len)
                                      : seg_store(segs, &seg_count, &hdr, entries, count, NULL, 0);
      if (stored < 0) log_line("cannot keep segment %u, dropped", hdr.segment_id);
      if (stored <= 0) continue;
      received++;
      if (truncated)
        log_line("segment %u: %d services, more present beyond the %d cap", received, count, SDS_MAX_SERVICES);
      else if (cfg->verbose)
        log_line("segment %u: %d service%s", received, count, count == 1 ? "" : "s");
    }
  }
  for (int i = 0; i < seg_count; i++) {
    if (cfg->format == OUT_XML) format_out_raw(f, cfg->format, segs[i].xml, segs[i].xml_len);
    else
      for (int j = 0; j < segs[i].count; j++) format_out_item(f, cfg->format, &segs[i].entries[j]);
    total_services += (unsigned)segs[i].count;
    seg_free(&segs[i]);
  }
  format_out_close(f, cfg->format);

  dvbstp_reasm_free(r);
  mcast_close(m);
  int rc = 0;
  if (f != stdout && fclose(f)) {
    log_line("error writing %s", cfg->output_path);
    rc = 1;
  }
  log_line("found %u service%s in %u segment%s", total_services, total_services == 1 ? "" : "s", (unsigned)seg_count, seg_count == 1 ? "" : "s");
  return rc;
}
