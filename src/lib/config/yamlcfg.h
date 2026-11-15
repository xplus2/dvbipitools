/* Copyright 2026 dvbipitools authors. Licensed under GPL-3.0-or-later.
 * See NOTICE and LICENSE for details and authorship information. */

#ifndef DVBIPITOOLS_LIB_CONFIG_YAMLCFG_H
#define DVBIPITOOLS_LIB_CONFIG_YAMLCFG_H

#include <stddef.h>

#define YAMLCFG_LOADED 0
#define YAMLCFG_ABSENT 1
#define YAMLCFG_ERROR (-1)

#define YAMLCFG_LIST_KEYED 2

#define YAMLCFG_CHECK 1
#define YAMLCFG_STRICT 2

typedef struct {
  const char *key;
  int (*apply)(void *cfg, const char *val, char *err, size_t errsz);
  int must_exist;
  int list;
} yamlcfg_key_t;

typedef struct {
  int opt;
  const char *key;
  const char *code;
  const char *fmt;
  int fmt_err;
} yamlcfg_opt_t;

/* 1 applied, -1 bad value (reported), 0 opt not in opts or key unknown */
int yamlcfg_opt_apply(const char *tool, const yamlcfg_opt_t *opts, size_t nopts, const yamlcfg_key_t *keys, size_t nkeys, void *cfg, int opt, const char *val);

typedef int (*yamlcfg_item_fn)(void *cfg, const char *list, int begin, char *err, size_t errsz);

typedef struct {
  const char *tool;
  int check;
  int strict;
  unsigned warnings;
  unsigned errors;
  char path[4096];
} yamlcfg_t;

int yamlcfg_load(yamlcfg_t *y, const char *tool, int mode, const char *path, const char *default_path, const yamlcfg_key_t *keys, size_t nkeys, void *cfg);

int yamlcfg_load_items(yamlcfg_t *y, const char *tool, int mode, const char *path, const char *default_path, const yamlcfg_key_t *keys, size_t nkeys, void *cfg, yamlcfg_item_fn item);

void yamlcfg_warn(yamlcfg_t *y, const char *code, const char *fmt, ...) __attribute__((format(printf, 3, 4)));

int yamlcfg_report(const yamlcfg_t *y);

int yamlcfg_parse_bool(const char *val, int *out);

int yamlcfg_set_str(void **pool, const char **dst, const char *val, char *err, size_t errsz);
/* "invalid '<val>' (<want>)" into err. always -1 */
int yamlcfg_bad(char *err, size_t errsz, const char *val, const char *want);
int yamlcfg_set_buf(char *dst, size_t sz, const char *val, char *err, size_t errsz);
/* copy of val appended to list[*n], max entries. 0 ok, -1 error (err) */
int yamlcfg_add_str(void **pool, const char **list, int *n, int max, const char *val, char *err, size_t errsz);
void yamlcfg_strpool_free(void *pool);
int yamlcfg_set_bool(int *dst, const char *val, char *err, size_t errsz);
int yamlcfg_set_uint(unsigned *dst, const char *val, unsigned min, unsigned max, char *err, size_t errsz);
int yamlcfg_set_double(double *dst, const char *val, double min, double max, int min_excl, char *err, size_t errsz);
int yamlcfg_set_lang(char *dst, const char *val, char *err, size_t errsz);
int yamlcfg_set_addrport(int *family, char *addr, size_t addrsz, unsigned *port, const char *val, char *err, size_t errsz);
int yamlcfg_set_color(int *dst, const char *val, char *err, size_t errsz);
int yamlcfg_set_long(long *dst, const char *val, unsigned min, char *err, size_t errsz);
int yamlcfg_set_u8(unsigned char *dst, const char *val, unsigned max, char *err, size_t errsz);
int yamlcfg_set_int_by(int *dst, const char *val, int (*parse)(const char *s, int *out), const char *hint, char *err, size_t errsz);
void yamlcfg_warn_if(yamlcfg_t *y, int cond, const char *code, const char *msg);

#endif
