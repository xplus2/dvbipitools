/* Copyright 2026 dvbipitools authors. Licensed under GPL-3.0-or-later.
 * See NOTICE and LICENSE for details and authorship information. */

#ifndef LIB_YAMLCFG_H
#define LIB_YAMLCFG_H

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

typedef int (*yamlcfg_item_fn)(void *cfg, const char *list, int begin, char *err, size_t errsz);

typedef struct {
  const char *tool;
  int check;
  int strict;
  unsigned warnings;
  char path[4096];
} yamlcfg_t;

int yamlcfg_load(yamlcfg_t *y, const char *tool, int mode, const char *path, const char *default_path, const yamlcfg_key_t *keys, size_t nkeys, void *cfg);

int yamlcfg_load_items(yamlcfg_t *y, const char *tool, int mode, const char *path, const char *default_path, const yamlcfg_key_t *keys, size_t nkeys, void *cfg, yamlcfg_item_fn item);

void yamlcfg_warn(yamlcfg_t *y, const char *fmt, ...) __attribute__((format(printf, 2, 3)));

int yamlcfg_report(const yamlcfg_t *y);

int yamlcfg_parse_bool(const char *val, int *out);

int yamlcfg_set_str(const char **dst, const char *val, char *err, size_t errsz);
int yamlcfg_set_bool(int *dst, const char *val, char *err, size_t errsz);
int yamlcfg_set_uint(unsigned *dst, const char *val, unsigned min, unsigned max, char *err, size_t errsz);
int yamlcfg_set_double(double *dst, const char *val, double min, double max, int min_excl, char *err, size_t errsz);
int yamlcfg_set_lang(char *dst, const char *val, char *err, size_t errsz);
int yamlcfg_set_addrport(int *family, char *addr, size_t addrsz, unsigned *port, const char *val, char *err, size_t errsz);
int yamlcfg_set_color(int *dst, const char *val, char *err, size_t errsz);

#endif
