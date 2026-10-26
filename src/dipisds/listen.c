/* Copyright 2026 dvbipitools authors. Licensed under GPL-3.0-or-later.
 * See NOTICE and LICENSE for details and authorship information. */

#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

#include "lib/helper/fileutil.h"
#include "lib/helper/log.h"
#include "lib/net/dvbstp.h"
#include "lib/net/multicast.h"
#include "format_out.h"
#include "listen.h"
#include "lib/helper/sds_xml.h"
#include "lib/bim/sdns.h"
#include "lib/bim/sdnsbim.h"
#include "version.h"


typedef struct {
  dvbstp_key_t key;
  unsigned version;
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

/* slot index of segment, n if absent */
static int seg_find(const listen_seg_t *segs, int n, const dvbstp_header_t *h) {
  int i;
  for (i = 0; i < n && !dvbstp_key_match(&segs[i].key, h); i++)
    ;
  return i;
}

/* 1 stored or replaced, 0 same version already held, -1 table full or oom (old entry kept) */
static int seg_store(listen_seg_t *segs, int *n, const dvbstp_header_t *h, const sds_service_t *entries, int count, const unsigned char *xml,
                     size_t xml_len) {
  listen_seg_t next;
  int i = seg_find(segs, *n, h);

  if (i < *n && segs[i].version == h->segment_version) return 0;
  if (i == *n && *n >= LISTEN_SEEN_MAX) return -1;

  memset(&next, 0, sizeof next);
  dvbstp_key_set(&next.key, h);
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

typedef struct {
  const config_t *cfg;
  listen_seg_t segs[LISTEN_SEEN_MAX];
  int seg_count;
  unsigned received;
  sds_service_t entries[SDS_MAX_SERVICES];
  bim_sdnsdoc_t sch; /* with -s */
  int have_schema;
  dvbstp_key_t drop_key[LISTEN_SEEN_MAX]; /* undecodable segments, reported once per version */
  unsigned drop_ver[LISTEN_SEEN_MAX];
  int drop_count;
} listen_ctx_t;

/* slot index of dropped segment, drop_count if absent */
static int drop_find(const listen_ctx_t *c, const dvbstp_header_t *h) {
  int i;
  for (i = 0; i < c->drop_count && !dvbstp_key_match(&c->drop_key[i], h); i++)
    ;
  return i;
}

static void drop_note(listen_ctx_t *c, int i, const dvbstp_header_t *h) {
  if (i == c->drop_count) {
    if (c->drop_count >= LISTEN_SEEN_MAX) return;
    dvbstp_key_set(&c->drop_key[c->drop_count++], h);
  }
  c->drop_ver[i] = h->segment_version;
}


static int on_segment(void *ctx, const dvbstp_header_t *hdr, const unsigned char *data, size_t len) {
  listen_ctx_t *c = ctx;
  int known = seg_find(c->segs, c->seg_count, hdr);
  int dropped;
  const char *xml;
  size_t xml_len;
  char *owned;
  int count;
  int truncated;
  int stored;
  int rc;
  if (hdr->payload_id != DVBSTP_PAYLOAD_BROADCAST_DISCOVERY) return 0;
  if (known < c->seg_count && c->segs[known].version == hdr->segment_version) return 0;
  dropped = drop_find(c, hdr);
  if (dropped < c->drop_count && c->drop_ver[dropped] == hdr->segment_version) return 0;
  rc = sdns_doc_segment_xml(c->have_schema ? &c->sch : NULL, hdr->compr, data, len, c->cfg->sdns_ns, &xml, &xml_len, &owned);
  if (rc == -1) log_code("AW400243", "segment %u: BiM-encoded SD&S not decodable, dropped", hdr->segment_id);
  if (rc == -2) log_code("AW400244", "segment %u: compression %u not supported, dropped", hdr->segment_id, hdr->compr);
  if (rc) {
    drop_note(c, dropped, hdr);
    return 0;
  }
  count = sds_parse_broadcast(xml, c->entries, SDS_MAX_SERVICES, &truncated);
  stored = c->cfg->format == OUT_XML ? seg_store(c->segs, &c->seg_count, hdr, NULL, count, (const unsigned char *)xml, xml_len)
                                     : seg_store(c->segs, &c->seg_count, hdr, c->entries, count, NULL, 0);
  free(owned);
  if (stored < 0) log_code("AW700107", "cannot keep segment %u, dropped", hdr->segment_id);
  if (stored <= 0) return 0;
  c->received++;
  if (truncated)
    log_code("AW700108", "segment %u: %d services, more present beyond the %d cap", c->received, count, SDS_MAX_SERVICES);
  else if (c->cfg->verbose)
    log_code("AD700109", "segment %u: %d service%s", c->received, count, count == 1 ? "" : "s");
  return 0;
}

int listen_run(const config_t *cfg) {
  char invocation[128], mcast[80];
  FILE *f;
  mcast_t *m;
  listen_ctx_t *c;
  unsigned total_services = 0;
  int rc;

  mcast_describe(cfg, mcast, sizeof mcast);
  f = fileutil_open_std(cfg->output_path, "w");
  if (!f) {
    log_code("AS100101", "cannot open %s for writing", cfg->output_path);
    return 1;
  }
  m = mcast_open(cfg->family, cfg->mcast_group, cfg->mcast_port, cfg->iface, 500);
  if (!m) {
    log_code("AS500100", "cannot join %s", mcast);
    if (f != stdout) fclose(f);
    return 1;
  }
  c = calloc(1, sizeof *c);
  if (!c) {
    log_code("AS500101", "out of memory");
    mcast_close(m);
    if (f != stdout) fclose(f);
    return 1;
  }
  c->cfg = cfg;
  if (cfg->nschemas && sdns_doc_open(&c->sch, "AS", cfg->schemas, cfg->nschemas, cfg->sdns_ns)) {
    free(c);
    mcast_close(m);
    if (f != stdout) fclose(f);
    return 1;
  }
  c->have_schema = cfg->nschemas > 0;

  snprintf(invocation, sizeof invocation, "%s --listen --mcast %s --timeout %ld", TOOL_NAME, mcast, cfg->timeout_s);
  format_out_init(f, cfg->format, invocation);
  if (cfg->timeout_s) log_line("listening on %s for %lds", mcast, cfg->timeout_s);
  else log_line("listening on %s until stopped", mcast);
  rc = dvbstp_listen(m, cfg->timeout_s ? (double)cfg->timeout_s : INFINITY, on_segment, c);
  if (rc == -1) log_code("AE500240", "receive on %s failed, giving up", mcast);
  if (rc == -2) log_code("AE500101", "out of memory");
  for (int i = 0; i < c->seg_count; i++) {
    if (cfg->format == OUT_XML) format_out_raw(f, cfg->format, c->segs[i].xml, c->segs[i].xml_len);
    else
      for (int j = 0; j < c->segs[i].count; j++) format_out_item(f, cfg->format, &c->segs[i].entries[j]);
    total_services += (unsigned)c->segs[i].count;
    seg_free(&c->segs[i]);
  }
  format_out_close(f, cfg->format);
  mcast_close(m);
  rc = rc != 0;
  if (f != stdout && fclose(f)) {
    log_code("AE100102", "error writing %s", cfg->output_path);
    rc = 1;
  }
  log_line("found %u service%s in %u segment%s", total_services, total_services == 1 ? "" : "s", (unsigned)c->seg_count, c->seg_count == 1 ? "" : "s");
  if (c->have_schema) bim_sdnsdoc_close(&c->sch);
  free(c);
  return rc;
}
