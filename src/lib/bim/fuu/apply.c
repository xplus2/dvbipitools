/* Copyright 2026 dvbipitools authors. Licensed under GPL-3.0-or-later.
 * See NOTICE and LICENSE for details and authorship information. */

#include <stdlib.h>
#include <string.h>

#include "lib/bim/fuu/priv.h"

void bim_fuu_state_close(bim_fuu_state_t *st) {
  bim_fuu_state_free(st);
  if (st->aod_cur != st->aod_default) bim_aod_free(st->aod_cur);
  bim_aod_free(st->aod_default);
  st->aod_cur = st->aod_default = NULL;
  st->opts.aod = NULL;
}

int bim_fuu_state_aod_init(bim_fuu_state_t *st, bim_err_t *err) {
  if (!st->di->aod) return 0;
  if (bim_aod_build(st->di, st->di, st->schemas, (int)st->nschemas, &st->aod_default, err)) return -1;
  st->aod_cur = st->aod_default;
  st->opts.aod = st->aod_cur;
  return 0;
}

static int state(bim_err_t *err, const char *what) { return bim_fail(err, BIM_FUU_STATE, "bim: FUU %s", what); }

void bim_fuu_state_free(bim_fuu_state_t *st) {
  bim_node_free(st->doc);
  st->doc = NULL;
  st->nctx = 0;
}

static int same_name(const bim_node_t *k, const char *name, const char *ns) {
  if (!name || !k->name) return !name && !k->name;
  return bim_node_is(k, ns, name);
}

static int sbc_index(const bim_type_t *pt, const char *name, const char *ns) {
  for (int i = 0; pt && name && i < pt->nsbc; i++)
    if (pt->sbc[i].name && !strcmp(pt->sbc[i].name, name) && ((!pt->sbc[i].ns && !ns) || (pt->sbc[i].ns && ns && !strcmp(pt->sbc[i].ns, ns)))) return i;
  return pt ? pt->nsbc : 0;
}

/* 6.6.5.5.4 rational: positions of z consecutive nodes */
static void rat_implicit(uint64_t z, uint64_t *out) {
  int b = bim_width(z + 1);
  uint64_t step = b >= 64 ? 1 : 1ULL << (64 - b);
  uint64_t n = 1ULL << (b < 63 ? b : 63);
  uint64_t p = 0;
  uint64_t i = 0;
  while (i < z && (i <= (3 * z - n + 3) / 2 || !(i % 2))) {
    p += step;
    out[i++] = p;
  }
  while (i < z) {
    p += 2 * step;
    out[i++] = p;
  }
}

/* 6.6.5.5.4: nodes added by payload get positions in document order, MPC per parent, SPC per name */
static int materialize(const bim_fuu_state_t *st, bim_node_t *parent, const bim_type_t *pt, bim_err_t *err) {
  int mpc = bim_fuu_uses_mpc(st, pt);
  for (int i = 0; i < parent->nkids; i++) {
    bim_node_t *k = parent->kids[i];
    uint64_t idx = 0;
    uint64_t z = 0;
    if (k->pos_set) continue;
    for (int j = 0; j < parent->nkids; j++)
      if (mpc || same_name(parent->kids[j], k->name, k->ns)) {
        idx += j < i;
        z++;
      }
    k->pos = idx;
    if (st->di->insert) {
      uint64_t *pos = malloc((z ? z : 1) * sizeof *pos);
      if (!pos) return bim_oom(err);
      rat_implicit(z, pos);
      if (idx >= z) {
        free(pos);
        return state(err, "position index out of range");
      }
      k->pos = pos[idx];
      free(pos);
    }
    k->pos_set = 1;
  }
  return 0;
}

/* -1 absent, -2 error, -3 position taken by other element */
static int find_child(bim_fuu_state_t *st, bim_node_t *parent, const bim_type_t *pt, const bim_tbc_t *t, uint64_t pos, bim_err_t *err) {
  int mpc = bim_fuu_uses_mpc(st, pt);
  if (materialize(st, parent, pt, err)) return -2;
  for (int i = 0; i < parent->nkids; i++) {
    const bim_node_t *k = parent->kids[i];
    int same = same_name(k, t->name, t->ns);
    if (!t->has_pos) {
      if (same) return i;
      continue;
    }
    if (k->pos != pos || (!same && !mpc)) continue;
    return same ? i : -3;
  }
  return -1;
}

/* MPC by position, SPC by SBC index then position */
static int insert_child(const bim_fuu_state_t *st, bim_node_t *parent, const bim_type_t *pt, bim_node_t *k, bim_err_t *err) {
  int mpc = bim_fuu_uses_mpc(st, pt);
  int ki = sbc_index(pt, k->name, k->ns);
  int at;
  if (bim_node_add(parent, k)) return bim_oom(err);
  for (at = 0; at < parent->nkids - 1; at++) {
    const bim_node_t *o = parent->kids[at];
    int oi = sbc_index(pt, o->name, o->ns);
    if (mpc ? o->pos > k->pos : (oi > ki || (oi == ki && o->pos > k->pos))) break;
  }
  memmove(&parent->kids[at + 1], &parent->kids[at], (size_t)(parent->nkids - 1 - at) * sizeof *parent->kids);
  parent->kids[at] = k;
  return 0;
}

static void remove_child(bim_node_t *parent, int i) {
  bim_node_free(parent->kids[i]);
  memmove(&parent->kids[i], &parent->kids[i + 1], (size_t)(parent->nkids - i - 1) * sizeof *parent->kids);
  parent->nkids--;
}

static int find_attr(const bim_node_t *n, const bim_tbc_t *t) {
  for (int i = 0; i < n->nattrs; i++)
    if (!strcmp(n->attrs[i].name, t->name) && ((!n->attrs[i].ns && !t->ns) || (n->attrs[i].ns && t->ns && !strcmp(n->attrs[i].ns, t->ns)))) return i;
  return -1;
}

static int op_attr(bim_node_t *n, const bim_tbc_t *t, unsigned cmd, char **v, bim_err_t *err) {
  int i = find_attr(n, t);
  const char *name;
  if ((cmd == BIM_CMD_ADD) != (i < 0)) return state(err, cmd == BIM_CMD_ADD ? "AddContent on instantiated attribute" : "attribute not instantiated");
  if (i >= 0) {
    free(n->attrs[i].value);
    memmove(&n->attrs[i], &n->attrs[i + 1], (size_t)(n->nattrs - i - 1) * sizeof *n->attrs);
    n->nattrs--;
  }
  if (cmd == BIM_CMD_DELETE) return 0;
  if (!(name = t->owned ? bim_node_keep(n, t->name) : t->name)) return bim_oom(err);
  if (bim_node_add_attr_ns(n, t->ns, name, *v) == -1) {
    *v = NULL;
    return bim_oom(err);
  }
  *v = NULL;
  return 0;
}

static int op_text(bim_node_t *n, unsigned cmd, char **v, bim_err_t *err) {
  if ((cmd == BIM_CMD_ADD) != !n->text) return state(err, cmd == BIM_CMD_ADD ? "AddContent on instantiated content" : "content not instantiated");
  free(n->text);
  n->text = NULL;
  if (cmd != BIM_CMD_DELETE) {
    n->text = *v;
    *v = NULL;
  }
  return 0;
}

static int op_child(bim_fuu_state_t *st, bim_fuu_t *f, size_t pi, bim_node_t *parent, const bim_type_t *pt, bim_err_t *err) {
  const bim_tbc_t *op = &f->tbcs[f->ntbcs - 1];
  uint64_t pos = op->has_pos ? f->pos[pi * f->ntbcs + f->ntbcs - 1] : 0;
  int idx = find_child(st, parent, pt, op, pos, err);
  bim_node_t *k;
  if (idx == -2) return -1;
  if (idx == -3 || (f->command == BIM_CMD_ADD && idx >= 0)) return state(err, "AddContent on instantiated node");
  if (f->command != BIM_CMD_ADD && idx < 0) return state(err, "operand node not instantiated");
  if (idx >= 0) remove_child(parent, idx);
  if (f->command == BIM_CMD_DELETE) return 0;
  if (op->kind == BIM_TBC_CHARDATA) {
    if (!(k = bim_node_new(NULL))) return bim_oom(err);
    k->text = f->values[pi];
    f->values[pi] = NULL;
  } else {
    k = f->payloads[pi];
    f->payloads[pi] = NULL;
  }
  k->pos = pos;
  k->pos_set = 1;
  if (insert_child(st, parent, pt, k, err)) {
    bim_node_free(k);
    return -1;
  }
  return 0;
}

static int op_topmost(bim_fuu_state_t *st, bim_fuu_t *f, size_t pi, bim_err_t *err) {
  const bim_tbc_t *op = &f->tbcs[f->ntbcs - 1];
  if (op->kind != BIM_TBC_ELEM) return state(err, "topmost operand not an element");
  if (st->doc && !same_name(st->doc, op->name, op->ns) && f->command != BIM_CMD_REPLACE) return state(err, "topmost node differs");
  if ((f->command == BIM_CMD_ADD) != !st->doc) return state(err, st->doc ? "AddContent on instantiated node" : "operand node not instantiated");
  bim_node_free(st->doc);
  st->doc = NULL;
  if (f->command != BIM_CMD_DELETE) {
    st->doc = f->payloads[pi];
    f->payloads[pi] = NULL;
  }
  st->nctx = 0;
  st->last_schema = f->schema;
  return 0;
}

/* context nodes along path, created for AddContent */
static int descend(bim_fuu_state_t *st, const bim_fuu_t *f, size_t pi, bim_node_t **chain, const bim_type_t **types, int *depth, bim_err_t *err) {
  const uint64_t *pos = &f->pos[pi * f->ntbcs];
  size_t i = 0;
  if (f->mode != 2 && f->mode != 4) {
    const bim_tbc_t *t = &f->tbcs[0];
    if (!st->doc) {
      if (f->command != BIM_CMD_ADD) return state(err, "context node not instantiated");
      if (!(st->doc = bim_node_new(t->name))) return bim_oom(err);
      st->doc->ns = t->ns;
    } else if (!same_name(st->doc, t->name, t->ns)) {
      return state(err, "topmost node differs");
    }
    chain[0] = st->doc;
    types[0] = t->type;
    *depth = 1;
    i = 1;
  }
  for (; i + 1 < f->ntbcs; i++) {
    const bim_tbc_t *t = &f->tbcs[i];
    bim_node_t *parent = chain[*depth - 1];
    int idx;
    if (t->kind == BIM_TBC_END) continue;
    if (t->kind == BIM_TBC_PARENT) {
      if (*depth < 2) return state(err, "Reference to Parent above topmost node");
      (*depth)--;
      continue;
    }
    if (*depth >= BIM_CTX_MAX) return state(err, "context path too deep");
    if ((idx = find_child(st, parent, types[*depth - 1], t, pos[i], err)) == -2) return -1;
    if (idx == -3) return state(err, "position taken by another element");
    if (idx < 0) {
      bim_node_t *k;
      if (f->command != BIM_CMD_ADD) return state(err, "context node not instantiated");
      if (!(k = bim_node_new(t->name))) return bim_oom(err);
      k->ns = t->ns;
      k->pos = t->has_pos ? pos[i] : 0;
      k->pos_set = 1;
      if (insert_child(st, parent, types[*depth - 1], k, err)) {
        bim_node_free(k);
        return -1;
      }
      idx = find_child(st, parent, types[*depth - 1], t, pos[i], err);
    }
    chain[*depth] = parent->kids[idx];
    types[(*depth)++] = t->type;
  }
  return 0;
}

static int apply_one(bim_fuu_state_t *st, bim_fuu_t *f, size_t pi, bim_err_t *err) {
  bim_node_t *chain[BIM_CTX_MAX] = {NULL};
  const bim_type_t *types[BIM_CTX_MAX] = {NULL};
  const bim_tbc_t *op = &f->tbcs[f->ntbcs - 1];
  int depth = 0;
  int rc;
  if (f->mode == 2 || f->mode == 4) {
    if (st->nctx < 1) return state(err, "relative addressing without context node");
    depth = st->nctx;
    memcpy(chain, st->ctx, (size_t)depth * sizeof chain[0]);
    memcpy(types, st->ctx_type, (size_t)depth * sizeof types[0]);
  } else if (f->tbcs[0].kind == BIM_TBC_END) {
    return op_topmost(st, f, pi, err);
  }
  if (descend(st, f, pi, chain, types, &depth, err)) return -1;
  if (depth < 1) return state(err, "context path empty");
  if (op->kind == BIM_TBC_ATTR) rc = op_attr(chain[depth - 1], op, f->command, &f->values[pi], err);
  else if (op->kind == BIM_TBC_SIMPLE) rc = op_text(chain[depth - 1], f->command, &f->values[pi], err);
  else rc = op_child(st, f, pi, chain[depth - 1], types[depth - 1], err);
  if (rc) return -1;
  memcpy(st->ctx, chain, (size_t)depth * sizeof chain[0]);
  memcpy(st->ctx_type, types, (size_t)depth * sizeof types[0]);
  st->nctx = depth;
  st->last_schema = f->schema;
  return 0;
}

int bim_fuu_apply(bim_fuu_state_t *st, bim_fuu_t *f, bim_err_t *err) {
  memset(err, 0, sizeof *err);
  if (f->command == BIM_CMD_RESET) {
    bim_fuu_state_free(st);
    return 0;
  }
  for (size_t i = 0; !f->user_data && i < f->npayloads; i++)
    if (apply_one(st, f, i, err)) return -1;
  return 0;
}
