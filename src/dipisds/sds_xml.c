/* Copyright 2026 dvbipitools authors. Licensed under GPL-3.0-or-later.
 * See NOTICE and LICENSE for details and authorship information. */

#include <arpa/inet.h>
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "sds_xml.h"

typedef struct {
  unsigned char *buf;
  size_t cap, used;
  int overflow;
} xbuild_t;

static void xb_init(xbuild_t *b, unsigned char *buf, size_t cap) {
  b->buf = buf;
  b->cap = cap;
  b->used = 0;
  b->overflow = 0;
}

static void xb_printf(xbuild_t *b, const char *fmt, ...) {
  va_list ap;
  int n;
  if (b->overflow)
    return;
  va_start(ap, fmt);
  n = vsnprintf((char *)b->buf + b->used, b->cap - b->used, fmt, ap);
  va_end(ap);
  if (n < 0 || (size_t)n >= b->cap - b->used) {
    b->overflow = 1;
    return;
  }
  b->used += (size_t)n;
}

static void xb_escaped(xbuild_t *b, const char *s) {
  for (; *s; s++) {
    switch (*s) {
    case '&': xb_printf(b, "&amp;"); break;
    case '<': xb_printf(b, "&lt;"); break;
    case '>': xb_printf(b, "&gt;"); break;
    case '"': xb_printf(b, "&quot;"); break;
    case '\'': xb_printf(b, "&apos;"); break;
    default: xb_printf(b, "%c", *s); break;
    }
  }
}

size_t sds_build_broadcast(const char *domain, unsigned version, const sds_service_t *svcs, int count, unsigned char *buf, size_t cap) {
  xbuild_t b;
  int i;
  xb_init(&b, buf, cap);
  xb_printf(&b, "<?xml version=\"1.0\" encoding=\"UTF-8\"?>\n<ServiceDiscovery xmlns=\"urn:dvb:metadata:iptv:sdns:2008-1\">\n<BroadcastDiscovery DomainName=\"");
  xb_escaped(&b, domain);
  xb_printf(&b, "\" Version=\"%u\">\n<ServiceList>\n", version);
  for (i = 0; i < count; i++) {
    const sds_service_t *s = &svcs[i];
    xb_printf(&b, "<SingleService><ServiceLocation><IPMulticastAddress Address=\"%s\" Port=\"%u\" Streaming=\"%s\"/></ServiceLocation><TextualIdentifier ServiceName=\"", s->address, s->port, s->rtp ? "rtp" : "udp");
    xb_escaped(&b, s->name);
    xb_printf(&b, "\"/><DVBTriplet OrigNetId=\"%u\" TSId=\"%u\" ServiceId=\"%u\"/></SingleService>\n", s->onid, s->tsid, s->sid);
  }
  xb_printf(&b, "</ServiceList>\n</BroadcastDiscovery>\n</ServiceDiscovery>\n");
  return b.overflow ? 0 : b.used;
}

size_t sds_build_sp(const char *domain, const char *display_name, const char *lang, unsigned version, const char *push_addr, unsigned push_port, unsigned char *buf, size_t cap) {
  xbuild_t b;
  xb_init(&b, buf, cap);
  xb_printf(&b, "<?xml version=\"1.0\" encoding=\"UTF-8\"?>\n<ServiceDiscovery xmlns=\"urn:dvb:metadata:iptv:sdns:2008-1\">\n<ServiceProviderDiscovery>\n<ServiceProvider DomainName=\"");
  xb_escaped(&b, domain);
  xb_printf(&b, "\" Version=\"%u\">\n<Name Language=\"%.3s\">", version, lang);
  xb_escaped(&b, display_name);
  xb_printf(&b, "</Name>\n<Offering><Push Address=\"%s\" Port=\"%u\"><PayloadId Id=\"2\"/></Push></Offering>\n</ServiceProvider>\n</ServiceProviderDiscovery>\n</ServiceDiscovery>\n", push_addr, push_port);
  return b.overflow ? 0 : b.used;
}

int sds_xml_attr(const char *s, const char *end, const char *name, char *out, size_t outcap) {
  size_t namelen = strlen(name);
  const char *p = s;
  while (p < end) {
    const char *hit = strstr(p, name);
    const char *v, *q;
    size_t vlen;
    if (!hit || hit >= end)
      return -1;
    if (hit[namelen] != '=' || hit[namelen + 1] != '"') {
      p = hit + 1;
      continue;
    }
    v = hit + namelen + 2;
    q = strchr(v, '"');
    if (!q || q > end)
      return -1;
    vlen = (size_t)(q - v);
    if (vlen >= outcap)
      vlen = outcap - 1;
    memcpy(out, v, vlen);
    out[vlen] = '\0';
    return 0;
  }
  return -1;
}

int sds_parse_broadcast(const char *xml, sds_service_t *out, int max) {
  const char *p = xml;
  int n = 0;
  while (n < max) {
    const char *tag = strstr(p, "<SingleService");
    const char *end;
    char tmp[32];
    sds_service_t *s;
    if (!tag)
      break;
    end = strstr(tag, "</SingleService>");
    if (!end)
      break;
    s = &out[n];
    memset(s, 0, sizeof *s);
    if (sds_xml_attr(tag, end, "Address", s->address, sizeof s->address) == 0 && sds_xml_attr(tag, end, "Port", tmp, sizeof tmp) == 0) {
      s->port = (unsigned)strtoul(tmp, NULL, 10);
      s->family = strchr(s->address, ':') ? AF_INET6 : AF_INET;
      if (sds_xml_attr(tag, end, "ServiceName", s->name, sizeof s->name))
        s->name[0] = '\0';
      s->rtp = sds_xml_attr(tag, end, "Streaming", tmp, sizeof tmp) == 0 && !strcmp(tmp, "rtp");
      s->onid = sds_xml_attr(tag, end, "OrigNetId", tmp, sizeof tmp) == 0 ? (unsigned)strtoul(tmp, NULL, 10) : 1;
      s->tsid = sds_xml_attr(tag, end, "TSId", tmp, sizeof tmp) == 0 ? (unsigned)strtoul(tmp, NULL, 10) : 1;
      s->sid = sds_xml_attr(tag, end, "ServiceId", tmp, sizeof tmp) == 0 ? (unsigned)strtoul(tmp, NULL, 10) : (unsigned)(n + 1);
      n++;
    }
    p = end + 16;
  }
  return n;
}
