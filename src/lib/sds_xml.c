/* Copyright 2026 dvbipitools authors. Licensed under GPL-3.0-or-later.
 * See NOTICE and LICENSE for details and authorship information. */

#include <arpa/inet.h>
#include <stdlib.h>
#include <string.h>

#include "sds_xml.h"

void sds_xml_escape(FILE *f, const char *s) {
  for (; *s; s++) {
    switch (*s) {
    case '&': fputs("&amp;", f); break;
    case '<': fputs("&lt;", f); break;
    case '>': fputs("&gt;", f); break;
    case '"': fputs("&quot;", f); break;
    case '\'': fputs("&apos;", f); break;
    default: fputc(*s, f); break;
    }
  }
}

void sds_broadcast_open(FILE *f, const char *domain, unsigned version) {
  fputs("<?xml version=\"1.0\" encoding=\"UTF-8\"?>\n<ServiceDiscovery xmlns=\"urn:dvb:metadata:iptv:sdns:2008-1\">\n<BroadcastDiscovery DomainName=\"", f);
  sds_xml_escape(f, domain);
  fprintf(f, "\" Version=\"%u\">\n<ServiceList>\n", version);
}

void sds_broadcast_item(FILE *f, const sds_service_t *s) {
  fprintf(f, "<SingleService><ServiceLocation><IPMulticastAddress Address=\"%s\" Port=\"%u\" Streaming=\"%s\"/></ServiceLocation><TextualIdentifier ServiceName=\"", s->address, s->port, s->rtp ? "rtp" : "udp");
  sds_xml_escape(f, s->name);
  fprintf(f, "\"/><DVBTriplet OrigNetId=\"%u\" TSId=\"%u\" ServiceId=\"%u\"/></SingleService>\n", s->onid, s->tsid, s->sid);
}

void sds_broadcast_close(FILE *f) {
  fputs("</ServiceList>\n</BroadcastDiscovery>\n</ServiceDiscovery>\n", f);
}

size_t sds_build_broadcast(const char *domain, unsigned version, const sds_service_t *svcs, int count, unsigned char *buf, size_t cap) {
  char *ptr;
  size_t len;
  int i;
  FILE *f = open_memstream(&ptr, &len);
  if (!f)
    return 0;
  sds_broadcast_open(f, domain, version);
  for (i = 0; i < count; i++)
    sds_broadcast_item(f, &svcs[i]);
  sds_broadcast_close(f);
  fclose(f);
  if (len > cap) {
    free(ptr);
    return 0;
  }
  memcpy(buf, ptr, len);
  free(ptr);
  return len;
}

size_t sds_build_sp(const char *domain, const char *display_name, const char *lang, unsigned version, const char *push_addr, unsigned push_port, unsigned char *buf, size_t cap) {
  char *ptr;
  size_t len;
  FILE *f = open_memstream(&ptr, &len);
  if (!f)
    return 0;
  fputs("<?xml version=\"1.0\" encoding=\"UTF-8\"?>\n<ServiceDiscovery xmlns=\"urn:dvb:metadata:iptv:sdns:2008-1\">\n<ServiceProviderDiscovery>\n<ServiceProvider DomainName=\"", f);
  sds_xml_escape(f, domain);
  fprintf(f, "\" Version=\"%u\">\n<Name Language=\"%.3s\">", version, lang);
  sds_xml_escape(f, display_name);
  fprintf(f, "</Name>\n<Offering><Push Address=\"%s\" Port=\"%u\"><PayloadId Id=\"2\"/></Push></Offering>\n</ServiceProvider>\n</ServiceProviderDiscovery>\n</ServiceDiscovery>\n", push_addr, push_port);
  fclose(f);
  if (len > cap) {
    free(ptr);
    return 0;
  }
  memcpy(buf, ptr, len);
  free(ptr);
  return len;
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
