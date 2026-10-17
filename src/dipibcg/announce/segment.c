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

int build_segment(bitwriter_t *bw, const strrepo_writer_t *sw, const accessunit_ref_t *refs, int nrefs, bcg_version_t *ver, int compress, seg_cache_t *seg) {
  size_t bits_len, strs_len, cont_len;
  const unsigned char *bits = bitwriter_data(bw, &bits_len);
  const unsigned char *strs = strrepo_writer_data(sw, &strs_len);
  unsigned char *cont;
  unsigned char *wrapped;
  size_t wrapped_len;
  int rc = SEG_FAILED;

  if (container_build(bits, bits_len, strs, strs_len, refs, nrefs, &cont, &cont_len) != 0) {
    log_code("GE100141", "container build failed");
    return SEG_NO_DOC;
  }
  version_track(ver, cont, cont_len);
  if (wrapper_build(cont, cont_len, compress, &wrapped, &wrapped_len) != 0) {
    log_code("GE100142", "wrapper build failed");
  } else if (wrapped_len > DVBSTP_MAX_SEGMENT) {
    log_code("GE100143", "guide too large (%zu bytes, max %u), reduce window or channels", wrapped_len, DVBSTP_MAX_SEGMENT);
    free(wrapped);
  } else {
    seg->buf = wrapped;
    seg->len = wrapped_len;
    rc = SEG_OK;
  }
  free(cont);
  return rc;
}

int send_segment(mcast_t *m, const seg_cache_t *seg, unsigned version, unsigned segment_id, int64_t budget_ns) {
  if (dvbstp_send_segment(m, &(dvbstp_send_t){.payload_id = DVBSTP_PAYLOAD_BCG_DATA_CONTAINER, .segment_id = segment_id, .segment_version = version, .compr = 1, .want_crc = 1, .pacing_budget_ns = budget_ns}, seg->buf, seg->len) == 0)
    return 0;
  log_code("GE100144", "send failed");
  return -1;
}

static size_t vlu8_len(size_t n) {
  size_t len = 1;
  while (n >= 128) {
    n >>= 7;
    len++;
  }
  return len;
}

int plan_groups(accessunit_scratch_t *sc, const bcg_doc_t *doc, const accessunit_index_t *ix, int **bounds, int *ngroups) {
  int n = doc->channel_count;
  int *b = malloc(sizeof *b * ((size_t)n + 2));
  size_t grp_au_body = 0;
  size_t grp_str_body = 0;
  size_t grp_nfuu = 0;
  int ng = 0;
  int per_channel = n <= (int)BCG_MAX_SEGMENTS;
  bitwriter_t bw;
  strrepo_writer_t sw;
  int rc = -1;

  if (!b) return -1;
  b[0] = 0;
  bitwriter_init(&bw);
  if (strrepo_writer_init(&sw)) goto done;
  for (int c = 0; c < n; c++) {
    size_t au_len;
    size_t str_len;
    size_t au_body;
    size_t str_body;
    int nf = 0;
    bitwriter_reset(&bw);
    strrepo_writer_free(&sw);
    if (strrepo_writer_init(&sw)) goto done;
    if (accessunit_encode_range(sc, doc, ix, c, c + 1, &bw, &sw, &nf, NULL)) goto done;
    bitwriter_data(&bw, &au_len);
    strrepo_writer_data(&sw, &str_len);
    au_body = au_len - vlu8_len((size_t)nf);
    str_body = str_len - 1;
    if (vlu8_len((size_t)nf) + au_body > CONTAINER_MAX_REPO || 1 + str_body > CONTAINER_MAX_REPO) {
      log_code("GE100145", "channel %s alone exceeds %u bytes per repository, reduce its programmes", doc->channels[c].id, CONTAINER_MAX_REPO);
      goto done;
    }
    if (c > 0 && (per_channel || vlu8_len(grp_nfuu + (size_t)nf) + grp_au_body + au_body > CONTAINER_MAX_REPO || 1 + grp_str_body + str_body > CONTAINER_MAX_REPO)) {
      if ((unsigned)ng + 1 >= BCG_MAX_SEGMENTS) {
        log_code("GE100147", "guide needs more than %u segments, reduce channels", BCG_MAX_SEGMENTS);
        goto done;
      }
      b[++ng] = c;
      grp_au_body = 0;
      grp_str_body = 0;
      grp_nfuu = 0;
    }
    grp_au_body += au_body;
    grp_str_body += str_body;
    grp_nfuu += (size_t)nf;
  }
  b[++ng] = n;
  *bounds = b;
  *ngroups = ng;
  b = NULL;
  rc = 0;

done:
  free(b);
  bitwriter_free(&bw);
  strrepo_writer_free(&sw);
  return rc;
}
