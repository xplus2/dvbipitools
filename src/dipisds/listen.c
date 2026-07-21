/* Copyright 2026 dvbipitools authors. Licensed under GPL-3.0-or-later.
 * See NOTICE and LICENSE for details and authorship information. */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

#include "lib/log.h"
#include "lib/net/dvbstp.h"
#include "lib/net/multicast.h"
#include "lib/signal.h"
#include "format_out.h"
#include "input.h"
#include "listen.h"
#include "lib/sds_xml.h"
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

int listen_run(const config_t *cfg) {
  char invocation[128], mcast[80];
  FILE *f;
  mcast_t *m;
  dvbstp_reasm_t *r;
  seen_t seen[SEEN_MAX];
  int seen_count = 0;
  unsigned segments = 0, total_services = 0;
  double deadline;

  mcast_describe(cfg, mcast, sizeof mcast);
  f = strcmp(cfg->output_path, "-") == 0 ? stdout : fopen(cfg->output_path, "w");
  if (!f) {
    log_line("cannot open %s for writing", cfg->output_path);
    return 1;
  }
  m = mcast_open(cfg->family, cfg->mcast_group, cfg->mcast_port, cfg->iface, 500);
  if (!m) {
    log_line("cannot join %s", mcast);
    if (f != stdout)
      fclose(f);
    return 1;
  }
  r = dvbstp_reasm_new();

  snprintf(invocation, sizeof invocation, "%s --listen --mcast %s --timeout %ld", TOOL_NAME, mcast, cfg->timeout_s);
  format_out_init(f, cfg->format, invocation);
  log_line("listening on %s for %lds", mcast, cfg->timeout_s);

  deadline = mono() + (double)cfg->timeout_s;
  while (mono() < deadline && !signal_stop_requested()) {
    unsigned char buf[RECV_BUF];
    ssize_t n = mcast_recv(m, buf, sizeof buf);
    dvbstp_header_t hdr;
    const unsigned char *data;
    size_t len;

    if (n <= 0)
      continue;
    if (!dvbstp_reasm_feed(r, buf, (size_t)n, &hdr, &data, &len))
      continue;
    if (hdr.payload_id != DVBSTP_PAYLOAD_BROADCAST_DISCOVERY)
      continue;
    if (already_seen(seen, &seen_count, &hdr))
      continue;
    segments++;

    if (cfg->format == OUT_XML) {
      format_out_raw(f, cfg->format, data, len);
    } else {
      char *xml = malloc(len + 1);
      sds_service_t entries[SDS_MAX_SERVICES];
      int i, count;
      memcpy(xml, data, len);
      xml[len] = '\0';
      count = sds_parse_broadcast(xml, entries, SDS_MAX_SERVICES);
      for (i = 0; i < count; i++)
        format_out_item(f, cfg->format, &entries[i]);
      total_services += (unsigned)count;
      free(xml);
      if (cfg->verbose)
        log_line("segment %u: %d service%s", segments, count, count == 1 ? "" : "s");
    }
  }
  format_out_close(f, cfg->format);

  dvbstp_reasm_free(r);
  mcast_close(m);
  if (f != stdout)
    fclose(f);
  log_line("found %u service%s in %u segment%s", total_services, total_services == 1 ? "" : "s", segments, segments == 1 ? "" : "s");
  return 0;
}
