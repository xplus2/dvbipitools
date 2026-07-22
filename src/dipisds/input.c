/* Copyright 2026 dvbipitools authors. Licensed under GPL-3.0-or-later.
 * See NOTICE and LICENSE for details and authorship information. */

#include <arpa/inet.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

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
  if (fseek(f, 0, SEEK_END) || (sz = ftell(f)) < 0) {
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
  snprintf(s->address, sizeof s->address, "%s", addr);
  return 0;
}

static int load_csv(FILE *f, input_t *in) {
  char line[512];
  int idx = 0, lineno = 0;

  while (fgets(line, sizeof line, f)) {
    char *fields[5];
    int nf = 0;
    char *p = line;
    size_t l = strlen(line);
    sds_service_t *s;
    lineno++;
    while (l && (line[l - 1] == '\n' || line[l - 1] == '\r'))
      line[--l] = '\0';
    if (!line[0])
      continue;
    while (nf < 5) {
      fields[nf++] = p;
      p = strchr(p, ',');
      if (!p)
        break;
      *p = '\0';
      p++;
    }
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
    snprintf(s->name, sizeof s->name, "%s", fields[0]);
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
    size_t l = strlen(line);
    while (l && (line[l - 1] == '\n' || line[l - 1] == '\r'))
      line[--l] = '\0';
    if (!line[0])
      continue;
    if (!strncmp(line, "#EXTINF:", 8)) {
      char *comma = strrchr(line, ',');
      char tmp[32];
      if (!comma) {
        fprintf(stderr, TOOL_NAME ": malformed #EXTINF line: %s\n", line);
        return -1;
      }
      snprintf(pending_name, sizeof pending_name, "%s", comma + 1);
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
      snprintf(s->name, sizeof s->name, "%s", pending_name);
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
      if (te && te <= end) {
        size_t n = (size_t)(te - tb);
        if (n >= sizeof title)
          n = sizeof title - 1;
        memcpy(title, tb, n);
        title[n] = '\0';
      }
    }

    s = &in->services[idx];
    memset(s, 0, sizeof *s);
    snprintf(s->name, sizeof s->name, "%s", title);
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
    else {
      fprintf(stderr, TOOL_NAME ": %s: no BroadcastDiscovery/ServiceProviderDiscovery root element\n", path);
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
