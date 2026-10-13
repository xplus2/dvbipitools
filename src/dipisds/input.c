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
#include "input.h"

static const char *suffix(const char *path) {
  const char *dot = strrchr(path, '.');
  const char *slash = strrchr(path, '/');
  if (!dot || (slash && dot < slash)) return "";
  return dot;
}

static int read_whole_file(const char *path, unsigned char **out, size_t *out_len) {
  FILE *f = fopen(path, "rb");
  long sz;
  unsigned char *buf;
  if (!f) return -1;
  if (fseek(f, 0, SEEK_END)) {
    fclose(f);
    return -1;
  }
  sz = ftell(f);
  if (sz < 0) {
    fclose(f);
    return -1;
  }
  rewind(f);
  buf = malloc((size_t)sz + 1);
  if (!buf) {
    fclose(f);
    return -1;
  }
  if (sz > 0 && fread(buf, 1, (size_t)sz, f) != (size_t)sz) {
    fclose(f);
    free(buf);
    return -1;
  }
  buf[sz] = '\0';
  fclose(f);
  *out = buf;
  *out_len = (size_t)sz;
  return 0;
}

/* rtp://[[src]@]<addr>:<port> or udp://..., [addr6] for v6. fills address/source/family/port */
static int parse_mcast_uri(const char *uri, sds_service_t *s) {
  const char *p;
  const char *at;
  char addr[SDS_MAX_ADDR];
  size_t alen;
  char *end;
  unsigned long v;
  if (!strncmp(uri, "rtp://", 6)) s->rtp = 1;
  else if (!strncmp(uri, "udp://", 6)) s->rtp = 0;
  else return -1;
  p = uri + 6;
  at = strchr(p, '@');
  if (at) {
    size_t slen = (size_t)(at - p);
    if (slen >= sizeof s->source) return -1;
    if (slen > 2 && p[0] == '[' && at[-1] == ']') {
      p++;
      slen -= 2;
    }
    memcpy(s->source, p, slen);
    s->source[slen] = '\0';
    p = at + 1;
  }
  if (*p == '[') {
    const char *close = strchr(p, ']');
    if (!close) return -1;
    alen = (size_t)(close - (p + 1));
    if (alen == 0 || alen >= sizeof addr) return -1;
    memcpy(addr, p + 1, alen);
    addr[alen] = '\0';
    if (close[1] != ':') return -1;
    p = close + 2;
    s->family = AF_INET6;
  } else {
    const char *colon = strchr(p, ':');
    if (!colon) return -1;
    alen = (size_t)(colon - p);
    if (alen == 0 || alen >= sizeof addr) return -1;
    memcpy(addr, p, alen);
    addr[alen] = '\0';
    p = colon + 1;
    s->family = AF_INET;
  }
  v = strtoul(p, &end, 10);
  if (*end != '\0' || v == 0 || v > 65535) return -1;
  s->port = (unsigned)v;
  {
    unsigned char b[16];
    if (inet_pton(s->family, addr, b) != 1) return -1;
    if (s->family == AF_INET ? (b[0] & 0xF0) != 0xE0 : b[0] != 0xFF) return -1;
  }
  bufcpy(s->address, sizeof s->address, addr);
  if (s->source[0]) {
    unsigned char b[16];
    if (inet_pton(s->family, s->source, b) != 1) return -1;
  }
  return 0;
}

static int dvb_id(const char *name, const char *v, unsigned *out) {
  unsigned long n;
  if (parse_ulong(v, 65535, &n)) {
    log_code("AE400215", "bad %s (0..65535): %s", name, v);
    return -1;
  }
  *out = (unsigned)n;
  return 0;
}

static int load_csv(FILE *f, input_t *in) {
  char line[512];
  int idx = 0, lineno = 0, rc;

  while ((rc = read_line(f, line, sizeof line)) != 0) {
    char *fields[5];
    size_t nf;
    sds_service_t *s;
    lineno++;
    if (rc < 0) {
      log_code("AE400205", "line %d: too long (max %zu)", lineno, sizeof line - 1);
      return -1;
    }
    chomp(line);
    if (!line[0]) continue;
    nf = csv_split(line, fields, 5);
    if (nf < 2) {
      log_code("AE000224", "line %d: expected name,uri", lineno);
      return -1;
    }
    if (idx >= SDS_MAX_SERVICES) {
      log_code("AE000225", "too many entries (max %d)", SDS_MAX_SERVICES);
      return -1;
    }
    s = &in->services[idx];
    memset(s, 0, sizeof *s);
    bufcpy(s->name, sizeof s->name, fields[0]);
    if (parse_mcast_uri(fields[1], s)) {
      log_code("AE400110", "line %d: bad uri: %s", lineno, fields[1]);
      return -1;
    }
    s->tsid = 1;
    s->onid = 1;
    s->sid = (unsigned)(idx + 1);
    if ((nf > 2 && dvb_id("tsid", fields[2], &s->tsid)) || (nf > 3 && dvb_id("onid", fields[3], &s->onid)) ||
        (nf > 4 && dvb_id("sid", fields[4], &s->sid))) {
      log_code("AE400216", "line %d: bad service id field", lineno);
      return -1;
    }
    idx++;
  }
  in->service_count = idx;
  return 0;
}

static char *extinf_comma(char *line) {
  int quoted = 0;
  for (char *p = line; *p; p++) {
    if (*p == '"') quoted = !quoted;
    else if (*p == ',' && !quoted) return p;
  }
  return NULL;
}

static int load_m3u(FILE *f, input_t *in) {
  char line[2048];
  char pending_name[SDS_MAX_NAME];
  unsigned pending_tsid = 1, pending_onid = 1, pending_sid = 0;
  int have_pending = 0, idx = 0, lineno = 0, rc;

  while ((rc = read_line(f, line, sizeof line)) != 0) {
    sds_service_t *s;
    lineno++;
    if (rc < 0) {
      log_code("AE400205", "line %d: too long (max %zu)", lineno, sizeof line - 1);
      return -1;
    }
    chomp(line);
    if (!line[0]) continue;
    if (!strncmp(line, "#EXTINF:", 8)) {
      char *comma = extinf_comma(line);
      char tmp[32];
      if (!comma) {
        log_code("AE400111", "malformed #EXTINF line: %s", line);
        return -1;
      }
      bufcpy(pending_name, sizeof pending_name, comma + 1);
      pending_tsid = pending_onid = 1;
      pending_sid = 0;
      if ((xml_attr_list(line, comma, "tsid", tmp, sizeof tmp) == 0 && dvb_id("tsid", tmp, &pending_tsid)) ||
          (xml_attr_list(line, comma, "onid", tmp, sizeof tmp) == 0 && dvb_id("onid", tmp, &pending_onid)) ||
          (xml_attr_list(line, comma, "sid", tmp, sizeof tmp) == 0 && dvb_id("sid", tmp, &pending_sid))) {
        log_code("AE400216", "line %d: bad service id field", lineno);
        return -1;
      }
      have_pending = 1;
      continue;
    }
    if (line[0] == '#' || !have_pending) continue;
    if (idx >= SDS_MAX_SERVICES) {
      log_code("AE000225", "too many entries (max %d)", SDS_MAX_SERVICES);
      return -1;
    }
    s = &in->services[idx];
    memset(s, 0, sizeof *s);
    bufcpy(s->name, sizeof s->name, pending_name);
    if (parse_mcast_uri(line, s)) {
      log_code("AE400112", "bad uri: %s", line);
      return -1;
    }
    s->tsid = pending_tsid;
    s->onid = pending_onid;
    s->sid = pending_sid ? pending_sid : (unsigned)(idx + 1);
    idx++;
    have_pending = 0;
  }
  in->service_count = idx;
  return 0;
}

typedef struct {
  input_t *in;
  int idx;
} xspf_ctx_t;

static int xspf_track_cb(const char *tag, const char *end, void *vctx) {
  xspf_ctx_t *c = vctx;
  input_t *in = c->in;
  char loc[2 * SDS_MAX_ADDR + 16];
  char title[SDS_MAX_NAME];
  char tmp[32];
  sds_service_t *s;

  if (c->idx >= SDS_MAX_SERVICES) {
    log_code("AE000225", "too many entries (max %d)", SDS_MAX_SERVICES);
    return -1;
  }
  if (xml_elem_text(tag, end, "location", loc, sizeof loc)) {
    log_code("AE000226", "xspf track missing <location>");
    return -1;
  }
  if (xml_elem_text(tag, end, "title", title, sizeof title)) title[0] = '\0';

  s = &in->services[c->idx];
  memset(s, 0, sizeof *s);
  bufcpy(s->name, sizeof s->name, title);
  if (parse_mcast_uri(loc, s)) {
    log_code("AE400112", "bad uri: %s", loc);
    return -1;
  }
  s->tsid = 1;
  s->onid = 1;
  s->sid = (unsigned)(c->idx + 1);
  if ((xml_attr(tag, end, "tsid", tmp, sizeof tmp) == 0 && dvb_id("tsid", tmp, &s->tsid)) ||
      (xml_attr(tag, end, "onid", tmp, sizeof tmp) == 0 && dvb_id("onid", tmp, &s->onid)) ||
      (xml_attr(tag, end, "sid", tmp, sizeof tmp) == 0 && dvb_id("sid", tmp, &s->sid))) {
    log_code("AE400217", "xspf track %d: bad service id field", c->idx + 1);
    return -1;
  }
  c->idx++;
  return 0;
}

static int load_xspf(input_t *in, const unsigned char *buf) {
  const char *p = (const char *)buf;
  xspf_ctx_t c = {in, 0};
  if (for_each_xml_elem(p, p + strlen(p), "track", xspf_track_cb, &c)) return -1;
  in->service_count = c.idx;
  return 0;
}

static int count_cb(const char *tag, const char *blk_end, void *ctx) {
  (void)tag;
  (void)blk_end;
  ++*(int *)ctx;
  return 0;
}

static int count_xml_elems(const char *buf, size_t len, const char *name) {
  int n = 0;
  for_each_xml_elem(buf, buf + len, name, count_cb, &n);
  return n;
}

int input_load(const char *path, input_t *in) {
  const char *sfx = suffix(path);
  memset(in, 0, sizeof *in);
  if (!strcasecmp(sfx, ".xml")) {
    unsigned char *buf;
    size_t len;
    static const struct { const char *tag; unsigned payload_id; } root_tags[] = {
      {"BroadcastDiscovery", DVBSTP_PAYLOAD_BROADCAST_DISCOVERY},
      {"ServiceProviderDiscovery", DVBSTP_PAYLOAD_SP_DISCOVERY},
      {"PackageDiscovery", DVBSTP_PAYLOAD_PACKAGE_DISCOVERY},
      {"RegionalisationDiscovery", DVBSTP_PAYLOAD_REGIONALISATION_DISCOVERY},
      {"RMSFUSDiscovery", DVBSTP_PAYLOAD_RMSFUS_DISCOVERY},
    };
    size_t i;
    if (read_whole_file(path, &buf, &len)) {
      log_code("AE000227", "cannot read %s", path);
      return -1;
    }
    if (len > DVBSTP_MAX_SEGMENT) {
      log_code("AE000695", "%s: %zu bytes, max %u", path, len, (unsigned)DVBSTP_MAX_SEGMENT);
      free(buf);
      return -1;
    }
    in->raw_version = -1;
    for (i = 0; i < sizeof root_tags / sizeof root_tags[0]; i++) {
      const char *root = xml_find_start((char *)buf, (char *)buf + len, root_tags[i].tag);
      if (root) {
        char ver[8];
        char *e;
        unsigned long v;
        in->raw_payload_id = root_tags[i].payload_id;
        if (xml_tag_attr(root, (char *)buf + len, "Version", ver, sizeof ver) == 0 && ver[0]) {
          v = strtoul(ver, &e, 10);
          if (!*e && v <= 255) in->raw_version = (int)v;
        }
        break;
      }
    }
    if (i == sizeof root_tags / sizeof root_tags[0]) {
      log_code("AE400113", "%s: no recognized SD&S root element", path);
      free(buf);
      return -1;
    }
    in->kind = INPUT_RAW_XML;
    in->raw_xml = buf;
    in->raw_xml_len = len;
    in->service_count = count_xml_elems((char *)buf, len, "SingleService");
    in->raw_provider_count = count_xml_elems((char *)buf, len, "ServiceProvider");
    return 0;
  }

  in->kind = INPUT_SERVICES;
  if (!strcasecmp(sfx, ".xspf")) {
    unsigned char *buf;
    size_t len;
    int rc;
    if (read_whole_file(path, &buf, &len)) {
      log_code("AE000227", "cannot read %s", path);
      return -1;
    }
    rc = load_xspf(in, buf);
    free(buf);
    return rc;
  }

  {
    FILE *f = fopen(path, "r");
    int rc;
    if (!f) {
      log_code("AE000100", "cannot open %s", path);
      return -1;
    }
    if (!strcasecmp(sfx, ".csv")) rc = load_csv(f, in);
    else if (!strcasecmp(sfx, ".m3u") || !strcasecmp(sfx, ".m3u8")) rc = load_m3u(f, in);
    else {
      log_code("AE400114", "%s: unrecognized suffix, expected .csv/.m3u/.m3u8/.xspf/.xml", path);
      rc = -1;
    }
    fclose(f);
    return rc;
  }
}

void input_free(input_t *in) {
  free(in->raw_xml);
  in->raw_xml = NULL;
}

int input_load_packages(const char *path, sds_package_t *out, int max, int *count) {
  FILE *f = fopen(path, "r");
  char line[9216];
  int idx = 0, lineno = 0, rc;

  if (!f) {
    log_code("AE000100", "cannot open %s", path);
    return -1;
  }
  while ((rc = read_line(f, line, sizeof line)) != 0) {
    char *fields[5];
    size_t nf;
    char *svc, *svc_save;
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
  int idx = 0, lineno = 0, rc;

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
