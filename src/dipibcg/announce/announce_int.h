/* Copyright 2026 dvbipitools authors. Licensed under GPL-3.0-or-later.
 * See NOTICE and LICENSE for details and authorship information. */

#ifndef DIPIBCG_ANNOUNCE_ANNOUNCE_INT_H
#define DIPIBCG_ANNOUNCE_ANNOUNCE_INT_H

#include <stdint.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include "dipibcg/announce.h"
#include "lib/bim/bcgau.h"
#include "lib/bim/bitwriter.h"
#include "lib/bim/index.h"
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
#include "lib/tva/tva_xml.h"
#include "lib/tva/xmltv.h"
#include "dipibcg/version.h"
#include "dipibcg/schema.h"

/* set while a failed build is retried: its errors not logged again */
extern int announce_log_quiet;
#define build_log(...) \
  do { \
    if (!announce_log_quiet) log_code(__VA_ARGS__); \
  } while (0)

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
/* TS 102 822-3-2 4.6.1.1 fragment_id and fragment_version, kept per fragment key across builds */
typedef struct {
  char key[TVA_CRID_BUF + 16]; /* kind letter, ':', crid or channel id, "#n" duplicates */
  uint32_t id;
  unsigned gen; /* build last emitting it */
  bcg_version_t ver;
} frag_state_t;

/* -f xmltv input */
typedef struct {
  bcg_doc_t doc;
  prog_time_t *times; /* per doc programme, parsed at load */
  int *sel;           /* programme indices of last build */
  int *sel_new;
  int nsel;
} bcg_src_t;

/* containers of one build. versions shallow-copied into a build, see version_stage */
typedef struct {
  seg_cache_t *seg; /* per container, wrapped */
  int nseg;
  bcg_version_t *vers; /* per segment_id - 1, kept past nseg */
  int vers_n;
  seg_cache_t *iseg; /* [0]: index list container, then sub-index containers */
  uint16_t *iseg_id;
  int niseg;
  bcg_version_t *ivers;
  int ivers_n;
  seg_cache_t main_seg; /* TVAMain, empty without publisher */
  bcg_version_t main_ver;
  bcg_version_t init_ver; /* DVB-TVA-init */
  int nfuu;
  int nchan_prog; /* windowed channels with events */
  int nevents;
  long sched_start;
  long sched_end;
  int have_sched;
  int *gsel;        /* doc programme indices per channel container: gsel[goff[g]..goff[g+1]) */
  int *goff;        /* NULL: no container per channel */
  unsigned src_gen; /* ctx->src_gen built from */
  int nreused;      /* containers taken unchanged from previous build */
  const bcg_schema_t *main_sch; /* schema main_seg built with */
} bcg_out_t;

typedef struct {
  const config_t *cfg;
  metrics_exporter_t *mx;
  bcg_src_t src;
  unsigned src_gen; /* bumped per loaded src */
  bcg_out_t out; /* last good build, sent */
  int cache_valid; /* out matches src.sel / tfrag */
  int build_failing; /* last cycle build failed, errors logged */
  bcg_metrics_t bm;
  bcgau_scratch_t sc;
  bcg_schema_t *sch;
  dvbtva_fuu_t *tfrag; /* -f tva|bim */
  int ntfrag;
  frag_state_t *frags; /* sorted by key */
  int nfrags;
  int frags_cap;
  uint32_t frag_next; /* last fragment_id handed out */
  unsigned frag_gen;
  unsigned char *fbuf; /* fragment B for version_track */
  int fbuf_cap;
  int metrics_on;
  unsigned au_flags; /* ACCESSUNIT_* */
  bim_index_input_t ix_in[BIM_INDEX_KINDS];
  char **ix_keys; /* owned key strings of ix_in */
  int ix_nkeys;
  int ix_keys_cap;
} bcg_announce_ctx_t;

/* -f tva|bim: fresh schema in *sch (heap), fragments in *parts. want_ns: namespace required, NULL any.
   0 ok, -1 error (logged, nothing returned) */
int tvadoc_load(const config_t *cfg, int startup, const char *want_ns, bcg_schema_t **sch, dvbtva_fuu_t **parts, int *nparts);
long now_minutes(void);
/* plain TVA XML as segment, version staged by content */
int build_plain(const char *xml, size_t len, bcg_version_t *ver, seg_cache_t *seg);
int build_segment(bitwriter_t *bw, const strrepo_writer_t *sw, const bcgau_ref_t *refs, int nrefs, bcg_version_t *ver, int compress, seg_cache_t *seg);
int send_segment(mcast_t *m, unsigned compr, const seg_cache_t *seg, unsigned version, unsigned segment_id, int64_t budget_ns);
int send_tva_init(mcast_t *m, const bcg_schema_t *s, unsigned version);
/* sets frag_id + version of refs (one container fuus), sort refs by frag_id. 0 ok, -1 oom or ids exhausted */
int frag_assign(bcg_announce_ctx_t *ctx, const bcg_doc_t *doc, const unsigned char *au, size_t au_len, const unsigned char *strs, size_t strs_len, bcgau_ref_t *refs, int n);
/* key of fragment i, 0 ok */
typedef int (*frag_key_fn)(const void *kctx, int i, char *buf, size_t cap);
int key_node(const void *kctx, int i, char *buf, size_t cap);
int frag_assign_keys(bcg_announce_ctx_t *ctx, frag_key_fn key, const void *kctx, const unsigned char *au, size_t au_len, const unsigned char *strs, size_t strs_len, bcgau_ref_t *refs, int n);
/* frag_id + version of refs of unchanged fragments, kept for this build. 0 ok, -1 oom or ids exhausted */
int frag_touch(bcg_announce_ctx_t *ctx, const bcg_doc_t *doc, bcgau_ref_t *refs, int n);
int frag_touch_keys(bcg_announce_ctx_t *ctx, frag_key_fn key, const void *kctx, bcgau_ref_t *refs, int n);
/* all containers of src.sel or tfrag into ctx->out, channels with unchanged selection reused. 0 ok, -1 error, ctx->out unchanged */
int announce_build(bcg_announce_ctx_t *ctx, long sched_start, long sched_end, int have_sched);
/* drop unemitted fragments by build frag_gen */
void frag_prune(bcg_announce_ctx_t *ctx);
void frag_free(bcg_announce_ctx_t *ctx);
/* st->main_seg from cfg publisher/lang, fragment id from frag state, previous one reused for same schema. 0 ok or none, -1 error */
int build_tva_main(bcg_announce_ctx_t *ctx, bcg_out_t *st);
/* index entries of one data container, refs after frag_assign. 0 ok, -1 oom */
int index_collect(bcg_announce_ctx_t *ctx, const bcg_doc_t *doc, const bcg_doc_index_t *ix, const bcgau_ref_t *refs, int n, unsigned container);
/* st->iseg from collected entries, sub-index ids after first_id. 0 ok or none, -1 error */
int index_build(bcg_announce_ctx_t *ctx, bcg_out_t *st, unsigned first_id);
void index_reset(bcg_announce_ctx_t *ctx);
void index_free(bcg_announce_ctx_t *ctx);
/* st versions as shallow copy of base. 0 ok, -1 oom */
int out_stage(bcg_out_t *st, const bcg_out_t *base);
/* base takes st (st emptied) */
void out_commit(bcg_out_t *base, bcg_out_t *st);
/* st dropped, base untouched */
void out_discard(bcg_out_t *st, const bcg_out_t *base);
void out_free(bcg_out_t *o);
int send_tva_main(mcast_t *m, unsigned compr, const seg_cache_t *seg, unsigned version);
int send_index(mcast_t *m, const seg_cache_t *seg, unsigned version, unsigned container_id, int64_t budget_ns);
/* access unit and string repository of nfuu fragments within BIM_REPO_MAX. 1 fits */
int segment_fits(bitwriter_t *bw, const strrepo_writer_t *sw, size_t nfuu);
int plan_groups_frags(bcgau_scratch_t *sc, const bcgau_t *a, const dvbtva_fuu_t *f, int n, int **bounds, int *ngroups);
/* container per channel up to BCG_MAX_SEGMENTS (size checked when built), packed above */
int plan_groups(bcgau_scratch_t *sc, const bcgau_t *a, const bcg_doc_t *doc, const bcg_doc_index_t *ix, unsigned au_flags, int **bounds, int *ngroups);

#endif
