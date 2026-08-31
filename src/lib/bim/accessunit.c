/* Copyright 2026 dvbipitools authors. Licensed under GPL-3.0-or-later.
 * See NOTICE and LICENSE for details and authorship information. */

#include <limits.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "lib/helper/ioutil.h"

#include "accessunit.h"
#include "fragment.h"

static int emit_fuu(bitwriter_t *outer, int ctxpath, bitwriter_t *fbw) {
  size_t flen;
  const unsigned char *fbytes = bitwriter_data(fbw, &flen);
  if (bitwriter_put_vluimsbf8(outer, (uint64_t)flen))
    return -1;
  if (bitwriter_put(outer, (uint64_t)ctxpath, 16))
    return -1;
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
  if (n <= 0)
    return -1;
  lo = 0;
  hi = n - 1;
  while (lo <= hi) {
    int mid = (lo + hi) / 2;
    int c = strcmp(id, idx[mid].id);
    if (c == 0)
      return idx[mid].idx;
    if (c < 0)
      hi = mid - 1;
    else
      lo = mid + 1;
  }
  return -1;
}

int accessunit_encode(accessunit_scratch_t *sc, const bcg_doc_t *doc, bitwriter_t *bw, strrepo_writer_t *sw, int *out_nfuu) {
  int i, nfuu = 0, rc = -1;
  channel_idx_t *cidx = NULL;
  int *prog_channel = NULL;      /* channel index per programme, -1 if unmatched */
  int *channel_has_prog = NULL;  /* 1 per channel with at least one programme */

  if (doc->channel_count) {
    cidx = malloc(sizeof *cidx * (size_t)doc->channel_count);
    channel_has_prog = calloc((size_t)doc->channel_count, sizeof *channel_has_prog);
    if (!cidx || !channel_has_prog)
      goto done;
    for (i = 0; i < doc->channel_count; i++) {
      cidx[i].id = doc->channels[i].id;
      cidx[i].idx = i;
    }
    qsort(cidx, (size_t)doc->channel_count, sizeof *cidx, channel_idx_cmp);
  }
  if (doc->programme_count) {
    prog_channel = malloc(sizeof *prog_channel * (size_t)doc->programme_count);
    if (!prog_channel)
      goto done;
    for (i = 0; i < doc->programme_count; i++) {
      prog_channel[i] = channel_idx_find(cidx, doc->channel_count, doc->programmes[i].channel_id);
      if (prog_channel[i] >= 0)
        channel_has_prog[prog_channel[i]] = 1;
    }
  }

  for (i = 0; i < doc->programme_count; i++)
    if (prog_channel[i] >= 0 && doc->channels[prog_channel[i]].uri[0])
      nfuu++;
  for (i = 0; i < doc->channel_count; i++)
    if (doc->channels[i].uri[0] && channel_has_prog[i])
      nfuu++;
  for (i = 0; i < doc->channel_count; i++)
    if (doc->channels[i].uri[0])
      nfuu++;

  if (bitwriter_put_vluimsbf8(bw, (uint64_t)nfuu))
    goto done;

  for (i = 0; i < doc->programme_count; i++) {
    const bcg_programme_t *pr = &doc->programmes[i];
    if (prog_channel[i] < 0 || !doc->channels[prog_channel[i]].uri[0])
      continue;
    bitwriter_reset(&sc->fbw);
    if (fragment_encode_program_information(pr, &sc->fbw, sw) || emit_fuu(bw, DVBCTXPATH_PROGRAM_INFORMATION, &sc->fbw))
      goto done;
  }

  for (i = 0; i < doc->channel_count; i++) {
    const bcg_channel_t *c = &doc->channels[i];
    if (!c->uri[0] || !channel_has_prog[i])
      continue;
    bitwriter_reset(&sc->fbw);
    if (fragment_encode_schedule(c->id, doc->programmes, doc->programme_count, &sc->fbw, sw) || emit_fuu(bw, DVBCTXPATH_SCHEDULE, &sc->fbw))
      goto done;
  }

  for (i = 0; i < doc->channel_count; i++) {
    const bcg_channel_t *c = &doc->channels[i];
    if (!c->uri[0])
      continue;
    bitwriter_reset(&sc->fbw);
    if (fragment_encode_service_information(c, &sc->fbw, sw) || emit_fuu(bw, DVBCTXPATH_SERVICE_INFORMATION, &sc->fbw))
      goto done;
  }

  *out_nfuu = nfuu;
  rc = 0;

done:
  free(cidx);
  free(prog_channel);
  free(channel_has_prog);
  return rc;
}

typedef struct {
  bcg_progtext_t *arr;
  int n;
} ptext_ctx_t;

static int ptext_lookup(void *vctx, const char *crid, bcg_programme_t *pr) {
  ptext_ctx_t *ctx = (ptext_ctx_t *)vctx;
  for (int i = 0; i < ctx->n; i++)
    if (!strcmp(ctx->arr[i].crid, crid)) {
      bufcpy(pr->title, sizeof pr->title, ctx->arr[i].title);
      bufcpy(pr->desc, sizeof pr->desc, ctx->arr[i].desc);
      bufcpy(pr->category, sizeof pr->category, ctx->arr[i].category);
      return 0;
    }
  return -1;
}

typedef struct {
  int context_path;
  size_t offset;
  size_t length;
} fuu_index_t;

void accessunit_scratch_init(accessunit_scratch_t *sc) { memset(sc, 0, sizeof *sc); }

void accessunit_scratch_free(accessunit_scratch_t *sc) {
  free(sc->fuus);
  free(sc->ptext);
  bitwriter_free(&sc->fbw);
  memset(sc, 0, sizeof *sc);
}

static int decode_program_info_fuu(accessunit_scratch_t *sc, bcg_progtext_t **ptext, int *ptext_n, bitreader_t *fbr, strrepo_reader_t *sr) {
  bcg_programme_t tmp;
  if (*ptext_n >= sc->ptext_cap) {
    void *np = array_grow(sc->ptext, &sc->ptext_cap, *ptext_n + 1, sizeof **ptext);
    if (!np)
      return -1;
    sc->ptext = np;
    *ptext = sc->ptext;
  }
  if (fragment_decode_program_information(fbr, sr, (*ptext)[*ptext_n].crid, sizeof (*ptext)[*ptext_n].crid, &tmp))
    return -1;
  bufcpy((*ptext)[*ptext_n].title, sizeof (*ptext)[*ptext_n].title, tmp.title);
  bufcpy((*ptext)[*ptext_n].desc, sizeof (*ptext)[*ptext_n].desc, tmp.desc);
  bufcpy((*ptext)[*ptext_n].category, sizeof (*ptext)[*ptext_n].category, tmp.category);
  (*ptext_n)++;
  return 0;
}

/* -1: context_path not phase-ordered (no ordering check applies to it) */
static int resolve_fuu_phase(int context_path) {
  switch (context_path) {
    case DVBCTXPATH_PROGRAM_INFORMATION:
      return 0;
    case DVBCTXPATH_SCHEDULE:
      return 1;
    case DVBCTXPATH_SERVICE_INFORMATION:
      return 2;
    default:
      return -1;
  }
}

/* decodes one fuu's payload by context_path. 0 ok, -1 error */
static int decode_one_fuu(accessunit_scratch_t *sc, bcg_doc_t *doc, strrepo_reader_t *sr, const unsigned char *base,
                           const fuu_index_t *fu, bcg_progtext_t **ptext, int *ptext_n) {
  bitreader_t fbr;
  switch (fu->context_path) {
    case DVBCTXPATH_PROGRAM_INFORMATION:
      bitreader_init(&fbr, base + fu->offset, fu->length);
      return decode_program_info_fuu(sc, ptext, ptext_n, &fbr, sr);
    case DVBCTXPATH_SCHEDULE: {
      ptext_ctx_t ctx;
      ctx.arr = *ptext;
      ctx.n = *ptext_n;
      bitreader_init(&fbr, base + fu->offset, fu->length);
      return fragment_decode_schedule(&fbr, sr, doc, ptext_lookup, &ctx);
    }
    case DVBCTXPATH_SERVICE_INFORMATION: {
      bcg_channel_t *c = bcg_add_channel(doc);
      if (!c)
        return -1;
      bitreader_init(&fbr, base + fu->offset, fu->length);
      return fragment_decode_service_information(&fbr, sr, c);
    }
    default:
      return 0;
  }
}

int accessunit_decode(accessunit_scratch_t *sc, bitreader_t *br, strrepo_reader_t *sr, bcg_doc_t *doc, int *out_nfuu) {
  const unsigned char *base = br->buf;
  bcg_progtext_t *ptext;
  int ptext_n = 0;
  fuu_index_t *fuus;
  int nfuu, i, rc = 0;
  uint64_t n64;
  size_t bytes_left;

  if (bitreader_get_vluimsbf8(br, &n64))
    return -1;
  bytes_left = bitreader_bits_left(br) / 8;
  if (n64 > (uint64_t)INT_MAX || n64 > (uint64_t)(bytes_left / 3) || n64 > (uint64_t)(SIZE_MAX / sizeof *fuus))
    return -1;
  nfuu = (int)n64;
  if (nfuu > sc->fuus_cap) {
    void *np = array_grow(sc->fuus, &sc->fuus_cap, nfuu, sizeof *fuus);
    if (!np)
      return -1;
    sc->fuus = np;
  }
  fuus = sc->fuus;
  ptext = sc->ptext;

  {
    /* sr sequential cursor: fuu order must match encode's group order
       (program_info, schedule, service_info) or frags read wrong strings */
    int phase = 0;
    for (i = 0; i < nfuu; i++) {
      uint64_t flen, ctxpath;
      int fuu_phase;
      if (bitreader_get_vluimsbf8(br, &flen) || bitreader_get(br, 16, &ctxpath) || br->bit_pos != 0 ||
          flen > bitreader_bits_left(br) / 8) {
        rc = -1;
        goto done;
      }
      fuus[i].context_path = (int)ctxpath;
      fuu_phase = resolve_fuu_phase(fuus[i].context_path);
      if (fuu_phase >= 0) {
        if (fuu_phase < phase) {
          rc = -1;
          goto done;
        }
        phase = fuu_phase;
      }
      fuus[i].offset = br->byte_pos;
      fuus[i].length = (size_t)flen;
      br->byte_pos += (size_t)flen;
    }
  }

  for (i = 0; i < nfuu; i++) {
    if (decode_one_fuu(sc, doc, sr, base, &fuus[i], &ptext, &ptext_n)) {
      rc = -1;
      goto done;
    }
  }

  *out_nfuu = nfuu;

done:
  return rc;
}
