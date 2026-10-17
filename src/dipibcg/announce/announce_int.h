/* Copyright 2026 dvbipitools authors. Licensed under GPL-3.0-or-later.
 * See NOTICE and LICENSE for details and authorship information. */

#ifndef DIPIBCG_ANNOUNCE_ANNOUNCE_INT_H
#define DIPIBCG_ANNOUNCE_ANNOUNCE_INT_H

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

typedef struct {
  int sources_up;
  unsigned long long source_errors_total[NET_ERR_COUNT];
  unsigned long long documents_generated_total;
  unsigned long long document_errors_total;
  unsigned long long publications_total;
  unsigned long long publication_errors_total;
  double last_success_time; /* unix seconds, 0 = never */
} bcg_metrics_t;
enum { SEG_OK, SEG_FAILED, SEG_NO_DOC };

#define BCG_MAX_SEGMENTS 0xFFFFu

typedef struct {
  unsigned char *buf; /* wrapped segment payload */
  size_t len;
} seg_cache_t;
typedef struct {
  const config_t *cfg;
  metrics_exporter_t *mx;
  bcg_doc_t doc;
  prog_time_t *times; /* per doc programme, parsed at load */
  int *sel;           /* programme indices of last build */
  int *sel_new;
  int nsel;
  bcg_doc_t win;      /* view on doc for sel */
  long sched_start;
  long sched_end;
  int have_sched;
  seg_cache_t *seg;   /* per container, wrapped */
  int nseg;
  int cache_valid;
  int ngroups;
  int nfuu_total;
  int nchan_prog; /* windowed channels with events */
  bcg_metrics_t bm;
  accessunit_scratch_t sc;
  bcg_version_t *vers; /* per segment_id - 1 */
  int vers_n;
  int metrics_on;
} bcg_announce_ctx_t;

long now_minutes(void);
int build_segment(bitwriter_t *bw, const strrepo_writer_t *sw, const accessunit_ref_t *refs, int nrefs, bcg_version_t *ver, int compress, seg_cache_t *seg);
int send_segment(mcast_t *m, const seg_cache_t *seg, unsigned version, unsigned segment_id, int64_t budget_ns);
int plan_groups(accessunit_scratch_t *sc, const bcg_doc_t *doc, const accessunit_index_t *ix, int **bounds, int *ngroups);

#endif
