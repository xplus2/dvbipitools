/* Copyright 2026 dvbipitools authors. Licensed under GPL-3.0-or-later.
 * See NOTICE and LICENSE for details and authorship information. */

#include <check.h>
#include <fcntl.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#include "../alloc_fail.h"
#include "../bcg_schema_fixture.h"
#include "../log_capture.h"
#include "dipibcg/announce/announce_int.h"
#include "dipibcg/schema.h"
#include "lib/bim/bcgau.h"
#include "lib/bim/bcgfrag.h"
#include "lib/bim/dvbtva.h"

typedef struct {
  char dir[64];
  config_t cfg;
  bcg_announce_ctx_t ctx;
} fx_t;

static void fx_open(fx_t *f, bcg_enc_t enc, int compress) {
  memset(f, 0, sizeof *f);
  bcg_fixture_make(f->dir);
  f->cfg.schemas[0] = f->dir;
  f->cfg.nschemas = 1;
  f->cfg.encoding = enc;
  f->cfg.compress = compress;
  f->ctx.cfg = &f->cfg;
  f->ctx.sch = calloc(1, sizeof *f->ctx.sch);
  ck_assert_ptr_nonnull(f->ctx.sch);
  ck_assert_int_eq(bcg_schema_open(f->ctx.sch, &f->cfg, BCG_FIXTURE_NS, 1), 0);
  bcgau_scratch_init(&f->ctx.sc);
}

static void fx_close(fx_t *f) {
  index_free(&f->ctx);
  frag_free(&f->ctx);
  out_free(&f->ctx.out);
  bcgau_scratch_free(&f->ctx.sc);
  bcg_schema_close(f->ctx.sch);
  free(f->ctx.sch);
  bcg_fixture_remove(f->dir);
}

/* build_tva_main or index_build into stage of ctx.out, committed on success. fail_at >= 0: nth allocation fails, *hit set */
static int fx_stage(fx_t *f, int index, unsigned first_id, long fail_at, int *hit) {
  bcg_out_t st;
  int rc;
  ck_assert_int_eq(out_stage(&st, &f->ctx.out), 0);
  f->ctx.frag_gen++;
  if (fail_at >= 0) alloc_fail_at(fail_at);
  rc = index ? index_build(&f->ctx, &st, first_id) : build_tva_main(&f->ctx, &st);
  if (fail_at >= 0) *hit = alloc_fail_end();
  if (rc) out_discard(&st, &f->ctx.out);
  else out_commit(&f->ctx.out, &st);
  return rc;
}

START_TEST(segment_plain_size_limit_and_allocation) {
  bcg_version_t ver;
  seg_cache_t seg;
  memset(&ver, 0, sizeof ver);
  memset(&seg, 0, sizeof seg);
  ck_assert_int_eq(build_plain("x", DVBSTP_MAX_SEGMENT + 1, &ver, &seg), SEG_FAILED);
  for (long n = 0; n < 4; n++) {
    int rc;
    alloc_fail_at(n);
    rc = build_plain("<a/>", 4, &ver, &seg);
    alloc_fail_end();
    if (rc == SEG_OK) {
      free(seg.buf);
      seg.buf = NULL;
    }
  }
  ck_assert_int_eq(build_plain("<a/>", 4, &ver, &seg), SEG_OK);
  ck_assert_uint_eq(seg.len, 4);
  free(seg.buf);
  free(ver.last);
}
END_TEST

/* retried failed build: its errors not logged again */
START_TEST(segment_quiet_build_log) {
  bcg_version_t ver;
  seg_cache_t seg;
  char msg[1024];
  memset(&ver, 0, sizeof ver);
  memset(&seg, 0, sizeof seg);
  log_capture_begin();
  announce_log_quiet = 1;
  ck_assert_int_eq(build_plain("x", DVBSTP_MAX_SEGMENT + 1, &ver, &seg), SEG_FAILED);
  announce_log_quiet = 0;
  log_capture_end(msg, sizeof msg);
  ck_assert_ptr_null(strstr(msg, "GE100143"));
  log_capture_begin();
  ck_assert_int_eq(build_plain("x", DVBSTP_MAX_SEGMENT + 1, &ver, &seg), SEG_FAILED);
  log_capture_end(msg, sizeof msg);
  ck_assert_ptr_nonnull(strstr(msg, "GE100143"));
}
END_TEST

START_TEST(segment_build_container_failures) {
  for (int compress = 0; compress < 2; compress++)
    for (int nrefs = 0; nrefs < 3; nrefs++)
      for (long n = 0;; n++) {
        bitwriter_t bw;
        strrepo_writer_t sw;
        bcgau_ref_t refs[2];
        bcg_version_t ver;
        seg_cache_t seg;
        int hit;
        int rc;
        memset(refs, 0, sizeof refs);
        memset(&ver, 0, sizeof ver);
        memset(&seg, 0, sizeof seg);
        refs[0].frag_id = 1;
        refs[1].frag_id = 2;
        bitwriter_init(&bw);
        ck_assert_int_eq(strrepo_writer_init(&sw), 0);
        ck_assert_int_eq(bitwriter_put(&bw, 0xAB, 8), 0);
        ck_assert_int_eq(strrepo_writer_put(&sw, "s"), 0);
        alloc_fail_at(n);
        rc = build_segment(&bw, &sw, refs, nrefs, &ver, compress, &seg);
        hit = alloc_fail_end();
        if (rc == SEG_OK) free(seg.buf);
        free(ver.last);
        bitwriter_free(&bw);
        strrepo_writer_free(&sw);
        if (!hit) break;
      }
}
END_TEST

START_TEST(segment_build_rejects_oversized_guide) {
  bitwriter_t bw;
  strrepo_writer_t sw;
  bcg_version_t ver;
  seg_cache_t seg;
  memset(&ver, 0, sizeof ver);
  memset(&seg, 0, sizeof seg);
  bitwriter_init(&bw);
  ck_assert_int_eq(strrepo_writer_init(&sw), 0);
  for (size_t i = 0; i < DVBSTP_MAX_SEGMENT / 4 + 1000; i++) ck_assert_int_eq(bitwriter_put(&bw, (uint64_t)(i * 2654435761u), 32), 0);
  ck_assert_int_eq(build_segment(&bw, &sw, NULL, 0, &ver, 0, &seg), SEG_FAILED);
  free(ver.last);
  bitwriter_free(&bw);
  strrepo_writer_free(&sw);
}
END_TEST

START_TEST(segment_send_failure_is_reported) {
  mcast_t *m = mcast_open_send(AF_INET, "239.1.2.3", 5000, NULL, 1);
  unsigned char data[] = "data";
  unsigned char init[] = "init";
  seg_cache_t seg = {data, 4};
  bcg_schema_t s;
  int nul;
  memset(&s, 0, sizeof s);
  s.init = init;
  s.init_len = 4;
  ck_assert_ptr_nonnull(m);
  ck_assert_int_eq(send_segment(m, 0, &seg, 1, 1, 0), 0);
  ck_assert_int_eq(send_tva_main(m, 1, &seg, 1), 0);
  ck_assert_int_eq(send_index(m, &seg, 1, 2, 0), 0);
  ck_assert_int_eq(send_tva_init(m, &s, 0), 0);
  nul = open("/dev/null", O_RDONLY);
  ck_assert_int_ge(nul, 0);
  ck_assert_int_ge(dup2(nul, mcast_fd(m)), 0);
  close(nul);
  ck_assert_int_eq(send_segment(m, 0, &seg, 1, 1, 0), -1);
  ck_assert_int_eq(send_tva_main(m, 1, &seg, 1), -1);
  ck_assert_int_eq(send_index(m, &seg, 1, 2, 0), -1);
  ck_assert_int_eq(send_tva_init(m, &s, 0), -1);
  mcast_close(m);
}
END_TEST

START_TEST(segment_tva_main_in_both_encodings) {
  for (int enc = 0; enc < 2; enc++)
    for (int compress = 0; compress < 2; compress++) {
      fx_t f;
      fx_open(&f, enc ? ENC_PLAIN : ENC_BIM, compress);
      f.cfg.publisher = NULL;
      ck_assert_int_eq(fx_stage(&f, 0, 0, -1, NULL), 0);
      ck_assert_ptr_null(f.ctx.out.main_seg.buf);
      f.cfg.publisher = "example.com/Guide";
      f.cfg.lang = enc ? "de" : NULL;
      f.ctx.frag_next = 5; /* ids 1..5 taken by data fragments */
      ck_assert_int_eq(fx_stage(&f, 0, 0, -1, NULL), 0);
      ck_assert_ptr_nonnull(f.ctx.out.main_seg.buf);
      if (!enc && !compress) {
        /* TS 102 822-3-2 4.6.1.1: fragment_id unique in stream */
        bim_fragref_t *refs;
        unsigned fmt;
        int nrefs;
        ck_assert_uint_eq(f.ctx.out.main_seg.buf[0], 0);
        ck_assert_int_eq(bim_container_parse_refs(f.ctx.out.main_seg.buf + 1, f.ctx.out.main_seg.len - 1, &fmt, &refs, &nrefs), 0);
        ck_assert_int_eq(nrefs, 1);
        ck_assert_uint_eq(refs[0].frag_id, 6);
        ck_assert_uint_eq(refs[0].version, 1);
        free(refs);
      }
      {
        /* constant per schema: reused, fragment kept */
        size_t len = f.ctx.out.main_seg.len;
        unsigned char *prev = malloc(len);
        ck_assert_ptr_nonnull(prev);
        memcpy(prev, f.ctx.out.main_seg.buf, len);
        ck_assert_int_eq(fx_stage(&f, 0, 0, -1, NULL), 0);
        ck_assert_uint_eq(f.ctx.out.main_seg.len, len);
        ck_assert_mem_eq(f.ctx.out.main_seg.buf, prev, len);
        if (!enc) {
          frag_prune(&f.ctx);
          ck_assert_int_eq(f.ctx.nfrags, 1);
        }
        free(prev);
      }
      for (long n = 0;; n++) {
        int hit;
        (void)fx_stage(&f, 0, 0, n, &hit);
        if (!hit) break;
      }
      fx_close(&f);
    }
}
END_TEST

static void fill_doc(bcg_doc_t *d, int nprog, size_t title_len) {
  bcg_channel_t *c;
  bcg_doc_init(d);
  c = bcg_add_channel(d);
  bufcpy(c->id, sizeof c->id, "ch1");
  bufcpy(c->uri, sizeof c->uri, "rtp://239.1.1.1:5000");
  for (int i = 0; i < nprog; i++) {
    bcg_programme_t *p = bcg_add_programme(d);
    char *t = malloc(title_len + 1);
    char start[BCG_TIME_LEN];
    char tag[16];
    ck_assert_ptr_nonnull(t);
    memset(t, 'a' + i % 20, title_len);
    snprintf(tag, sizeof tag, "%06d", i);
    memcpy(t, tag, 6);
    t[title_len] = 0;
    snprintf(start, sizeof start, "2030-01-%02dT%02d:%02d:00Z", 1 + i / 1440 % 28, i / 60 % 24, i % 60);
    bufcpy(p->channel_id, sizeof p->channel_id, "ch1");
    bufcpy(p->start, sizeof p->start, start);
    ck_assert_int_eq(bcg_set_text(&p->title, t), 0);
    free(t);
  }
}

START_TEST(segment_index_collect_and_build) {
  fx_t f;
  bcg_doc_t d;
  bcg_doc_index_t ix;
  bcgau_ref_t refs[3];
  fx_open(&f, ENC_BIM, 1);
  fill_doc(&d, 6, 20);
  ck_assert_int_eq(bcg_doc_index_build(&ix, &d), 0);
  memset(refs, 0, sizeof refs);
  refs[0].kind = BCGAU_PROGRAM;
  refs[0].index = 0;
  refs[0].frag_id = 1;
  refs[1].kind = BCGAU_PROGRAM;
  refs[1].index = 1;
  refs[1].frag_id = 2;
  refs[2].kind = BCGAU_SCHEDULE;
  refs[2].index = 0;
  refs[2].frag_id = 3;
  f.cfg.index_mask = 0;
  ck_assert_int_eq(index_collect(&f.ctx, &d, &ix, refs, 3, 1), 0);
  ck_assert_int_eq(fx_stage(&f, 1, 3, -1, NULL), 0);
  f.cfg.index_mask = (1u << BIM_INDEX_KINDS) - 1;
  ck_assert_int_eq(index_collect(&f.ctx, &d, &ix, refs, 3, 1), 0);
  ck_assert_int_eq(fx_stage(&f, 1, 3, -1, NULL), 0);
  ck_assert_int_gt(f.ctx.out.niseg, 1);
  index_reset(&f.ctx);

  for (long n = 0;; n++) {
    int hit;
    int rc;
    index_reset(&f.ctx);
    alloc_fail_at(n);
    rc = index_collect(&f.ctx, &d, &ix, refs, 3, 1);
    hit = alloc_fail_end();
    if (!hit) {
      ck_assert_int_eq(rc, 0);
      break;
    }
  }
  for (long n = 0;; n++) {
    int hit;
    (void)fx_stage(&f, 1, 3, n, &hit);
    if (!hit) break;
  }
  ck_assert_int_eq(fx_stage(&f, 1, 0xFFFF, -1, NULL), -1);
  bcg_doc_index_free(&ix);
  bcg_doc_free(&d);
  fx_close(&f);
}
END_TEST

/* TS 102 323 8.5: stopless programme ends at next start, ScheduleEvent carries PublishedDuration */
START_TEST(segment_schedule_of_stopless_programme_has_duration) {
  fx_t f;
  bcg_doc_t d;
  bcg_doc_t view;
  bcg_doc_index_t ix;
  prog_time_t *times;
  int sel[2];
  int n;
  dvbtva_fuu_t fuu;
  bim_err_t err;
  const bim_node_t *ev;
  const bim_node_t *dur;
  fx_open(&f, ENC_BIM, 0);
  fill_doc(&d, 2, 8);
  bufcpy(d.programmes[1].start, sizeof d.programmes[1].start, "2030-01-01T00:30:00Z");
  ck_assert_int_eq(prog_times_build(&d, &times), 0);
  window_select(&d, times, date_to_mjd(2030, 1, 1) * 1440L, 120, sel, &n, NULL, NULL, NULL);
  ck_assert_int_eq(n, 1);
  ck_assert_int_eq(window_view(&d, times, sel, n, &view), 0);
  ck_assert_int_eq(bcg_doc_index_build(&ix, &view), 0);
  fuu.code = 0x0005;
  fuu.root = bcgfrag_schedule(&f.ctx.sch->au, "ch1", view.programmes, ix.first[0], ix.next, 0);
  ck_assert_ptr_nonnull(fuu.root);
  ev = bim_node_child(fuu.root, "ScheduleEvent");
  ck_assert_ptr_nonnull(ev);
  dur = bim_node_child(ev, "PublishedDuration");
  ck_assert_ptr_nonnull(dur);
  ck_assert_str_eq(dur->text, "PT0H30M0S");
  ck_assert_str_eq(bim_node_attr(fuu.root, "end"), "2030-01-01T00:30:00Z");
  ck_assert_int_eq(dvbtva_check_fragment(&f.ctx.sch->au.ns, &fuu, &err), 0);
  bim_node_free(fuu.root);
  bcg_doc_index_free(&ix);
  window_view_free(&view);
  free(times);
  bcg_doc_free(&d);
  fx_close(&f);
}
END_TEST

/* init version by content: same content keeps it, change bumps, discarded build leaves committed one */
START_TEST(segment_staged_versions_commit_and_discard) {
  static const unsigned char a[] = "init-a";
  static const unsigned char b[] = "init-b";
  static const unsigned char c[] = "init-c";
  bcg_out_t out;
  bcg_out_t st;
  memset(&out, 0, sizeof out);
  ck_assert_int_eq(out_stage(&st, &out), 0);
  version_stage(&st.init_ver, a, sizeof a);
  out_commit(&out, &st);
  ck_assert_uint_eq(out.init_ver.version, 1);
  ck_assert_int_eq(out_stage(&st, &out), 0);
  version_stage(&st.init_ver, a, sizeof a);
  out_commit(&out, &st);
  ck_assert_uint_eq(out.init_ver.version, 1);
  ck_assert_int_eq(out_stage(&st, &out), 0);
  version_stage(&st.init_ver, b, sizeof b);
  out_commit(&out, &st);
  ck_assert_uint_eq(out.init_ver.version, 2);
  ck_assert_int_eq(out_stage(&st, &out), 0);
  version_stage(&st.init_ver, c, sizeof c);
  ck_assert_uint_eq(st.init_ver.version, 3);
  out_discard(&st, &out);
  ck_assert_uint_eq(out.init_ver.version, 2);
  ck_assert_mem_eq(out.init_ver.last, b, sizeof b);
  out_free(&out);
}
END_TEST

static void add_chan(bcg_doc_t *d, const char *id, const char *uri) {
  bcg_channel_t *c = bcg_add_channel(d);
  bufcpy(c->id, sizeof c->id, id);
  bufcpy(c->uri, sizeof c->uri, uri);
}

static void add_prog(bcg_doc_t *d, const char *ch, const char *start, const char *stop, const char *title) {
  bcg_programme_t *p = bcg_add_programme(d);
  bufcpy(p->channel_id, sizeof p->channel_id, ch);
  bufcpy(p->start, sizeof p->start, start);
  bufcpy(p->stop, sizeof p->stop, stop);
  ck_assert_int_eq(bcg_set_text(&p->title, title), 0);
}

static int build_at(fx_t *f, long minute) {
  bcg_src_t *s = &f->ctx.src;
  window_select(&s->doc, s->times, date_to_mjd(2030, 1, 1) * 1440L + minute, 600, s->sel, &s->nsel, NULL, NULL, NULL);
  return announce_build(&f->ctx, 0, 0, 0);
}

/* unchanged channel container reused with its version, index as from a full build */
START_TEST(segment_build_reuses_unchanged_channels) {
  fx_t f;
  bcg_src_t *s;
  unsigned char *seg1;
  size_t seg1_len;
  unsigned v0;
  unsigned v1;
  unsigned char *iseg[8];
  size_t ilen[8];
  int niseg;
  fx_open(&f, ENC_BIM, 0);
  f.cfg.index_mask = (1u << BIM_INDEX_PI_CRID) | (1u << BIM_INDEX_SCHED_TIME);
  s = &f.ctx.src;
  bcg_doc_init(&s->doc);
  add_chan(&s->doc, "ch1", "rtp://239.1.1.1:5000");
  add_chan(&s->doc, "ch2", "rtp://239.1.1.2:5000");
  add_prog(&s->doc, "ch1", "2030-01-01T00:00:00Z", "2030-01-01T01:00:00Z", "A");
  add_prog(&s->doc, "ch1", "2030-01-01T01:00:00Z", "2030-01-01T02:00:00Z", "B");
  add_prog(&s->doc, "ch2", "2030-01-01T00:00:00Z", "2030-01-01T03:00:00Z", "C");
  ck_assert_int_eq(prog_times_build(&s->doc, &s->times), 0);
  s->sel = calloc(3, sizeof *s->sel);
  s->sel_new = calloc(3, sizeof *s->sel_new);
  ck_assert_ptr_nonnull(s->sel);
  ck_assert_ptr_nonnull(s->sel_new);

  ck_assert_int_eq(build_at(&f, 30), 0);
  ck_assert_int_eq(f.ctx.out.nseg, 2);
  ck_assert_int_eq(f.ctx.out.nreused, 0);
  seg1_len = f.ctx.out.seg[1].len;
  seg1 = malloc(seg1_len);
  ck_assert_ptr_nonnull(seg1);
  memcpy(seg1, f.ctx.out.seg[1].buf, seg1_len);
  v0 = f.ctx.out.vers[0].version;
  v1 = f.ctx.out.vers[1].version;

  ck_assert_int_eq(build_at(&f, 90), 0);
  ck_assert_int_eq(f.ctx.out.nreused, 1);
  ck_assert_uint_eq(f.ctx.out.seg[1].len, seg1_len);
  ck_assert_mem_eq(f.ctx.out.seg[1].buf, seg1, seg1_len);
  ck_assert_uint_eq(f.ctx.out.vers[1].version, v1);
  ck_assert_uint_ne(f.ctx.out.vers[0].version, v0);
  niseg = f.ctx.out.niseg;
  ck_assert_int_gt(niseg, 1);
  ck_assert_int_le(niseg, 8);
  for (int i = 0; i < niseg; i++) {
    ilen[i] = f.ctx.out.iseg[i].len;
    iseg[i] = malloc(ilen[i]);
    ck_assert_ptr_nonnull(iseg[i]);
    memcpy(iseg[i], f.ctx.out.iseg[i].buf, ilen[i]);
  }

  /* full rebuild of same selection: same bytes */
  f.ctx.src_gen++;
  ck_assert_int_eq(build_at(&f, 90), 0);
  ck_assert_int_eq(f.ctx.out.nreused, 0);
  ck_assert_mem_eq(f.ctx.out.seg[1].buf, seg1, seg1_len);
  ck_assert_int_eq(f.ctx.out.niseg, niseg);
  for (int i = 0; i < niseg; i++) {
    ck_assert_uint_eq(f.ctx.out.iseg[i].len, ilen[i]);
    ck_assert_mem_eq(f.ctx.out.iseg[i].buf, iseg[i], ilen[i]);
    free(iseg[i]);
  }
  free(seg1);
  bcg_doc_free(&s->doc);
  free(s->times);
  free(s->sel);
  free(s->sel_new);
  fx_close(&f);
}
END_TEST

START_TEST(segment_index_build_limits) {
  fx_t f;
  bcg_doc_t d;
  bcg_doc_index_t ix;
  bcgau_ref_t *refs;
  int n = 1200;
  fx_open(&f, ENC_BIM, 0);
  fill_doc(&d, n, 3000);
  ck_assert_int_eq(bcg_doc_index_build(&ix, &d), 0);
  refs = calloc((size_t)n, sizeof *refs);
  ck_assert_ptr_nonnull(refs);
  for (int i = 0; i < n; i++) {
    refs[i].kind = BCGAU_PROGRAM;
    refs[i].index = i;
    refs[i].frag_id = (uint32_t)i + 1;
  }
  f.cfg.index_mask = 1u << BIM_INDEX_PI_TITLE;
  ck_assert_int_eq(index_collect(&f.ctx, &d, &ix, refs, n, 1), 0);
  ck_assert_int_eq(fx_stage(&f, 1, 3, -1, NULL), -1);
  ck_assert_int_eq(fx_stage(&f, 1, 0xFFFF, -1, NULL), -1);
  free(refs);
  bcg_doc_index_free(&ix);
  bcg_doc_free(&d);
  fx_close(&f);
}
END_TEST

START_TEST(segment_plan_groups) {
  fx_t f;
  bcg_doc_t d;
  bcg_doc_index_t ix;
  int *bounds = NULL;
  int ng = 0;
  fx_open(&f, ENC_BIM, 0);
  fill_doc(&d, 30, 40);
  ck_assert_int_eq(bcg_doc_index_build(&ix, &d), 0);
  ck_assert_int_eq(plan_groups(&f.ctx.sc, &f.ctx.sch->au, &d, &ix, 0, &bounds, &ng), 0);
  ck_assert_int_ge(ng, 1);
  free(bounds);
  for (long n = 0;; n++) {
    int hit;
    int rc;
    bounds = NULL;
    alloc_fail_at(n);
    rc = plan_groups(&f.ctx.sc, &f.ctx.sch->au, &d, &ix, 0, &bounds, &ng);
    hit = alloc_fail_end();
    if (!rc) free(bounds);
    if (!hit) break;
  }
  bcg_doc_index_free(&ix);
  bcg_doc_free(&d);
  /* container per channel, oversize found when built */
  fill_doc(&d, 700, 120);
  ck_assert_int_eq(bcg_doc_index_build(&ix, &d), 0);
  ck_assert_int_eq(plan_groups(&f.ctx.sc, &f.ctx.sch->au, &d, &ix, 0, &bounds, &ng), 0);
  ck_assert_int_eq(ng, 1);
  ck_assert_int_eq(bounds[0], 0);
  ck_assert_int_eq(bounds[1], 1);
  free(bounds);
  bcg_doc_index_free(&ix);
  bcg_doc_free(&d);
  fx_close(&f);
}
END_TEST

static Suite *segment_suite(void) {
  Suite *s = suite_create("dipibcg_announce_segment");
  TCase *tc = tcase_create("core");
  tcase_set_timeout(tc, 120);
  tcase_add_test(tc, segment_plain_size_limit_and_allocation);
  tcase_add_test(tc, segment_quiet_build_log);
  tcase_add_test(tc, segment_build_container_failures);
  tcase_add_test(tc, segment_build_rejects_oversized_guide);
  tcase_add_test(tc, segment_send_failure_is_reported);
  tcase_add_test(tc, segment_tva_main_in_both_encodings);
  tcase_add_test(tc, segment_index_collect_and_build);
  tcase_add_test(tc, segment_index_build_limits);
  tcase_add_test(tc, segment_build_reuses_unchanged_channels);
  tcase_add_test(tc, segment_staged_versions_commit_and_discard);
  tcase_add_test(tc, segment_schedule_of_stopless_programme_has_duration);
  tcase_add_test(tc, segment_plan_groups);
  suite_add_tcase(s, tc);
  return s;
}

int main(void) {
  SRunner *sr = srunner_create(segment_suite());
  int nf;
  srunner_run_all(sr, CK_NORMAL);
  nf = srunner_ntests_failed(sr);
  srunner_free(sr);
  return nf == 0 ? EXIT_SUCCESS : EXIT_FAILURE;
}
