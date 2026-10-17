/* Copyright 2026 dvbipitools authors. Licensed under GPL-3.0-or-later.
 * See NOTICE and LICENSE for details and authorship information. */

#include <arpa/inet.h>
#include <limits.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>

#include "lib/sys/ioutil.h"
#include "lib/helper/log.h"
#include "lib/net/dvbstp.h"
#include "lib/helper/xml_util.h"
#include "dipisds/input.h"
#include "dipisds/input/input_int.h"

int input_load_packages(const char *path, sds_package_t *out, int max, int *count) {
  FILE *f = fopen(path, "r");
  char line[9216];
  int idx = 0;
  int lineno = 0;
  int rc;

  if (!f) {
    log_code("AE000100", "cannot open %s", path);
    return -1;
  }
  while ((rc = read_line(f, line, sizeof line)) != 0) {
    char *fields[5];
    size_t nf;
    const char *svc;
    char *svc_save;
    unsigned long pid;
    sds_package_t *pkg;
    lineno++;
    if (rc < 0) {
      log_code("AE400205", "%s: line %d: too long (max %zu)", path, lineno, sizeof line - 1);
      goto fail;
    }
    chomp(line);
    if (!line[0]) continue;
    nf = csv_split(line, fields, 5);
    if (nf < 5) {
      log_code("AE400115", "%s: line %d: expected id,name,lang,visible,svc1|svc2|...", path, lineno);
      goto fail;
    }
    if (idx >= max) {
      log_code("AE000228", "%s: too many packages (max %d)", path, max);
      goto fail;
    }
    pkg = &out[idx];
    memset(pkg, 0, sizeof *pkg);
    if (parse_ulong(fields[0], UINT_MAX, &pid)) {
      log_code("AE400116", "%s: line %d: bad package id: %s", path, lineno, fields[0]);
      goto fail;
    }
    pkg->id = (unsigned)pid;
    bufcpy(pkg->name, sizeof pkg->name, fields[1]);
    if (strlen(fields[2]) != 3) {
      log_code("AE400117", "%s: line %d: bad lang: %s", path, lineno, fields[2]);
      goto fail;
    }
    memcpy(pkg->lang, fields[2], 3);
    if (fields[3][0] != '\0' && strcmp(fields[3], "0") && strcmp(fields[3], "1")) {
      log_code("AE400210", "%s: line %d: bad visible flag, expected 0 or 1: %s", path, lineno, fields[3]);
      goto fail;
    }
    pkg->visible = fields[3][0] != '0';
    for (int i = 0; i < idx; i++) {
      if (out[i].id == pkg->id) {
        log_code("AE400211", "%s: line %d: duplicate package id: %u", path, lineno, pkg->id);
        goto fail;
      }
    }
    svc = strtok_r(fields[4], "|", &svc_save);
    while (svc) {
      if (pkg->service_count >= SDS_MAX_PKG_SERVICES) {
        log_code("AE000229", "%s: line %d: too many services in package (max %d)", path, lineno, SDS_MAX_PKG_SERVICES);
        goto fail;
      }
      bufcpy(pkg->service_names[pkg->service_count], sizeof pkg->service_names[0], svc);
      pkg->service_count++;
      svc = strtok_r(NULL, "|", &svc_save);
    }
    if (pkg->service_count == 0) {
      log_code("AE400118", "%s: line %d: package has no services", path, lineno);
      goto fail;
    }
    idx++;
  }
  *count = idx;
  fclose(f);
  return 0;

fail:
  fclose(f);
  return -1;
}

int input_load_cells(const char *path, sds_cell_t *out, int max, int *count) {
  FILE *f = fopen(path, "r");
  char line[512];
  int idx = 0;
  int lineno = 0;
  int rc;

  if (!f) {
    log_code("AE000100", "cannot open %s", path);
    return -1;
  }
  while ((rc = read_line(f, line, sizeof line)) != 0) {
    char *fields[3 + SDS_MAX_CA_DEPTH];
    size_t nf;
    sds_cell_t *cell;
    lineno++;
    if (rc < 0) {
      log_code("AE400205", "%s: line %d: too long (max %zu)", path, lineno, sizeof line - 1);
      goto fail;
    }
    chomp(line);
    if (!line[0]) continue;
    if (idx >= max) {
      log_code("AE000230", "%s: too many cells (max %d)", path, max);
      goto fail;
    }
    cell = &out[idx];
    memset(cell, 0, sizeof *cell);
    nf = csv_split(line, fields, sizeof fields / sizeof fields[0]);
    if (nf < 2 || !fields[0][0]) {
      log_code("AE400119", "%s: line %d: expected id,country,type:value,...", path, lineno);
      goto fail;
    }
    bufcpy(cell->id, sizeof cell->id, fields[0]);
    if (strlen(fields[1]) != 2) {
      log_code("AE400120", "%s: line %d: bad country code: %s", path, lineno, fields[1]);
      goto fail;
    }
    bufcpy(cell->country, sizeof cell->country, fields[1]);
    if (nf - 2 > SDS_MAX_CA_DEPTH) {
      log_code("AE000231", "%s: line %d: too many CA entries (max %d)", path, lineno, SDS_MAX_CA_DEPTH);
      goto fail;
    }
    for (size_t k = 2; k < nf; k++) {
      char *colon = strchr(fields[k], ':');
      unsigned long type;
      if (!colon) {
        log_code("AE400121", "%s: line %d: bad CA entry: %s", path, lineno, fields[k]);
        goto fail;
      }
      *colon = '\0';
      if (parse_ulong(fields[k], 255, &type)) {
        log_code("AE400122", "%s: line %d: bad CA type (0..255): %s", path, lineno, fields[k]);
        goto fail;
      }
      cell->ca[cell->ca_depth].type = (unsigned)type;
      bufcpy(cell->ca[cell->ca_depth].value, sizeof cell->ca[cell->ca_depth].value, colon + 1);
      cell->ca_depth++;
    }
    if (cell->ca_depth == 0) {
      log_code("AE400123", "%s: line %d: cell has no CA entries", path, lineno);
      goto fail;
    }
    idx++;
  }
  *count = idx;
  fclose(f);
  return 0;

fail:
  fclose(f);
  return -1;
}
