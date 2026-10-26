/* Copyright 2026 dvbipitools authors. Licensed under GPL-3.0-or-later.
 * See NOTICE and LICENSE for details and authorship information. */

#define _GNU_SOURCE
#include <dirent.h>
#include <limits.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>

#include "lib/bim/codec.h"
#include "lib/bim/xsd/xsd.h"
#include "lib/helper/log.h"
#include "lib/helper/xml_util.h"
#include "lib/sys/ioutil.h"

#define DIR_DEPTH_MAX 16

typedef struct {
  char **v;
  int n;
  int cap;
} slist_t;

/* tns read on demand. dir: index of search dir, -1 file given directly */
typedef struct {
  char *path;
  char *tns;
  int scanned;
  int dir;
} xsd_file_t;

struct xsd_index {
  slist_t dirs; /* indexed, as given */
  xsd_file_t *files;
  int nfiles;
  int fcap;
};

typedef struct {
  xsd_set_t *s;
  xsd_index_t *x;
  const char *const *dirs;
  int ndirs;
  slist_t loaded; /* real paths */
  slist_t namespaces; /* "" for none */
} loader_t;

static int oom(loader_t *l) {
  xsd_set_fail(l->s, "LS500101", "out of memory");
  return -2;
}

static int slist_has(const slist_t *l, const char *v) {
  for (int i = 0; i < l->n; i++)
    if (!strcmp(l->v[i], v)) return 1;
  return 0;
}

/* 0 ok, -1 oom */
static int slist_put(slist_t *sl, const char *v) {
  char *d;
  if (sl->n >= sl->cap) {
    void *np = array_grow(sl->v, &sl->cap, sl->n + 1, sizeof *sl->v);
    if (!np) return -1;
    sl->v = np;
  }
  if (!(d = strdup(v))) return -1;
  sl->v[sl->n++] = d;
  return 0;
}

static int slist_add(loader_t *l, slist_t *sl, const char *v) { return slist_put(sl, v) ? oom(l) : 0; }

static void slist_free(slist_t *sl) {
  for (int i = 0; i < sl->n; i++) free(sl->v[i]);
  free(sl->v);
}

xsd_index_t *xsd_index_new(void) { return calloc(1, sizeof(xsd_index_t)); }

void xsd_index_free(xsd_index_t *x) {
  if (!x) return;
  for (int i = 0; i < x->nfiles; i++) {
    free(x->files[i].path);
    free(x->files[i].tns);
  }
  free(x->files);
  slist_free(&x->dirs);
  free(x);
}

/* takes path. 0 ok, -1 oom (path freed) */
static int file_add(xsd_index_t *x, char *path, int dir) {
  if (x->nfiles >= x->fcap) {
    void *np = array_grow(x->files, &x->fcap, x->nfiles + 1, sizeof *x->files);
    if (!np) {
      free(path);
      return -1;
    }
    x->files = np;
  }
  memset(&x->files[x->nfiles], 0, sizeof *x->files);
  x->files[x->nfiles].path = path;
  x->files[x->nfiles++].dir = dir;
  return 0;
}

static int is_url(const char *loc) { return !strncmp(loc, "http://", 7) || !strncmp(loc, "https://", 8) || !strncmp(loc, "urn:", 4); }

static int is_file(const char *p) {
  struct stat st;
  return !stat(p, &st) && S_ISREG(st.st_mode);
}

static int index_dir(loader_t *l, const char *dir, int id, int depth) {
  DIR *d = opendir(dir);
  struct dirent *e;
  int rc = 0;
  if (!d) return xsd_set_fail(l->s, "LS000743", "bim: cannot open directory %s", dir);
  while (!rc) {
    e = readdir(d);
    if (!e) break;
    char p[PATH_MAX];
    struct stat st;
    char *real;
    size_t n = strlen(e->d_name);
    if (e->d_name[0] == '.') continue;
    if (snprintf(p, sizeof p, "%s/%s", dir, e->d_name) >= (int)sizeof p) continue;
    if (stat(p, &st)) continue;
    if (S_ISDIR(st.st_mode)) {
      if (depth < DIR_DEPTH_MAX) rc = index_dir(l, p, id, depth + 1);
    } else if (S_ISREG(st.st_mode) && n > 4 && !strcmp(e->d_name + n - 4, ".xsd")) {
      real = realpath(p, NULL);
      if (real && file_add(l->x, real, id)) rc = oom(l);
    }
  }
  closedir(d);
  return rc;
}

/* search dirs of l not yet in index listed. failed dir left unlisted */
static int ensure_index(loader_t *l) {
  xsd_index_t *x = l->x;
  for (int i = 0; i < l->ndirs; i++) {
    int mark = x->nfiles;
    int rc;
    if (slist_has(&x->dirs, l->dirs[i])) continue;
    rc = index_dir(l, l->dirs[i], x->dirs.n, 0);
    if (!rc && slist_put(&x->dirs, l->dirs[i])) rc = oom(l);
    if (rc) {
      while (x->nfiles > mark) free(x->files[--x->nfiles].path);
      return -1;
    }
  }
  return 0;
}

static int read_doc(loader_t *l, const char *path, xml_doc_t **doc) {
  FILE *f = fopen(path, "rb");
  char *buf = NULL;
  size_t len = 0;
  size_t off = 0;
  int rc;
  if (!f) return xsd_set_fail(l->s, "LS000100", "cannot open %s", path);
  rc = read_all(f, &buf, &len);
  fclose(f);
  if (rc) return xsd_set_fail(l->s, "LS000227", "cannot read %s", path);
  if (xml_to_utf8(&buf, &len, NULL, 0)) {
    free(buf);
    return xsd_set_fail(l->s, "LS400249", "bim: %s: unsupported encoding", path);
  }
  rc = xml_doc_parse(buf, len, doc, &off);
  free(buf);
  if (rc == -2) return oom(l);
  return rc ? xsd_set_fail(l->s, "LS400248", "bim: %s: not well-formed at byte %zu", path, off) : 0;
}

/* tns of schema file at path. *oomf on oom, NULL none or unreadable */
static char *scan_tns(const char *path, int *oomf) {
  FILE *fp;
  char *buf = NULL;
  size_t len = 0;
  char v[1024];
  char *tns = NULL;
  if (!(fp = fopen(path, "rb"))) return NULL;
  int rc = read_all(fp, &buf, &len);
  if (!rc) rc = xml_to_utf8(&buf, &len, NULL, 0);
  if (!rc) rc = xml_attr(buf, buf + len, "targetNamespace", v, sizeof v);
  if (!rc) {
    tns = strdup(v);
    if (!tns) *oomf = 1;
  }
  fclose(fp);
  free(buf);
  return tns;
}

/* targetNamespace of an indexed file, NULL none or unreadable */
static const char *file_tns(xsd_file_t *f, int *oomf) {
  if (f->scanned) return f->tns;
  f->scanned = 1;
  f->tns = scan_tns(f->path, oomf);
  return f->tns;
}

/* tns of a file given directly, cached in index. NULL none, unreadable or oom (*oomf) */
static const char *path_tns(xsd_index_t *x, const char *path, int *oomf) {
  char *p;
  for (int i = 0; i < x->nfiles; i++)
    if (x->files[i].dir < 0 && !strcmp(x->files[i].path, path)) return file_tns(&x->files[i], oomf);
  if (!(p = strdup(path)) || file_add(x, p, -1)) {
    *oomf = 1;
    return NULL;
  }
  return file_tns(&x->files[x->nfiles - 1], oomf);
}

/* file below a search dir of l */
static int in_dirs(const loader_t *l, const xsd_file_t *f) {
  for (int i = 0; f->dir >= 0 && i < l->ndirs; i++)
    if (!strcmp(l->x->dirs.v[f->dir], l->dirs[i])) return 1;
  return 0;
}

/* single indexed file matching basename of loc or namespace ns. 1 found, 0 none, -1 ambiguous, -2 oom */
static int find_indexed(loader_t *l, const char *loc, const char *ns, int by_ns, const char **out) {
  const char *base = loc ? strrchr(loc, '/') : NULL;
  int hits = 0;
  base = base ? base + 1 : loc;
  for (int i = 0; i < l->x->nfiles; i++) {
    xsd_file_t *f = &l->x->files[i];
    const char *b = strrchr(f->path, '/') + 1;
    int oomf = 0;
    const char *tns;
    if (!in_dirs(l, f)) continue;
    if (by_ns) {
      tns = file_tns(f, &oomf);
      if (oomf) return -2;
      if (!tns || strcmp(tns, ns ? ns : "")) continue;
    } else if (!base || strcmp(b, base)) {
      continue;
    }
    if (hits && strcmp(*out, f->path)) return -1;
    *out = f->path;
    hits = 1;
  }
  return hits;
}

/* schemaLocation relative to referencing document, or unique basename in search dirs */
static int locate(loader_t *l, const char *from, const char *loc, char *out, size_t cap) {
  const char *slash = strrchr(from, '/');
  const char *hit = NULL;
  int rc;
  if (loc[0] == '/') snprintf(out, cap, "%s", loc);
  else snprintf(out, cap, "%.*s%s", slash ? (int)(slash - from + 1) : 0, from, loc);
  if (is_file(out)) return 1;
  if (ensure_index(l)) return -1;
  rc = find_indexed(l, loc, NULL, 0, &hit);
  if (rc < 0) return xsd_set_fail(l->s, "LS000745", "bim: %s: schemaLocation %s matches several files in the schema directories", from, loc);
  if (rc) snprintf(out, cap, "%s", hit);
  return rc;
}

static int load_file(loader_t *l, const char *path, char kind, const char *tns);

static int load_ref(loader_t *l, const char *from, const xsd_ref_t *r) {
  char p[PATH_MAX];
  int found = 0;
  if (r->location && !is_url(r->location)) {
    found = locate(l, from, r->location, p, sizeof p);
    if (found < 0) return -1;
  }
  if (r->kind == 'm') {
    const char *hit = NULL;
    int rc;
    if (slist_has(&l->namespaces, r->ns ? r->ns : "")) return 0;
    if (!found) {
      if (ensure_index(l)) return -1;
      rc = find_indexed(l, NULL, r->ns, 1, &hit);
      if (rc == -2) return oom(l);
      if (rc < 0) return xsd_set_fail(l->s, "LS000747", "bim: %s: namespace %s found in several schema files", from, r->ns ? r->ns : "(none)");
      if (!rc) return xsd_set_fail(l->s, "LS000746", "bim: %s: no schema for imported namespace %s", from, r->ns ? r->ns : "(none)");
      snprintf(p, sizeof p, "%s", hit);
    }
    if (load_file(l, p, 'm', NULL)) return -1;
    /* XML Schema Part 1 4.2.3, src-import 3.1 */
    if (slist_has(&l->namespaces, r->ns ? r->ns : "")) return 0;
    return xsd_set_fail(l->s, "LS400276", "bim: %s: import of namespace %s, %s has another targetNamespace", from, r->ns ? r->ns : "(none)", p);
  }
  if (!r->location) return xsd_set_fail(l->s, "LS000748", "bim: %s: %s without schemaLocation", from, r->kind == 'i' ? "include" : "redefine");
  if (!found) return xsd_set_fail(l->s, "LS000744", "bim: %s: schemaLocation %s not found", from, r->location);
  if (load_file(l, p, r->kind, r->ns)) return -1;
  return r->kind == 'r' ? xsd_set_redefine(l->s, r, from) : 0;
}

/* kind 0 main, 'm' import, 'i' include, 'r' redefine target */
static int load_file(loader_t *l, const char *path, char kind, const char *tns) {
  char *real = realpath(path, NULL);
  xml_doc_t *doc = NULL;
  const xsd_ref_t *refs;
  xsd_ref_t *own;
  const char *ns;
  int n;
  int rc;
  if (!real) return xsd_set_fail(l->s, "LS000100", "cannot open %s", path);
  if (slist_has(&l->loaded, real)) {
    free(real);
    return 0;
  }
  rc = slist_add(l, &l->loaded, real) ? -2 : read_doc(l, real, &doc);
  free(real);
  if (rc) return rc;
  path = l->loaded.v[l->loaded.n - 1];
  ns = kind == 'i' || kind == 'r' ? tns : bim_node_attr(xml_doc_root(doc), "targetNamespace");
  if ((rc = kind == 'i' || kind == 'r' ? xsd_set_include_doc(l->s, doc, path, tns) : xsd_set_add_doc(l->s, doc, path))) return rc;
  if (!slist_has(&l->namespaces, ns ? ns : "") && slist_add(l, &l->namespaces, ns ? ns : "")) return -2;
  n = xsd_set_doc_refs(l->s, &refs);
  if (!n) return 0;
  if (!(own = malloc(sizeof *own * (size_t)n))) return oom(l);
  memcpy(own, refs, sizeof *own * (size_t)n);
  for (int i = 0; i < n && !rc; i++) rc = load_ref(l, path, &own[i]);
  free(own);
  return rc;
}

/* l over x, else over a private index (*tmp, freed by loader_done). 0 ok, -2 oom */
static int loader_init(loader_t *l, xsd_set_t *s, xsd_index_t *x, xsd_index_t **tmp, const char *const *dirs, int ndirs) {
  memset(l, 0, sizeof *l);
  l->s = s;
  l->dirs = dirs;
  l->ndirs = ndirs;
  *tmp = x ? NULL : xsd_index_new();
  l->x = x ? x : *tmp;
  return l->x ? 0 : oom(l);
}

static void loader_done(loader_t *l, xsd_index_t *tmp) {
  slist_free(&l->loaded);
  slist_free(&l->namespaces);
  xsd_index_free(tmp);
}

int xsd_set_load(xsd_set_t *s, xsd_index_t *x, const char *path, const char *const *dirs, int ndirs) {
  loader_t l;
  xsd_index_t *tmp;
  int rc = loader_init(&l, s, x, &tmp, dirs, ndirs);
  if (!rc) rc = load_file(&l, path, 0, NULL);
  if (!rc) rc = xsd_set_resolve(s);
  loader_done(&l, tmp);
  return rc;
}

int xsd_find_schema(xsd_set_t *s, xsd_index_t *x, const char *const *dirs, int ndirs, const char *ns, char *out, size_t cap) {
  loader_t l;
  xsd_index_t *tmp;
  const char *hit = NULL;
  int rc = loader_init(&l, s, x, &tmp, dirs, ndirs);
  if (rc) return -1;
  if (ensure_index(&l)) {
    rc = -1;
  } else {
    rc = 1;
    for (int i = 0; i < l.x->nfiles && rc != -1; i++) {
      int oomf = 0;
      const char *t;
      if (!in_dirs(&l, &l.x->files[i])) continue;
      t = file_tns(&l.x->files[i], &oomf);
      if (oomf) {
        oom(&l);
        rc = -1;
        continue;
      }
      if (!t || strcmp(t, ns)) continue;
      if (!hit) {
        hit = l.x->files[i].path;
        rc = 0;
      } else if (strcmp(hit, l.x->files[i].path)) {
        rc = 2;
      }
    }
    if (hit) snprintf(out, cap, "%s", hit);
  }
  loader_done(&l, tmp);
  return rc;
}

static char *dir_of(const char *path) {
  const char *slash = strrchr(path, '/');
  size_t n = slash ? (size_t)(slash - path) : 0;
  char *d;
  if (!slash) return strdup(".");
  if (!n) n = 1;
  d = malloc(n + 1);
  if (d) {
    memcpy(d, path, n);
    d[n] = '\0';
  }
  return d;
}

static int load_for_ns(xsd_set_t *s, xsd_index_t *x, const char *const *paths, int npaths, const char *ns, int strict, const char **culprit, char *tns_out,
                       size_t tns_cap) {
  const char **dirs = calloc((size_t)npaths + 1, sizeof *dirs);
  const char **files = calloc((size_t)npaths + 1, sizeof *files);
  char **owned = calloc((size_t)npaths + 1, sizeof *owned);
  xsd_index_t *tmp = x ? NULL : xsd_index_new();
  char found[PATH_MAX];
  const char *file = NULL;
  int nd = 0;
  int nf = 0;
  int no = 0;
  int rc = -2;
  *culprit = NULL;
  if (!dirs) goto out;
  if (!files) goto out;
  if (!owned) goto out;
  x = x ? x : tmp;
  if (!x) goto out;
  for (int i = 0; i < npaths; i++) {
    struct stat sb;
    if (stat(paths[i], &sb)) {
      *culprit = paths[i];
      rc = XSD_LOAD_STAT;
      goto out;
    }
    if (S_ISDIR(sb.st_mode)) {
      dirs[nd++] = paths[i];
      continue;
    }
    files[nf++] = paths[i];
    if (!(owned[no] = dir_of(paths[i]))) goto out;
    dirs[nd++] = owned[no++];
  }
  if ((nf > 1 || (strict && nf)) && !ns) {
    *culprit = files[nf > 1 ? 1 : 0];
    rc = XSD_LOAD_FILES;
    goto out;
  }
  /* one file as is, several by namespace */
  if (nf == 1 && !strict) file = files[0];
  for (int i = 0; (nf > 1 || strict) && i < nf; i++) {
    int oomf = 0;
    const char *t = path_tns(x, files[i], &oomf);
    if (oomf) goto out;
    if (t && !strcmp(t, ns)) {
      if (file) {
        *culprit = files[i];
        rc = XSD_LOAD_FILES;
        goto out;
      }
      file = files[i];
    }
  }
  if (!file && !ns) {
    rc = XSD_LOAD_NONE;
    goto out;
  }
  if (!file) {
    rc = xsd_find_schema(s, x, dirs, nd, ns, found, sizeof found);
    if (rc) goto out;
    file = found;
  }
  rc = xsd_set_load(s, x, file, dirs, nd);
  if (!rc && tns_out) {
    int oomf = 0;
    const char *t = path_tns(x, file, &oomf);
    snprintf(tns_out, tns_cap, "%s", t ? t : "");
    if (oomf) rc = -2;
  }
out:
  for (int i = 0; i < no; i++) free(owned[i]);
  free(owned);
  free(files);
  free(dirs);
  xsd_index_free(tmp);
  return rc;
}

int xsd_load_for_ns(xsd_set_t *s, xsd_index_t *x, const char *const *paths, int npaths, const char *ns, const char **culprit, char *tns_out, size_t tns_cap) {
  return load_for_ns(s, x, paths, npaths, ns, 0, culprit, tns_out, tns_cap);
}

int xsd_load_extra(xsd_set_t *s, xsd_index_t *x, const char *const *paths, int npaths, const char *ns, const char **culprit) {
  return load_for_ns(s, x, paths, npaths, ns, 1, culprit, NULL, 0);
}

int xsd_helper_ns(const char *ns) { return !strcmp(ns, XML_NS) || !strcmp(ns, XSD_NS); }

void xsd_load_report(const char *startup, const char *runtime, int rc, const char *culprit, const char *ns, const xsd_set_t *s) {
  char code[16];
  const char *type = startup;
  int num = 500101;
  switch (rc) {
    case XSD_LOAD_NONE: type = runtime; num = 754; break;
    case XSD_LOAD_SEVERAL: type = runtime; num = 755; break;
    case XSD_LOAD_FILES: num = 752; break;
    case XSD_LOAD_STAT: num = 100; break;
    case -2: type = runtime; break;
    default:
      log_code(xsd_set_error_code(s), "%s", xsd_set_error(s));
      return;
  }
  snprintf(code, sizeof code, "%s%06d", type, num);
  switch (rc) {
    case XSD_LOAD_NONE: log_code(code, "no schema file for namespace %s below the schema directories", ns); break;
    case XSD_LOAD_SEVERAL: log_code(code, "namespace %s found in several schema files, give the schema file", ns); break;
    case XSD_LOAD_FILES: log_code(code, "more than one schema file given: %s", culprit); break;
    case XSD_LOAD_STAT: log_code(code, "cannot open %s", culprit); break;
    default: log_code(code, "out of memory"); break;
  }
}
