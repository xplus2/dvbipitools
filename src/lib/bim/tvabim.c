/* Copyright 2026 dvbipitools authors. Licensed under GPL-3.0-or-later.
 * See NOTICE and LICENSE for details and authorship information. */

#include <stdlib.h>
#include <string.h>

#include "lib/bim/tvabim.h"

#define STEPS_MAX 16

static int fail(bim_err_t *err, const char *what) { return bim_fail(err, BIM_TVABIM_ERR, "bim: TVA BiM %s", what); }

int bim_tvabim_codecs(xsd_codec_map_t out[BIM_TVABIM_CODECS]) {
  out[0] = (xsd_codec_map_t){XSD_NS, "dateTime", BIM_TVA_DATETIME, 1};
  out[1] = (xsd_codec_map_t){XSD_NS, "date", BIM_TVA_DATE, 1};
  return BIM_TVABIM_CODECS;
}

void bim_tvabim_state(const xsd_compiled_t *c, const bim_decoderinit_t *di, const bim_decode_opts_t *o, bim_fuu_state_t *st) {
  memset(st, 0, sizeof *st);
  st->di = di;
  st->schemas = o && o->nschemas ? o->schemas : &c->schema;
  st->nschemas = o && o->nschemas ? o->nschemas : 1;
  if (o) st->opts = *o;
  st->opts.lax_schema = -1;
  st->opts.mpegb = 0;
}

static bim_tbc_t tbc(bim_tbc_kind_t kind, const char *name, const char *ns, const bim_type_t *type) {
  bim_tbc_t t;
  memset(&t, 0, sizeof t);
  t.kind = kind;
  t.name = name;
  t.ns = ns;
  t.type = type;
  return t;
}

/* 4.4.2.3: absolute path from TVAMain to parent of fragment, fragment as operand */
static int build_fuu(const xsd_compiled_t *c, const bim_tvans_t *ns, const bim_tvabim_fuu_t *f, bim_fuu_t *u, bim_err_t *err) {
  bim_tvaroot_t steps[STEPS_MAX];
  int k = bim_tvafrag_steps(c, ns, f->code, steps, STEPS_MAX);
  size_t n;
  memset(u, 0, sizeof *u);
  if (k < 1) return fail(err, "fragment type not in the schema");
  if (f->command < BIM_CMD_ADD || f->command > BIM_CMD_DELETE || (f->command != BIM_CMD_DELETE && !f->root)) return fail(err, "command invalid");
  n = k == 1 ? 2 : (size_t)k + 1;
  u->tbcs = calloc(n, sizeof *u->tbcs);
  u->pos = calloc(n, sizeof *u->pos);
  u->payloads = calloc(1, sizeof *u->payloads);
  if (!u->tbcs || !u->pos || !u->payloads) return bim_oom(err);
  if (k == 1) {
    u->tbcs[0] = tbc(BIM_TBC_END, NULL, NULL, NULL);
  } else {
    for (int i = 0; i < k - 1; i++) u->tbcs[i] = tbc(BIM_TBC_ELEM, steps[i].name, steps[i].ns, steps[i].type);
    u->tbcs[k - 1] = tbc(BIM_TBC_END, NULL, NULL, steps[k - 2].type);
  }
  u->tbcs[n - 1] = tbc(BIM_TBC_ELEM, steps[k - 1].name, steps[k - 1].ns, steps[k - 1].type);
  u->ntbcs = n;
  u->command = f->command;
  u->mode = 1;
  u->npayloads = 1;
  u->pos[n - 1] = f->position;
  u->payloads[0] = (bim_node_t *)f->root;
  return 0;
}

static void release(bim_fuu_t *u) {
  free(u->tbcs);
  free(u->pos);
  free(u->payloads);
}

int bim_tvabim_au_write(const xsd_compiled_t *c, const bim_tvans_t *ns, bim_fuu_state_t *st, const bim_encode_opts_t *eo, const bim_tvabim_fuu_t *f, size_t n, bitwriter_t *bw,
                        bim_fragref_t *refs, bim_err_t *err) {
  bim_au_state_t as;
  bim_au_t au;
  bim_encode_opts_t o = *eo;
  strrepo_writer_t sw;
  size_t off = (size_t)bitwriter_vluimsbf8_len(n);
  int rc = -1;
  memset(&au, 0, sizeof au);
  memset(err, 0, sizeof *err);
  if (!o.nschemas) {
    o.schemas = &c->schema;
    o.nschemas = 1;
  }
  o.mpegb = 0;
  if (bim_au_state_init(&as, st->di)) return bim_oom(err);
  if (strrepo_writer_init(&sw)) {
    bim_au_state_free(&as);
    return bim_oom(err);
  }
  if (n && !(au.fuus = calloc(n, sizeof *au.fuus))) {
    bim_oom(err);
    goto out;
  }
  for (size_t i = 0; i < n; i++) {
    bim_fuu_t u;
    bitwriter_t fb;
    const unsigned char *p;
    size_t len;
    bitwriter_init(&fb);
    o.cast = eo->cast | (f[i].root && bim_tree_has_cast(f[i].root));
    rc = build_fuu(c, ns, &f[i], &u, err) || bim_fuu_write(st, &u, &o, &sw, &fb, err);
    release(&u);
    p = rc ? NULL : bitwriter_data(&fb, &len);
    if (!rc && sw.len > 1) rc = fail(err, "fragment needs an external string buffer");
    if (rc) {
      bitwriter_free(&fb);
      rc = -1;
      goto out;
    }
    au.fuus[i].bits = malloc(len ? len : 1);
    if (!au.fuus[i].bits) {
      bitwriter_free(&fb);
      bim_oom(err);
      rc = -1;
      goto out;
    }
    memcpy(au.fuus[i].bits, p, len);
    au.fuus[i].nbits = (uint64_t)len * 8;
    au.nfuus = i + 1;
    if (refs) {
      refs[i].au_off = (uint32_t)off;
      refs[i].str_off = 0;
    }
    off += (size_t)bitwriter_vluimsbf8_len(len) + len;
    bitwriter_free(&fb);
  }
  rc = bim_au_write(&as, &au, bw, err);
out:
  bim_au_free(&au);
  bim_au_state_free(&as);
  strrepo_writer_free(&sw);
  return rc ? -1 : 0;
}

void bim_tvabim_au_free(bim_tvabim_fuu_t *f, size_t n) {
  for (size_t i = 0; f && i < n; i++) bim_node_free(f[i].root);
  free(f);
}

int bim_tvabim_au_parse(const xsd_compiled_t *c, const bim_tvans_t *ns, bim_fuu_state_t *st, const unsigned char *buf, size_t len, bim_tvabim_fuu_t **out, size_t *n, bim_err_t *err) {
  bim_au_state_t as;
  bim_au_t au;
  bim_tvabim_fuu_t *f;
  size_t used;
  size_t k = 0;
  *out = NULL;
  *n = 0;
  if (bim_au_state_init(&as, st->di)) return bim_oom(err);
  if (bim_au_parse(&as, buf, len, &au, &used, err)) {
    bim_au_state_free(&as);
    return -1;
  }
  f = calloc(au.nfuus + 1, sizeof *f);
  if (!f) {
    bim_au_free(&au);
    bim_au_state_free(&as);
    return bim_oom(err);
  }
  for (size_t i = 0; i < au.nfuus; i++) {
    bim_fuu_t u;
    const bim_tbc_t *op;
    int code;
    if (bim_fuu_parse(st, &au.fuus[i], &u, err)) goto bad;
    op = u.ntbcs ? &u.tbcs[u.ntbcs - 1] : NULL;
    code = op && op->kind == BIM_TBC_ELEM && u.mode == 1 ? bim_tvafrag_code(c, ns, op->name, op->ns, op->type) : -1;
    if (code >= 0) {
      f[k].code = (unsigned)code;
      f[k].command = u.command;
      f[k].position = op->has_pos ? op->pos : 0;
      if (u.payloads && u.npayloads == 1) {
        f[k].root = u.payloads[0];
        u.payloads[0] = NULL;
      }
      k++;
    }
    bim_fuu_free(&u);
  }
  bim_au_free(&au);
  bim_au_state_free(&as);
  *out = f;
  *n = k;
  return 0;
bad:
  bim_tvabim_au_free(f, k);
  bim_au_free(&au);
  bim_au_state_free(&as);
  return -1;
}
