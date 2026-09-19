/* Copyright 2026 dvbipitools authors. Licensed under GPL-3.0-or-later.
 * See NOTICE and LICENSE for details and authorship information. */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#include "lib/config/yamlcfg.h"

typedef struct {
  const char *str;
  const char *list[4];
  unsigned nlist;
  int flag;
  unsigned num;
} fcfg_t;

static int ap_str(void *c, const char *v, char *e, size_t n) {
  return yamlcfg_set_str(&((fcfg_t *)c)->str, v, e, n);
}

static int ap_bool(void *c, const char *v, char *e, size_t n) {
  return yamlcfg_set_bool(&((fcfg_t *)c)->flag, v, e, n);
}

static int ap_num(void *c, const char *v, char *e, size_t n) {
  return yamlcfg_set_uint(&((fcfg_t *)c)->num, v, 1, 100, e, n);
}

static int ap_list(void *c, const char *v, char *e, size_t n) {
  fcfg_t *t = c;
  if (t->nlist >= sizeof t->list / sizeof t->list[0]) t->nlist = 0;
  return yamlcfg_set_str(&t->list[t->nlist++], v, e, n);
}

static int item_cb(void *c, const char *list, int begin, char *e, size_t n) {
  (void)c;
  (void)list;
  (void)begin;
  (void)e;
  (void)n;
  return 0;
}

static const yamlcfg_key_t KEYS[] = {
  {"name", ap_str, 0, 0},
  {"on", ap_bool, 0, 0},
  {"count", ap_num, 0, 0},
  {"net.port", ap_num, 0, 0},
  {"file", ap_str, 1, 0},
  {"list", ap_list, 0, 1},
  {"items", ap_list, 0, YAMLCFG_LIST_KEYED},
  {"items.opt", ap_num, 0, 0},
};

static void poke_setters(const char *s) {
  char err[64];
  char addr[32];
  char lang[4];
  const char *str;
  int i;
  int fam;
  unsigned u;
  unsigned port;
  double d;
  yamlcfg_parse_bool(s, &i);
  yamlcfg_set_bool(&i, s, err, sizeof err);
  yamlcfg_set_uint(&u, s, 0, 65535, err, sizeof err);
  yamlcfg_set_double(&d, s, 0, 1e6, 1, err, sizeof err);
  yamlcfg_set_lang(lang, s, err, sizeof err);
  yamlcfg_set_addrport(&fam, addr, sizeof addr, &port, s, err, sizeof err);
  yamlcfg_set_color(&i, s, err, sizeof err);
  if (*s) yamlcfg_set_str(&str, s, err, sizeof err);
}

int main(int argc, char **argv) {
  FILE *f;
  unsigned char *buf;
  long len;
  size_t n;
  char path[] = "/tmp/fuzz_yamlcfg_XXXXXX";
  int fd;
  fcfg_t cfg;
  yamlcfg_t y;

  if (argc != 2) {
    fprintf(stderr, "usage: %s <input-file>\n", argv[0]);
    return 1;
  }
  f = fopen(argv[1], "rb");
  if (!f)
    return 1;
  if (fseek(f, 0, SEEK_END)) {
    fclose(f);
    return 1;
  }
  len = ftell(f);
  if (len < 1 || fseek(f, 0, SEEK_SET)) {
    fclose(f);
    return 1;
  }
  buf = malloc((size_t)len + 1);
  if (!buf) {
    fclose(f);
    return 1;
  }
  n = fread(buf, 1, (size_t)len, f);
  fclose(f);
  if (n < 1) {
    free(buf);
    return 1;
  }
  buf[n] = '\0';
  fd = mkstemp(path);
  if (fd < 0) {
    free(buf);
    return 1;
  }
  if (write(fd, buf + 1, n - 1) != (ssize_t)(n - 1)) {
    close(fd);
    unlink(path);
    free(buf);
    return 1;
  }
  close(fd);

  memset(&cfg, 0, sizeof cfg);
  yamlcfg_load_items(&y, "fuzz", buf[0] & (YAMLCFG_CHECK | YAMLCFG_STRICT), path, NULL, KEYS, sizeof KEYS / sizeof KEYS[0], &cfg, item_cb);
  yamlcfg_report(&y);
  unlink(path);
  poke_setters((const char *)buf + 1);
  free(buf);
  return 0;
}
