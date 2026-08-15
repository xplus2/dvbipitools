/* Copyright 2026 dvbipitools authors. Licensed under GPL-3.0-or-later.
 * See NOTICE and LICENSE for details and authorship information. */

#include <limits.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "lib/ioutil.h"

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

int accessunit_encode(const bcg_doc_t *doc, bitwriter_t *bw, strrepo_writer_t *sw, int *out_nfuu) {
  int i, j, nfuu = 0;

  for (i = 0; i < doc->programme_count; i++) {
    const bcg_channel_t *c = bcg_find_channel(doc, doc->programmes[i].channel_id);
    if (c && c->uri[0])
      nfuu++;
  }
  for (i = 0; i < doc->channel_count; i++) {
    const bcg_channel_t *c = &doc->channels[i];
    if (!c->uri[0])
      continue;
    for (j = 0; j < doc->programme_count; j++)
      if (!strcmp(doc->programmes[j].channel_id, c->id)) {
        nfuu++;
        break;
      }
  }
  for (i = 0; i < doc->channel_count; i++)
    if (doc->channels[i].uri[0])
      nfuu++;

  if (bitwriter_put_vluimsbf8(bw, (uint64_t)nfuu))
    return -1;

  for (i = 0; i < doc->programme_count; i++) {
    const bcg_programme_t *pr = &doc->programmes[i];
    const bcg_channel_t *c = bcg_find_channel(doc, pr->channel_id);
    bitwriter_t fbw;
    if (!c || !c->uri[0])
      continue;
    bitwriter_init(&fbw);
    if (fragment_encode_program_information(pr, &fbw, sw) || emit_fuu(bw, DVBCTXPATH_PROGRAM_INFORMATION, &fbw)) {
      bitwriter_free(&fbw);
      return -1;
    }
    bitwriter_free(&fbw);
  }

  for (i = 0; i < doc->channel_count; i++) {
    const bcg_channel_t *c = &doc->channels[i];
    bitwriter_t fbw;
    int any = 0;
    if (!c->uri[0])
      continue;
    for (j = 0; j < doc->programme_count; j++)
      if (!strcmp(doc->programmes[j].channel_id, c->id)) {
        any = 1;
        break;
      }
    if (!any)
      continue;
    bitwriter_init(&fbw);
    if (fragment_encode_schedule(c->id, doc->programmes, doc->programme_count, &fbw, sw) || emit_fuu(bw, DVBCTXPATH_SCHEDULE, &fbw)) {
      bitwriter_free(&fbw);
      return -1;
    }
    bitwriter_free(&fbw);
  }

  for (i = 0; i < doc->channel_count; i++) {
    const bcg_channel_t *c = &doc->channels[i];
    bitwriter_t fbw;
    if (!c->uri[0])
      continue;
    bitwriter_init(&fbw);
    if (fragment_encode_service_information(c, &fbw, sw) || emit_fuu(bw, DVBCTXPATH_SERVICE_INFORMATION, &fbw)) {
      bitwriter_free(&fbw);
      return -1;
    }
    bitwriter_free(&fbw);
  }

  *out_nfuu = nfuu;
  return 0;
}

typedef struct {
  char crid[BCG_ID_LEN * 3 + 64];
  char title[BCG_TEXT_LEN];
  char desc[BCG_TEXT_LEN];
  char category[BCG_ID_LEN];
} ptext_t;

typedef struct {
  ptext_t *arr;
  int n;
} ptext_ctx_t;

static int ptext_lookup(void *vctx, const char *crid, bcg_programme_t *pr) {
  ptext_ctx_t *ctx = (ptext_ctx_t *)vctx;
  int i;
  for (i = 0; i < ctx->n; i++)
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
  memset(sc, 0, sizeof *sc);
}

int accessunit_decode(accessunit_scratch_t *sc, bitreader_t *br, strrepo_reader_t *sr, bcg_doc_t *doc, int *out_nfuu) {
  const unsigned char *base = br->buf;
  ptext_t *ptext;
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
    int newcap = sc->fuus_cap ? sc->fuus_cap * 2 : 32;
    void *np;
    if (newcap < nfuu)
      newcap = nfuu;
    np = realloc(sc->fuus, (size_t)newcap * sizeof *fuus);
    if (!np)
      return -1;
    sc->fuus = np;
    sc->fuus_cap = newcap;
  }
  fuus = sc->fuus;
  ptext = sc->ptext;

  for (i = 0; i < nfuu; i++) {
    uint64_t flen, ctxpath;
    if (bitreader_get_vluimsbf8(br, &flen) || bitreader_get(br, 16, &ctxpath) || br->bit_pos != 0 ||
        flen > bitreader_bits_left(br) / 8) {
      rc = -1;
      goto done;
    }
    fuus[i].context_path = (int)ctxpath;
    fuus[i].offset = br->byte_pos;
    fuus[i].length = (size_t)flen;
    br->byte_pos += (size_t)flen;
  }

  for (i = 0; i < nfuu; i++) {
    bitreader_t fbr;
    bcg_programme_t tmp;
    if (fuus[i].context_path != DVBCTXPATH_PROGRAM_INFORMATION)
      continue;
    bitreader_init(&fbr, base + fuus[i].offset, fuus[i].length);
    if (ptext_n >= sc->ptext_cap) {
      int newcap = sc->ptext_cap ? sc->ptext_cap * 2 : 32;
      void *np = realloc(sc->ptext, (size_t)newcap * sizeof *ptext);
      if (!np) {
        rc = -1;
        goto done;
      }
      sc->ptext = np;
      sc->ptext_cap = newcap;
      ptext = sc->ptext;
    }
    if (fragment_decode_program_information(&fbr, sr, ptext[ptext_n].crid, sizeof ptext[ptext_n].crid, &tmp)) {
      rc = -1;
      goto done;
    }
    bufcpy(ptext[ptext_n].title, sizeof ptext[ptext_n].title, tmp.title);
    bufcpy(ptext[ptext_n].desc, sizeof ptext[ptext_n].desc, tmp.desc);
    bufcpy(ptext[ptext_n].category, sizeof ptext[ptext_n].category, tmp.category);
    ptext_n++;
  }

  for (i = 0; i < nfuu; i++) {
    bitreader_t fbr;
    if (fuus[i].context_path == DVBCTXPATH_SCHEDULE) {
      ptext_ctx_t ctx;
      ctx.arr = ptext;
      ctx.n = ptext_n;
      bitreader_init(&fbr, base + fuus[i].offset, fuus[i].length);
      if (fragment_decode_schedule(&fbr, sr, doc, ptext_lookup, &ctx)) {
        rc = -1;
        goto done;
      }
    } else if (fuus[i].context_path == DVBCTXPATH_SERVICE_INFORMATION) {
      bcg_channel_t *c = bcg_add_channel(doc);
      if (!c) {
        rc = -1;
        goto done;
      }
      bitreader_init(&fbr, base + fuus[i].offset, fuus[i].length);
      if (fragment_decode_service_information(&fbr, sr, c)) {
        rc = -1;
        goto done;
      }
    }
  }

  *out_nfuu = nfuu;

done:
  return rc;
}
