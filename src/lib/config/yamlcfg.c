/* Copyright 2026 dvbipitools authors. Licensed under GPL-3.0-or-later.
 * See NOTICE and LICENSE for details and authorship information. */

#include <errno.h>
#include <math.h>
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>
#include <sys/stat.h>
#include <unistd.h>

#include "lib/helper/argutil.h"
#include "lib/helper/ioutil.h"
#include "lib/helper/log.h"
#include "lib/vendor/libyaml/yaml.h"
#include "yamlcfg.h"

#define MAX_DEPTH 16
#define KEYPATH_MAX 256

typedef struct {
  size_t plen;
  int want_key;
  int is_seq;
  int is_item;
  int keyed;
  int opts;
  unsigned items;
} frame_t;

typedef struct {
  yamlcfg_t *y;
  const yamlcfg_key_t *keys;
  size_t nkeys;
  void *cfg;
  yamlcfg_item_fn item_fn;
  unsigned char *seen;
  char path[KEYPATH_MAX];
  frame_t st[MAX_DEPTH];
  int depth;
} walk_t;

static int soft(const yamlcfg_t *y) {
  return y->check || y->strict;
}

static void vreport(const yamlcfg_t *y, const char *kind, int line, const char *fmt, va_list ap) __attribute__((format(printf, 4, 0)));

static void vreport(const yamlcfg_t *y, const char *kind, int line, const char *fmt, va_list ap) {
  char body[512];
  vsnprintf(body, sizeof body, fmt, ap);
  if (line > 0) argutil_err(y->tool, "%s:%d: %s%s", y->path, line, kind, body);
  else          argutil_err(y->tool, "%s: %s%s", y->path, kind, body);
}

static int fail(const yamlcfg_t *y, int line, const char *fmt, ...) __attribute__((format(printf, 3, 4)));

static int fail(const yamlcfg_t *y, int line, const char *fmt, ...) {
  va_list ap;
  va_start(ap, fmt);
  vreport(y, "", line, fmt, ap);
  va_end(ap);
  return YAMLCFG_ERROR;
}

static void warn_at(yamlcfg_t *y, int line, const char *fmt, ...) __attribute__((format(printf, 3, 4)));

static void warn_at(yamlcfg_t *y, int line, const char *fmt, ...) {
  va_list ap;
  va_start(ap, fmt);
  vreport(y, y->strict ? "error: " : "warning: ", line, fmt, ap);
  va_end(ap);
  y->warnings++;
}

void yamlcfg_warn(yamlcfg_t *y, const char *fmt, ...) {
  va_list ap;
  va_start(ap, fmt);
  vreport(y, y->strict ? "error: " : "warning: ", 0, fmt, ap);
  va_end(ap);
  y->warnings++;
}

int yamlcfg_report(const yamlcfg_t *y) {
  const char *noun = y->strict ? "error" : "warning";
  if (y->warnings) argutil_err(y->tool, "%s: %u %s%s", y->path, y->warnings, noun, y->warnings == 1 ? "" : "s");
  else             argutil_err(y->tool, "%s: ok", y->path);
  return y->strict && y->warnings ? -1 : 0;
}

int yamlcfg_parse_bool(const char *val, int *out) {
  static const char *const yes[] = {"on", "yes", "true", "1"};
  static const char *const no[] = {"off", "no", "false", "0"};
  for (size_t i = 0; i < sizeof yes / sizeof yes[0]; i++) {
    if (!strcasecmp(val, yes[i])) {
      *out = 1;
      return 0;
    }
    if (!strcasecmp(val, no[i])) {
      *out = 0;
      return 0;
    }
  }
  return -1;
}

static int path_push(walk_t *w, const char *key, size_t klen) {
  size_t n = strlen(w->path);
  if (n + (n ? 1 : 0) + klen >= sizeof w->path) return -1;
  if (n) w->path[n++] = '.';
  memcpy(w->path + n, key, klen);
  w->path[n + klen] = 0;
  return 0;
}

static int scalar_is_null(const yaml_event_t *e) {
  const char *v = (const char *)e->data.scalar.value;
  if (e->data.scalar.style != YAML_PLAIN_SCALAR_STYLE) return 0;
  return e->data.scalar.length == 0 || !strcmp(v, "~") || !strcasecmp(v, "null");
}

static int apply_value(walk_t *w, const char *val, int line, int in_seq, unsigned item) {
  yamlcfg_t *y = w->y;
  char err[192] = "";
  size_t i;
  for (i = 0; i < w->nkeys; i++) if (!strcmp(w->keys[i].key, w->path)) break;
  if (i == w->nkeys) {
    warn_at(y, line, "unknown key '%s'", w->path);
    return 0;
  }
  if (in_seq && !w->keys[i].list) {
    if (!soft(y)) return fail(y, line, "%s: not a list", w->path);
    warn_at(y, line, "%s: not a list", w->path);
    return 0;
  }
  if (w->seen[i] && item == 0) warn_at(y, line, "duplicate key '%s', last one wins", w->path);
  w->seen[i] = 1;
  if (w->keys[i].apply(w->cfg, val, err, sizeof err)) {
    if (!soft(y)) return fail(y, line, "%s: %s", w->path, err[0] ? err : "invalid value");
    warn_at(y, line, "%s: %s", w->path, err[0] ? err : "invalid value");
    return 0;
  }
  if (soft(y) && w->keys[i].must_exist && access(val, R_OK)) warn_at(y, line, "%s: %s: %s", w->path, val, strerror(errno));
  return 0;
}

static int list_kind(const walk_t *w) {
  for (size_t i = 0; i < w->nkeys; i++) if (!strcmp(w->keys[i].key, w->path)) return w->keys[i].list;
  return 0;
}

static int item_event(walk_t *w, int begin, int line) {
  char err[192] = "";
  if (!w->item_fn || !w->item_fn(w->cfg, w->path, begin, err, sizeof err)) return 0;
  if (!soft(w->y)) return fail(w->y, line, "%s: %s", w->path, err[0] ? err : "invalid item");
  warn_at(w->y, line, "%s: %s", w->path, err[0] ? err : "invalid item");
  return 0;
}

static void item_reset_seen(walk_t *w) {
  size_t plen = strlen(w->path);
  for (size_t i = 0; i < w->nkeys; i++) if (!strncmp(w->keys[i].key, w->path, plen) && w->keys[i].key[plen] == '.') w->seen[i] = 0;
}

static int on_map_start(walk_t *w, int line) {
  int item = 0;
  if (w->depth >= MAX_DEPTH) return fail(w->y, line, "nesting too deep");
  if (w->depth > 0 && w->st[w->depth - 1].is_seq) {
    if (list_kind(w) != YAMLCFG_LIST_KEYED) return fail(w->y, line, "mappings inside lists not supported");
    item = 1;
    item_reset_seen(w);
  }
  if (w->depth > 0 && w->st[w->depth - 1].want_key) return fail(w->y, line, "mapping used as key");
  w->st[w->depth].plen = strlen(w->path);
  w->st[w->depth].want_key = 1;
  w->st[w->depth].is_seq = 0;
  w->st[w->depth].is_item = item;
  w->st[w->depth].keyed = item;
  w->st[w->depth].opts = 0;
  w->st[w->depth].items = 0;
  w->depth++;
  return item ? item_event(w, 1, line) : 0;
}

static int on_seq_start(walk_t *w, int line) {
  const frame_t *top;
  if (w->depth <= 0) return fail(w->y, line, "top level must be a mapping");
  if (w->depth >= MAX_DEPTH) return fail(w->y, line, "nesting too deep");
  top = &w->st[w->depth - 1];
  if (top->is_seq) return fail(w->y, line, "nested lists not supported");
  if (top->want_key) return fail(w->y, line, "list used as key");
  if (top->opts) return fail(w->y, line, "%s: options must be a mapping", w->path);
  w->st[w->depth].plen = strlen(w->path);
  w->st[w->depth].want_key = 0;
  w->st[w->depth].is_seq = 1;
  w->st[w->depth].is_item = 0;
  w->st[w->depth].keyed = 0;
  w->st[w->depth].opts = 0;
  w->st[w->depth].items = 0;
  w->depth++;
  return 0;
}

static void on_seq_end(walk_t *w) {
  frame_t *top;
  if (w->depth < 2) return;
  w->depth--;
  top = &w->st[w->depth - 1];
  w->path[top->plen] = 0;
  top->want_key = 1;
}

static int on_map_end(walk_t *w, int line) {
  int item;
  int rc;
  if (w->depth < 1) return fail(w->y, line, "unbalanced mapping end");
  item = w->st[w->depth - 1].is_item;
  rc = item ? item_event(w, 0, line) : 0;
  w->depth--;
  if (w->depth > 0) {
    frame_t *top = &w->st[w->depth - 1];
    w->path[top->plen] = 0;
    if (!top->is_seq) top->want_key = 1;
    top->opts = 0;
  }
  return rc;
}

static int on_scalar(walk_t *w, const yaml_event_t *e, int line) {
  frame_t *top;
  int rc = 0;
  if (w->depth <= 0) return fail(w->y, line, "top level must be a mapping");
  top = &w->st[w->depth - 1];
  if (top->is_seq) {
    if (scalar_is_null(e)) return 0;
    return apply_value(w, (const char *)e->data.scalar.value, line, 1, top->items++);
  }
  if (top->want_key) {
    if (top->keyed && w->depth > 1) {
      rc = apply_value(w, (const char *)e->data.scalar.value, line, 1, w->st[w->depth - 2].items++);
      top->want_key = 0;
      top->opts = 1;
      return rc;
    }
    if (path_push(w, (const char *)e->data.scalar.value, e->data.scalar.length)) return fail(w->y, line, "key too long");
    top->want_key = 0;
    return 0;
  }
  if (top->opts) {
    top->opts = 0;
    top->want_key = 1;
    if (scalar_is_null(e)) return 0;
    if (!soft(w->y)) return fail(w->y, line, "%s: options must be a mapping", w->path);
    warn_at(w->y, line, "%s: options must be a mapping", w->path);
    return 0;
  }
  if (!scalar_is_null(e)) rc = apply_value(w, (const char *)e->data.scalar.value, line, 0, 0);
  w->path[top->plen] = 0;
  top->want_key = 1;
  return rc;
}

static int parse_fail(const yamlcfg_t *y, const yaml_parser_t *p) {
  int line = (int)p->problem_mark.line + 1;
  const char *problem = p->problem ? p->problem : "parse error";
  if (p->context) return fail(y, line, "%s (%s)", problem, p->context);
  return fail(y, line, "%s", problem);
}

static int walk(walk_t *w, yaml_parser_t *p) {
  int docs = 0;

  for (;;) {
    yaml_event_t ev;
    int line;
    int rc = 0;
    int done = 0;
    if (!yaml_parser_parse(p, &ev)) return parse_fail(w->y, p);
    line = (int)ev.start_mark.line + 1;
    switch (ev.type) {
      case YAML_DOCUMENT_START_EVENT:
        if (++docs > 1) rc = fail(w->y, line, "multiple documents not supported");
        break;
      case YAML_STREAM_END_EVENT:
        done = 1;
        break;
      case YAML_MAPPING_START_EVENT:
        rc = on_map_start(w, line);
        break;
      case YAML_MAPPING_END_EVENT:
        rc = on_map_end(w, line);
        break;
      case YAML_SCALAR_EVENT:
        rc = on_scalar(w, &ev, line);
        break;
      case YAML_SEQUENCE_START_EVENT:
        rc = on_seq_start(w, line);
        break;
      case YAML_SEQUENCE_END_EVENT:
        on_seq_end(w);
        break;
      case YAML_ALIAS_EVENT:
        rc = fail(w->y, line, "aliases not supported");
        break;
      default:
        break;
    }
    yaml_event_delete(&ev);
    if (rc || done) return rc;
  }
}

int yamlcfg_load(yamlcfg_t *y, const char *tool, int mode, const char *path, const char *default_path, const yamlcfg_key_t *keys, size_t nkeys, void *cfg) {
  return yamlcfg_load_items(y, tool, mode, path, default_path, keys, nkeys, cfg, NULL);
}

int yamlcfg_load_items(yamlcfg_t *y, const char *tool, int mode, const char *path, const char *default_path, const yamlcfg_key_t *keys, size_t nkeys, void *cfg, yamlcfg_item_fn item) {
  const char *use = path ? path : default_path;
  yaml_parser_t p;
  struct stat sb;
  walk_t *w;
  FILE *f;
  int rc;

  memset(y, 0, sizeof *y);
  y->tool = tool;
  y->check = mode & YAMLCFG_CHECK;
  y->strict = mode & YAMLCFG_STRICT;
  if (!use) return YAMLCFG_ABSENT;
  bufcpy(y->path, sizeof y->path, use);

  f = fopen(use, "r");
  if (!f) {
    if (!path && !y->check && errno == ENOENT) return YAMLCFG_ABSENT;
    return fail(y, 0, "%s", strerror(errno));
  }
  if (fstat(fileno(f), &sb) || !S_ISREG(sb.st_mode)) {
    fclose(f);
    return fail(y, 0, "not a regular file");
  }
  if (!yaml_parser_initialize(&p)) {
    fclose(f);
    return fail(y, 0, "out of memory");
  }
  w = calloc(1, sizeof *w);
  if (w) w->seen = calloc(nkeys ? nkeys : 1, 1);
  if (!w || !w->seen) {
    free(w);
    yaml_parser_delete(&p);
    fclose(f);
    return fail(y, 0, "out of memory");
  }
  w->y = y;
  w->keys = keys;
  w->nkeys = nkeys;
  w->cfg = cfg;
  w->item_fn = item;
  yaml_parser_set_input_file(&p, f);
  rc = walk(w, &p);
  free(w->seen);
  free(w);
  yaml_parser_delete(&p);
  fclose(f);
  if (rc) return YAMLCFG_ERROR;
  if (y->strict && !y->check && y->warnings) {
    yamlcfg_report(y);
    return YAMLCFG_ERROR;
  }
  return YAMLCFG_LOADED;
}

typedef struct strnode {
  struct strnode *next;
  char s[];
} strnode_t;

static strnode_t *strs;

int yamlcfg_set_str(const char **dst, const char *val, char *err, size_t errsz) {
  size_t n = strlen(val);
  strnode_t *node;
  if (!n) {
    snprintf(err, errsz, "empty value");
    return -1;
  }
  node = malloc(sizeof *node + n + 1);
  if (!node) {
    snprintf(err, errsz, "out of memory");
    return -1;
  }
  memcpy(node->s, val, n + 1);
  node->next = strs;
  strs = node;
  *dst = node->s;
  return 0;
}

int yamlcfg_set_bool(int *dst, const char *val, char *err, size_t errsz) {
  if (yamlcfg_parse_bool(val, dst)) {
    snprintf(err, errsz, "invalid boolean '%s'", val);
    return -1;
  }
  return 0;
}

int yamlcfg_set_uint(unsigned *dst, const char *val, unsigned min, unsigned max, char *err, size_t errsz) {
  if (argutil_uint_range(val, min, max, dst)) {
    snprintf(err, errsz, "invalid '%s' (need %u..%u)", val, min, max);
    return -1;
  }
  return 0;
}

int yamlcfg_set_double(double *dst, const char *val, double min, double max, int min_excl, char *err, size_t errsz) {
  char *end;
  double v;
  errno = 0;
  v = strtod(val, &end);
  if (!*val || *end || errno || !isfinite(v) || v > max || (min_excl ? v <= min : v < min)) {
    snprintf(err, errsz, "invalid '%s' (need %s %g..%g)", val, min_excl ? ">" : ">=", min, max);
    return -1;
  }
  *dst = v;
  return 0;
}

int yamlcfg_set_lang(char *dst, const char *val, char *err, size_t errsz) {
  if (strlen(val) != 3) {
    snprintf(err, errsz, "invalid '%s' (3-letter ISO 639-2 code)", val);
    return -1;
  }
  memcpy(dst, val, 3);
  dst[3] = 0;
  return 0;
}

int yamlcfg_set_addrport(int *family, char *addr, size_t addrsz, unsigned *port, const char *val, char *err, size_t errsz) {
  int fam;
  char a[64];
  unsigned p;
  if (argutil_addrport_parse(val, &fam, a, sizeof a, &p) || strlen(a) >= addrsz) {
    snprintf(err, errsz, "invalid addr:port '%s'", val);
    return -1;
  }
  if (family) *family = fam;
  bufcpy(addr, addrsz, a);
  *port = p;
  return 0;
}

int yamlcfg_set_color(int *dst, const char *val, char *err, size_t errsz) {
  log_color_t v;
  if (log_color_from_string(val, &v)) {
    snprintf(err, errsz, "invalid '%s' (auto|always|never)", val);
    return -1;
  }
  *dst = v;
  return 0;
}
