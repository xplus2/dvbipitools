/* Copyright 2026 dvbipitools authors. Licensed under GPL-3.0-or-later.
 * See NOTICE and LICENSE for details and authorship information. */

#include <limits.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "lib/sys/ioutil.h"

#include "accessunit.h"
#include "fragment.h"

static int emit_fuu(bitwriter_t *outer, int ctxpath, bitwriter_t *fbw) {
  size_t flen;
  const unsigned char *fbytes;
  if (bitwriter_flush(fbw)) return -1;
  fbytes = bitwriter_data(fbw, &flen);
  if (bitwriter_put_vluimsbf8(outer, (uint64_t)flen)) return -1;
  if (bitwriter_put(outer, (uint64_t)ctxpath, 16)) return -1;
  return bitwriter_put_bytes(outer, fbytes, flen);
}

typedef struct {
  const char *id;
  int idx;
} channel_idx_t;

static int channel_idx_cmp(const void *a, const void *b) {
  return strcmp(((const channel_idx_t *)a)->id, ((const channel_idx_t *)b)->id);
}

/* -1 if not found */
static int channel_idx_find(const channel_idx_t *idx, int n, const char *id) {
  int lo, hi;
  if (n <= 0) return -1;
  lo = 0;
  hi = n - 1;
  while (lo <= hi) {
    int mid = (lo + hi) / 2;
    int c = strcmp(id, idx[mid].id);
    if (c == 0) return idx[mid].idx;
    if (c < 0) hi = mid - 1;
    else lo = mid + 1;
  }
  return -1;
}

static int record_ref(accessunit_scratch_t *sc, int n, bitwriter_t *bw, const strrepo_writer_t *sw) {
  if (bitwriter_flush(bw)) return -1;
  sc->refs[n].au_off = (uint32_t)bw->len;
  sc->refs[n].str_off = (uint32_t)sw->len;
  return 0;
}

void accessunit_index_free(accessunit_index_t *ix) {
  free(ix->nprog);
  free(ix->first);
  free(ix->next);
  memset(ix, 0, sizeof *ix);
}

int accessunit_index_build(accessunit_index_t *ix, const bcg_doc_t *doc) {
  int i;
  int *last = NULL;
  channel_idx_t *cidx = NULL;
  int rc = -1;

  memset(ix, 0, sizeof *ix);
  if (doc->channel_count) {
    cidx = malloc(sizeof *cidx * (size_t)doc->channel_count);
    last = malloc(sizeof *last * (size_t)doc->channel_count);
    ix->nprog = calloc((size_t)doc->channel_count, sizeof *ix->nprog);
    ix->first = malloc(sizeof *ix->first * (size_t)doc->channel_count);
    if (!cidx || !last || !ix->nprog || !ix->first) goto done;
    for (i = 0; i < doc->channel_count; i++) {
      cidx[i].id = doc->channels[i].id;
      cidx[i].idx = i;
      ix->first[i] = -1;
      last[i] = -1;
    }
    qsort(cidx, (size_t)doc->channel_count, sizeof *cidx, channel_idx_cmp);
  }
  if (doc->programme_count) {
    ix->next = malloc(sizeof *ix->next * (size_t)doc->programme_count);
    if (!ix->next) goto done;
    for (i = 0; i < doc->programme_count; i++) {
      int ch = channel_idx_find(cidx, doc->channel_count, doc->programmes[i].channel_id);
      ix->next[i] = -1;
      if (ch < 0) continue;
      ix->nprog[ch]++;
      if (last[ch] < 0) ix->first[ch] = i;
      else ix->next[last[ch]] = i;
      last[ch] = i;
    }
  }
  rc = 0;

done:
  free(cidx);
  free(last);
  if (rc) accessunit_index_free(ix);
  return rc;
}

int accessunit_encode_range(accessunit_scratch_t *sc, const bcg_doc_t *doc, const accessunit_index_t *ix, int ch_from, int ch_to, bitwriter_t *bw, strrepo_writer_t *sw, int *out_nfuu, int *out_nchan_prog) {
  int i;
  int p;
  int nfuu = 0;
  int nchan_prog = 0;
  int nref = 0;

  if (ch_from < 0) ch_from = 0;
  if (ch_to > doc->channel_count) ch_to = doc->channel_count;
  for (i = ch_from; i < ch_to; i++) {
    nchan_prog += ix->nprog[i] > 0;
    if (!doc->channels[i].uri[0]) continue;
    nfuu += ix->nprog[i] + (ix->nprog[i] > 0) + 1;
  }
  if (nfuu > sc->refs_cap) {
    void *np = array_grow(sc->refs, &sc->refs_cap, nfuu, sizeof *sc->refs);
    if (!np) return -1;
    sc->refs = np;
  }

  if (bitwriter_put_vluimsbf8(bw, (uint64_t)nfuu)) return -1;
  for (i = ch_from; i < ch_to; i++) {
    if (!doc->channels[i].uri[0]) continue;
    for (p = ix->first[i]; p >= 0; p = ix->next[p]) {
      bitwriter_reset(&sc->fbw);
      if (record_ref(sc, nref++, bw, sw)) return -1;
      if (fragment_encode_program_information(&doc->programmes[p], &sc->fbw, sw) || emit_fuu(bw, DVBCTXPATH_PROGRAM_INFORMATION, &sc->fbw)) return -1;
    }
  }

  for (i = ch_from; i < ch_to; i++) {
    const bcg_channel_t *c = &doc->channels[i];
    if (!c->uri[0] || !ix->nprog[i]) continue;
    bitwriter_reset(&sc->fbw);
    if (record_ref(sc, nref++, bw, sw)) return -1;
    if (fragment_encode_schedule(c->id, doc->programmes, ix->first[i], ix->next, &sc->fbw, sw) || emit_fuu(bw, DVBCTXPATH_SCHEDULE, &sc->fbw)) return -1;
  }

  for (i = ch_from; i < ch_to; i++) {
    const bcg_channel_t *c = &doc->channels[i];
    if (!c->uri[0]) continue;
    bitwriter_reset(&sc->fbw);
    if (record_ref(sc, nref++, bw, sw)) return -1;
    if (fragment_encode_service_information(c, &sc->fbw, sw) || emit_fuu(bw, DVBCTXPATH_SERVICE_INFORMATION, &sc->fbw)) return -1;
  }

  if (bitwriter_flush(bw)) return -1;
  *out_nfuu = nfuu;
  if (out_nchan_prog) *out_nchan_prog = nchan_prog;
  return 0;
}

int accessunit_encode(accessunit_scratch_t *sc, const bcg_doc_t *doc, bitwriter_t *bw, strrepo_writer_t *sw, int *out_nfuu) {
  accessunit_index_t ix;
  int rc;
  if (accessunit_index_build(&ix, doc)) return -1;
  rc = accessunit_encode_range(sc, doc, &ix, 0, doc->channel_count, bw, sw, out_nfuu, NULL);
  accessunit_index_free(&ix);
  return rc;
}

typedef struct {
  bcg_progtext_t *arr;
  int n;
  channel_idx_t *idx;
} ptext_ctx_t;

static int ptext_lookup(void *vctx, const char *crid, bcg_programme_t *pr) {
  ptext_ctx_t *ctx = (ptext_ctx_t *)vctx;
  int i = -1;
  if (ctx->idx) {
    i = channel_idx_find(ctx->idx, ctx->n, crid);
  } else {
    for (int k = 0; k < ctx->n; k++) {
      if (!strcmp(ctx->arr[k].crid, crid)) {
        i = k;
        break;
      }
    }
  }
  if (i < 0) return -1;
  if (bcg_set_text(&pr->title, ctx->arr[i].title) || bcg_set_text(&pr->desc, ctx->arr[i].desc) || bcg_set_text(&pr->category, ctx->arr[i].category))
    return -2;
  return 0;
}

typedef struct {
  int context_path;
  size_t start; /* FUULength position */
  size_t offset;
  size_t length;
} fuu_index_t;

void accessunit_scratch_init(accessunit_scratch_t *sc) { memset(sc, 0, sizeof *sc); }

void accessunit_scratch_free(accessunit_scratch_t *sc) {
  free(sc->fuus);
  free(sc->ptext);
  free(sc->refs);
  bitwriter_free(&sc->fbw);
  memset(sc, 0, sizeof *sc);
}

static int decode_program_info_fuu(accessunit_scratch_t *sc, bcg_progtext_t **ptext, int *ptext_n, bitreader_t *fbr, strrepo_reader_t *sr) {
  bcg_programme_t tmp;
  bcg_progtext_t *pt;
  char crid[BCG_ID_LEN * 3 + 64];
  if (*ptext_n >= sc->ptext_cap) {
    void *np = array_grow(sc->ptext, &sc->ptext_cap, *ptext_n + 1, sizeof **ptext);
    if (!np) return -1;
    sc->ptext = np;
    *ptext = sc->ptext;
  }
  if (fragment_decode_program_information(fbr, sr, crid, sizeof crid, &tmp)) return -1;
  pt = &(*ptext)[*ptext_n];
  pt->crid = strdup(crid);
  if (!pt->crid) {
    bcg_programme_free_text(&tmp);
    return -1;
  }
  pt->title = tmp.title;
  pt->desc = tmp.desc;
  pt->category = tmp.category;
  (*ptext_n)++;
  return 0;
}

/* -1: context_path not phase-ordered (no ordering check applies to it) */
static int resolve_fuu_phase(int context_path) {
  switch (context_path) {
    case DVBCTXPATH_PROGRAM_INFORMATION: return 0;
    case DVBCTXPATH_SCHEDULE:            return 1;
    case DVBCTXPATH_SERVICE_INFORMATION: return 2;
    default:                             return -1;
  }
}

/* NULL on alloc failure: lookup falls back to linear scan */
static channel_idx_t *build_ptext_index(const bcg_progtext_t *arr, int n) {
  channel_idx_t *idx = malloc(sizeof *idx * (size_t)n);
  if (!idx) return NULL;
  for (int k = 0; k < n; k++) {
    idx[k].id = arr[k].crid;
    idx[k].idx = k;
  }
  qsort(idx, (size_t)n, sizeof *idx, channel_idx_cmp);
  return idx;
}

/* decodes one fuu's payload by context_path. 0 ok, -1 error */
static int decode_one_fuu(accessunit_scratch_t *sc, bcg_doc_t *doc, strrepo_reader_t *sr, const unsigned char *base, const fuu_index_t *fu, bcg_progtext_t **ptext, int *ptext_n, channel_idx_t **pidx) {
  bitreader_t fbr;
  switch (fu->context_path) {
    case DVBCTXPATH_PROGRAM_INFORMATION:
      bitreader_init(&fbr, base + fu->offset, fu->length);
      return decode_program_info_fuu(sc, ptext, ptext_n, &fbr, sr);
    case DVBCTXPATH_SCHEDULE: {
      ptext_ctx_t ctx;
      if (!*pidx && *ptext_n > 0) *pidx = build_ptext_index(*ptext, *ptext_n);
      ctx.arr = *ptext;
      ctx.n = *ptext_n;
      ctx.idx = *pidx;
      bitreader_init(&fbr, base + fu->offset, fu->length);
      return fragment_decode_schedule(&fbr, sr, doc, ptext_lookup, &ctx);
    }
    case DVBCTXPATH_SERVICE_INFORMATION: {
      bcg_channel_t *c = bcg_add_channel(doc);
      if (!c) return -1;
      bitreader_init(&fbr, base + fu->offset, fu->length);
      return fragment_decode_service_information(&fbr, sr, c);
    }
    default:
      return 0;
  }
}

int accessunit_decode(accessunit_scratch_t *sc, bitreader_t *br, strrepo_reader_t *sr, bcg_doc_t *doc, int *out_nfuu) {
  return accessunit_decode_refs(sc, br, sr, NULL, 0, doc, out_nfuu);
}

/* position sr at fuu's string buffer if refs name it, hint = likely refs index. -1 on bad offset, or unnamed fuu if required */
static int sync_strings(strrepo_reader_t *sr, const accessunit_ref_t *refs, int nrefs, int hint, size_t start, int required) {
  if (hint < nrefs && refs[hint].au_off == start) return strrepo_reader_seek(sr, refs[hint].str_off);
  for (int k = 0; k < nrefs; k++) {
    if (refs[k].au_off == start) return strrepo_reader_seek(sr, refs[k].str_off);
  }
  return required ? -1 : 0;
}

int accessunit_decode_refs(accessunit_scratch_t *sc, bitreader_t *br, strrepo_reader_t *sr, const accessunit_ref_t *refs, int nrefs, bcg_doc_t *doc, int *out_nfuu) {
  const unsigned char *base = br->buf;
  bcg_progtext_t *ptext;
  int ptext_n = 0;
  channel_idx_t *pidx = NULL; /* built at first Schedule, PI phase is over then */
  fuu_index_t *fuus;
  int nfuu;
  int i;
  int rc = 0;
  uint64_t n64;
  size_t bytes_left;
  int phase;
  int reordered = 0;
  int pass;

  if (bitreader_get_vluimsbf8(br, &n64)) return -1;
  bytes_left = bitreader_bits_left(br) / 8;
  if (n64 > (uint64_t)INT_MAX || n64 > (uint64_t)(bytes_left / 3) || n64 > (uint64_t)(SIZE_MAX / sizeof *fuus)) return -1;
  nfuu = (int)n64;
  if (nfuu > sc->fuus_cap) {
    void *np = array_grow(sc->fuus, &sc->fuus_cap, nfuu, sizeof *fuus);
    if (!np) return -1;
    sc->fuus = np;
  }
  fuus = sc->fuus;
  ptext = sc->ptext;

  /* without refs sr is a sequential cursor: fuu order must match encode's group order (program_info, schedule, service_info). refs allow any order */
  phase = 0;
  for (i = 0; i < nfuu; i++) {
    uint64_t flen, ctxpath;
    int fuu_phase;
    fuus[i].start = br->byte_pos;
    if (bitreader_get_vluimsbf8(br, &flen) || bitreader_get(br, 16, &ctxpath) || br->bit_pos != 0 || flen > bitreader_bits_left(br) / 8) {
      rc = -1;
      goto done;
    }
    fuus[i].context_path = (int)ctxpath;
    fuu_phase = resolve_fuu_phase(fuus[i].context_path);
    if (fuu_phase >= 0) {
      if (fuu_phase < phase) {
        if (nrefs <= 0) {
          rc = -1;
          goto done;
        }
        reordered = 1;
      } else {
        phase = fuu_phase;
      }
    }
    fuus[i].offset = br->byte_pos;
    fuus[i].length = (size_t)flen;
    br->byte_pos += (size_t)flen;
  }
  /* phase order: schedules need all program texts */
  for (pass = 0; pass < 3; pass++) {
    for (i = 0; i < nfuu; i++) {
      if (resolve_fuu_phase(fuus[i].context_path) != pass) continue;
      if (sync_strings(sr, refs, nrefs, i, fuus[i].start, reordered)) {
        rc = -1;
        goto done;
      }
      if (decode_one_fuu(sc, doc, sr, base, &fuus[i], &ptext, &ptext_n, &pidx)) {
        rc = -1;
        goto done;
      }
    }
  }

  *out_nfuu = nfuu;

done:
  free(pidx);
  for (i = 0; i < ptext_n; i++) bcg_progtext_free(&ptext[i]);
  return rc;
}
