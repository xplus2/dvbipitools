/* Copyright 2026 dvbipitools authors. Licensed under GPL-3.0-or-later.
 * See NOTICE and LICENSE for details and authorship information. */

#include <arpa/inet.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "lib/ioutil.h"
#include "lib/net/dvbstp.h"
#include "lib/xml_util.h"
#include "input.h"
#include "version.h"

static const char *suffix(const char *path) {
  const char *dot = strrchr(path, '.');
  const char *slash = strrchr(path, '/');
  if (!dot || (slash && dot < slash))
    return "";
  return dot;
}

static int read_whole_file(const char *path, unsigned char **out, size_t *out_len) {
  FILE *f = fopen(path, "rb");
  long sz;
  unsigned char *buf;
  if (!f)
    return -1;
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

/* rtp://[@]<addr>:<port> or udp://..., [addr6] for v6. fills address/family/port */
static int parse_mcast_uri(const char *uri, sds_service_t *s) {
  const char *p;
  char addr[SDS_MAX_ADDR];
  size_t alen;

  if (!strncmp(uri, "rtp://", 6))
    s->rtp = 1;
  else if (!strncmp(uri, "udp://", 6))
    s->rtp = 0;
  else
    return -1;
  p = uri + 6;
  if (*p == '@')
    p++;
  if (*p == '[') {
    const char *close = strchr(p, ']');
    if (!close)
      return -1;
    alen = (size_t)(close - (p + 1));
    if (alen == 0 || alen >= sizeof addr)
      return -1;
    memcpy(addr, p + 1, alen);
    addr[alen] = '\0';
    if (close[1] != ':')
      return -1;
    p = close + 2;
    s->family = AF_INET6;
  } else {
    const char *colon = strchr(p, ':');
    if (!colon)
      return -1;
    alen = (size_t)(colon - p);
    if (alen == 0 || alen >= sizeof addr)
      return -1;
    memcpy(addr, p, alen);
    addr[alen] = '\0';
    p = colon + 1;
    s->family = AF_INET;
  }
  {
    char *end;
    unsigned long v = strtoul(p, &end, 10);
    if (*end != '\0' || v == 0 || v > 65535)
      return -1;
    s->port = (unsigned)v;
  }
  bufcpy(s->address, sizeof s->address, addr);
  return 0;
}

static int load_csv(FILE *f, input_t *in) {
  char line[512];
  int idx = 0, lineno = 0;

  while (fgets(line, sizeof line, f)) {
    char *fields[5];
    size_t nf;
    sds_service_t *s;
    lineno++;
    chomp(line);
    if (!line[0])
      continue;
    nf = csv_split(line, fields, 5);
    if (nf < 2) {
      fprintf(stderr, TOOL_NAME ": line %d: expected name,uri\n", lineno);
      return -1;
    }
    if (idx >= SDS_MAX_SERVICES) {
      fprintf(stderr, TOOL_NAME ": too many entries (max %d)\n", SDS_MAX_SERVICES);
      return -1;
    }
    s = &in->services[idx];
    memset(s, 0, sizeof *s);
    bufcpy(s->name, sizeof s->name, fields[0]);
    if (parse_mcast_uri(fields[1], s)) {
      fprintf(stderr, TOOL_NAME ": line %d: bad uri: %s\n", lineno, fields[1]);
      return -1;
    }
    s->tsid = nf > 2 ? (unsigned)strtoul(fields[2], NULL, 10) : 1;
    s->onid = nf > 3 ? (unsigned)strtoul(fields[3], NULL, 10) : 1;
    s->sid = nf > 4 ? (unsigned)strtoul(fields[4], NULL, 10) : (unsigned)(idx + 1);
    idx++;
  }
  in->service_count = idx;
  return 0;
}

static int load_m3u(FILE *f, input_t *in) {
  char line[512];
  char pending_name[SDS_MAX_NAME];
  unsigned pending_tsid = 1, pending_onid = 1, pending_sid = 0;
  int have_pending = 0, idx = 0;

  while (fgets(line, sizeof line, f)) {
    chomp(line);
    if (!line[0])
      continue;
    if (!strncmp(line, "#EXTINF:", 8)) {
      char *comma = strrchr(line, ',');
      char tmp[32];
      if (!comma) {
        fprintf(stderr, TOOL_NAME ": malformed #EXTINF line: %s\n", line);
        return -1;
      }
      bufcpy(pending_name, sizeof pending_name, comma + 1);
      pending_tsid = pending_onid = 1;
      pending_sid = 0;
      if (xml_attr(line, comma, "tsid", tmp, sizeof tmp) == 0)
        pending_tsid = (unsigned)strtoul(tmp, NULL, 10);
      if (xml_attr(line, comma, "onid", tmp, sizeof tmp) == 0)
        pending_onid = (unsigned)strtoul(tmp, NULL, 10);
      if (xml_attr(line, comma, "sid", tmp, sizeof tmp) == 0)
        pending_sid = (unsigned)strtoul(tmp, NULL, 10);
      have_pending = 1;
      continue;
    }
    if (line[0] == '#' || !have_pending)
      continue;
    if (idx >= SDS_MAX_SERVICES) {
      fprintf(stderr, TOOL_NAME ": too many entries (max %d)\n", SDS_MAX_SERVICES);
      return -1;
    }
    {
      sds_service_t *s = &in->services[idx];
      memset(s, 0, sizeof *s);
      bufcpy(s->name, sizeof s->name, pending_name);
      if (parse_mcast_uri(line, s)) {
        fprintf(stderr, TOOL_NAME ": bad uri: %s\n", line);
        return -1;
      }
      s->tsid = pending_tsid;
      s->onid = pending_onid;
      s->sid = pending_sid ? pending_sid : (unsigned)(idx + 1);
      idx++;
    }
    have_pending = 0;
  }
  in->service_count = idx;
  return 0;
}

/* copies [tb,te) into title, truncating to fit */
static void copy_title_field(char *title, size_t title_cap, const char *tb, const char *te) {
  size_t n = (size_t)(te - tb);
  if (n >= title_cap)
    n = title_cap - 1;
  memcpy(title, tb, n);
  title[n] = '\0';
}

static int load_xspf(input_t *in, const unsigned char *buf) {
  const char *p = (const char *)buf;
  int idx = 0;

  for (;;) {
    const char *tag = strstr(p, "<track");
    const char *end, *lb, *le, *tb;
    char loc[SDS_MAX_ADDR + 16], title[SDS_MAX_NAME], tmp[32];
    sds_service_t *s;

    if (!tag)
      break;
    end = strstr(tag, "</track>");
    if (!end)
      break;
    if (idx >= SDS_MAX_SERVICES) {
      fprintf(stderr, TOOL_NAME ": too many entries (max %d)\n", SDS_MAX_SERVICES);
      return -1;
    }

    lb = strstr(tag, "<location>");
    if (!lb || lb >= end) {
      fprintf(stderr, TOOL_NAME ": xspf track missing <location>\n");
      return -1;
    }
    lb += 10;
    le = strstr(lb, "</location>");
    if (!le || le > end) {
      fprintf(stderr, TOOL_NAME ": xspf track missing </location>\n");
      return -1;
    }
    {
      size_t n = (size_t)(le - lb);
      if (n >= sizeof loc)
        n = sizeof loc - 1;
      memcpy(loc, lb, n);
      loc[n] = '\0';
    }

    title[0] = '\0';
    tb = strstr(tag, "<title>");
    if (tb && tb < end) {
      const char *te;
      tb += 7;
      te = strstr(tb, "</title>");
      if (te && te <= end)
        copy_title_field(title, sizeof title, tb, te);
    }

    s = &in->services[idx];
    memset(s, 0, sizeof *s);
    bufcpy(s->name, sizeof s->name, title);
    if (parse_mcast_uri(loc, s)) {
      fprintf(stderr, TOOL_NAME ": bad uri: %s\n", loc);
      return -1;
    }
    s->tsid = xml_attr(tag, end, "tsid", tmp, sizeof tmp) == 0 ? (unsigned)strtoul(tmp, NULL, 10) : 1;
    s->onid = xml_attr(tag, end, "onid", tmp, sizeof tmp) == 0 ? (unsigned)strtoul(tmp, NULL, 10) : 1;
    s->sid = xml_attr(tag, end, "sid", tmp, sizeof tmp) == 0 ? (unsigned)strtoul(tmp, NULL, 10) : (unsigned)(idx + 1);
    idx++;
    p = end + 8;
  }
  in->service_count = idx;
  return 0;
}

int input_load(const char *path, input_t *in) {
  const char *sfx = suffix(path);
  memset(in, 0, sizeof *in);

  if (!strcmp(sfx, ".xml")) {
    unsigned char *buf;
    size_t len;
    if (read_whole_file(path, &buf, &len)) {
      fprintf(stderr, TOOL_NAME ": cannot read %s\n", path);
      return -1;
    }
    if (strstr((char *)buf, "<BroadcastDiscovery"))
      in->raw_payload_id = DVBSTP_PAYLOAD_BROADCAST_DISCOVERY;
    else if (strstr((char *)buf, "<ServiceProviderDiscovery"))
      in->raw_payload_id = DVBSTP_PAYLOAD_SP_DISCOVERY;
    else if (strstr((char *)buf, "<PackageDiscovery"))
      in->raw_payload_id = DVBSTP_PAYLOAD_PACKAGE_DISCOVERY;
    else if (strstr((char *)buf, "<RegionalisationDiscovery"))
      in->raw_payload_id = DVBSTP_PAYLOAD_REGIONALISATION_DISCOVERY;
    else if (strstr((char *)buf, "<RMSFUSDiscovery"))
      in->raw_payload_id = DVBSTP_PAYLOAD_RMSFUS_DISCOVERY;
    else {
      fprintf(stderr, TOOL_NAME ": %s: no recognized SD&S root element\n", path);
      free(buf);
      return -1;
    }
    in->kind = INPUT_RAW_XML;
    in->raw_xml = buf;
    in->raw_xml_len = len;
    return 0;
  }

  in->kind = INPUT_SERVICES;
  if (!strcmp(sfx, ".xspf")) {
    unsigned char *buf;
    size_t len;
    int rc;
    if (read_whole_file(path, &buf, &len)) {
      fprintf(stderr, TOOL_NAME ": cannot read %s\n", path);
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
      fprintf(stderr, TOOL_NAME ": cannot open %s\n", path);
      return -1;
    }
    if (!strcmp(sfx, ".csv"))
      rc = load_csv(f, in);
    else if (!strcmp(sfx, ".m3u"))
      rc = load_m3u(f, in);
    else {
      fprintf(stderr, TOOL_NAME ": %s: unrecognized suffix, expected .csv/.m3u/.xspf/.xml\n", path);
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
  char line[1024];
  int idx = 0, lineno = 0;

  if (!f) {
    fprintf(stderr, TOOL_NAME ": cannot open %s\n", path);
    return -1;
  }
  while (fgets(line, sizeof line, f)) {
    char *fields[5];
    size_t nf;
    char *end, *svc, *svc_save;
    sds_package_t *pkg;
    lineno++;
    chomp(line);
    if (!line[0])
      continue;
    nf = csv_split(line, fields, 5);
    if (nf < 5) {
      fprintf(stderr, TOOL_NAME ": %s: line %d: expected id,name,lang,visible,svc1|svc2|...\n", path, lineno);
      fclose(f);
      return -1;
    }
    if (idx >= max) {
      fprintf(stderr, TOOL_NAME ": %s: too many packages (max %d)\n", path, max);
      fclose(f);
      return -1;
    }
    pkg = &out[idx];
    memset(pkg, 0, sizeof *pkg);
    pkg->id = (unsigned)strtoul(fields[0], &end, 10);
    if (*end != '\0') {
      fprintf(stderr, TOOL_NAME ": %s: line %d: bad package id: %s\n", path, lineno, fields[0]);
      fclose(f);
      return -1;
    }
    bufcpy(pkg->name, sizeof pkg->name, fields[1]);
    if (strlen(fields[2]) != 3) {
      fprintf(stderr, TOOL_NAME ": %s: line %d: bad lang: %s\n", path, lineno, fields[2]);
      fclose(f);
      return -1;
    }
    memcpy(pkg->lang, fields[2], 3);
    pkg->visible = fields[3][0] == '\0' || fields[3][0] == '1';
    svc = strtok_r(fields[4], "|", &svc_save);
    while (svc) {
      if (pkg->service_count >= SDS_MAX_PKG_SERVICES) {
        fprintf(stderr, TOOL_NAME ": %s: line %d: too many services in package (max %d)\n", path, lineno, SDS_MAX_PKG_SERVICES);
        fclose(f);
        return -1;
      }
      bufcpy(pkg->service_names[pkg->service_count], sizeof pkg->service_names[0], svc);
      pkg->service_count++;
      svc = strtok_r(NULL, "|", &svc_save);
    }
    if (pkg->service_count == 0) {
      fprintf(stderr, TOOL_NAME ": %s: line %d: package has no services\n", path, lineno);
      fclose(f);
      return -1;
    }
    idx++;
  }
  *count = idx;
  fclose(f);
  return 0;
}

int input_load_cells(const char *path, sds_cell_t *out, int max, int *count) {
  FILE *f = fopen(path, "r");
  char line[512];
  int idx = 0, lineno = 0;

  if (!f) {
    fprintf(stderr, TOOL_NAME ": cannot open %s\n", path);
    return -1;
  }
  while (fgets(line, sizeof line, f)) {
    char *tok, *tok_save;
    sds_cell_t *cell;
    lineno++;
    chomp(line);
    if (!line[0])
      continue;
    if (idx >= max) {
      fprintf(stderr, TOOL_NAME ": %s: too many cells (max %d)\n", path, max);
      fclose(f);
      return -1;
    }
    cell = &out[idx];
    memset(cell, 0, sizeof *cell);
    tok = strtok_r(line, ",", &tok_save);
    if (!tok) {
      fprintf(stderr, TOOL_NAME ": %s: line %d: expected id,country,type:value,...\n", path, lineno);
      fclose(f);
      return -1;
    }
    bufcpy(cell->id, sizeof cell->id, tok);
    tok = strtok_r(NULL, ",", &tok_save);
    if (!tok || strlen(tok) != 2) {
      fprintf(stderr, TOOL_NAME ": %s: line %d: bad country code: %s\n", path, lineno, tok ? tok : "(missing)");
      fclose(f);
      return -1;
    }
    bufcpy(cell->country, sizeof cell->country, tok);
    while ((tok = strtok_r(NULL, ",", &tok_save)) != NULL) {
      char *colon = strchr(tok, ':');
      char *end;
      if (!colon) {
        fprintf(stderr, TOOL_NAME ": %s: line %d: bad CA entry: %s\n", path, lineno, tok);
        fclose(f);
        return -1;
      }
      if (cell->ca_depth >= SDS_MAX_CA_DEPTH) {
        fprintf(stderr, TOOL_NAME ": %s: line %d: too many CA entries (max %d)\n", path, lineno, SDS_MAX_CA_DEPTH);
        fclose(f);
        return -1;
      }
      *colon = '\0';
      cell->ca[cell->ca_depth].type = (unsigned)strtoul(tok, &end, 10);
      if (*end != '\0') {
        fprintf(stderr, TOOL_NAME ": %s: line %d: bad CA type: %s\n", path, lineno, tok);
        fclose(f);
        return -1;
      }
      bufcpy(cell->ca[cell->ca_depth].value, sizeof cell->ca[cell->ca_depth].value, colon + 1);
      cell->ca_depth++;
    }
    if (cell->ca_depth == 0) {
      fprintf(stderr, TOOL_NAME ": %s: line %d: cell has no CA entries\n", path, lineno);
      fclose(f);
      return -1;
    }
    idx++;
  }
  *count = idx;
  fclose(f);
  return 0;
}
