/* Copyright 2026 dvbipitools authors. Licensed under GPL-3.0-or-later.
 * See NOTICE and LICENSE for details and authorship information. */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

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

int accessunit_encode(const epg_doc_t *doc, bitwriter_t *bw, strrepo_writer_t *sw, int *out_nfuu) {
  int i, j, nfuu = 0;

  for (i = 0; i < doc->programme_count; i++) {
    const epg_channel_t *c = epg_find_channel(doc, doc->programmes[i].channel_id);
    if (c && c->uri[0])
      nfuu++;
  }
  for (i = 0; i < doc->channel_count; i++) {
    const epg_channel_t *c = &doc->channels[i];
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
    const epg_programme_t *pr = &doc->programmes[i];
    const epg_channel_t *c = epg_find_channel(doc, pr->channel_id);
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
    const epg_channel_t *c = &doc->channels[i];
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
    const epg_channel_t *c = &doc->channels[i];
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
  char crid[EPG_ID_LEN * 3 + 64];
  char title[EPG_TEXT_LEN];
  char desc[EPG_TEXT_LEN];
  char category[EPG_ID_LEN];
} ptext_t;

typedef struct {
  ptext_t *arr;
  int n;
} ptext_ctx_t;

static int ptext_lookup(void *vctx, const char *crid, epg_programme_t *pr) {
  ptext_ctx_t *ctx = (ptext_ctx_t *)vctx;
  int i;
  for (i = 0; i < ctx->n; i++)
    if (!strcmp(ctx->arr[i].crid, crid)) {
      snprintf(pr->title, sizeof pr->title, "%s", ctx->arr[i].title);
      snprintf(pr->desc, sizeof pr->desc, "%s", ctx->arr[i].desc);
      snprintf(pr->category, sizeof pr->category, "%s", ctx->arr[i].category);
      return 0;
    }
  return -1;
}

typedef struct {
  int context_path;
  size_t offset;
  size_t length;
} fuu_index_t;

int accessunit_decode(bitreader_t *br, strrepo_reader_t *sr, epg_doc_t *doc, int *out_nfuu) {
  const unsigned char *base = br->buf;
  ptext_t *ptext = NULL;
  int ptext_n = 0, ptext_cap = 0;
  fuu_index_t *fuus = NULL;
  int nfuu, i, rc = 0;
  uint64_t n64;

  if (bitreader_get_vluimsbf8(br, &n64))
    return -1;
  nfuu = (int)n64;
  fuus = malloc((size_t)(nfuu > 0 ? nfuu : 1) * sizeof *fuus);
  if (!fuus)
    return -1;

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
    epg_programme_t tmp;
    if (fuus[i].context_path != DVBCTXPATH_PROGRAM_INFORMATION)
      continue;
    bitreader_init(&fbr, base + fuus[i].offset, fuus[i].length);
    if (ptext_n >= ptext_cap) {
      int newcap = ptext_cap ? ptext_cap * 2 : 32;
      void *np = realloc(ptext, (size_t)newcap * sizeof *ptext);
      if (!np) {
        rc = -1;
        goto done;
      }
      ptext = np;
      ptext_cap = newcap;
    }
    if (fragment_decode_program_information(&fbr, sr, ptext[ptext_n].crid, sizeof ptext[ptext_n].crid, &tmp)) {
      rc = -1;
      goto done;
    }
    snprintf(ptext[ptext_n].title, sizeof ptext[ptext_n].title, "%s", tmp.title);
    snprintf(ptext[ptext_n].desc, sizeof ptext[ptext_n].desc, "%s", tmp.desc);
    snprintf(ptext[ptext_n].category, sizeof ptext[ptext_n].category, "%s", tmp.category);
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
      epg_channel_t *c = epg_add_channel(doc);
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
  free(fuus);
  free(ptext);
  return rc;
}
