/* Copyright 2026 dvbipitools authors. Licensed under GPL-3.0-or-later.
 * See NOTICE and LICENSE for details and authorship information. */

#include <arpa/inet.h>
#include <stdlib.h>
#include <string.h>

#include "sds_xml.h"
#include "xml_util.h"

void sds_broadcast_open(FILE *f, const char *domain, unsigned version) {
  fputs("<?xml version=\"1.0\" encoding=\"UTF-8\"?>\n<ServiceDiscovery xmlns=\"urn:dvb:metadata:iptv:sdns:2008-1\">\n<BroadcastDiscovery DomainName=\"", f);
  xml_escape(f, domain);
  fprintf(f, "\" Version=\"%u\">\n<ServiceList>\n", version);
}

/* must match chan_key_hash() in src/dipifccret/channel/hash.c exactly: same constants, same
   xor/mul order (addr bytes, then family, then port), or the two tools disagree on the port */
static unsigned fcc_resolve_port(const sds_service_t *s, const sds_fcc_t *fcc) {
  unsigned char addr[16];
  size_t addr_len = s->family == AF_INET6 ? 16 : 4;
  uint64_t h = 1469598103934665603ULL;
  size_t i;

  if (inet_pton(s->family, s->address, addr) != 1)
    return fcc->port;
  for (i = 0; i < addr_len; i++) {
    h ^= addr[i];
    h *= 1099511628211ULL;
  }
  h ^= (unsigned)s->family;
  h *= 1099511628211ULL;
  h ^= s->port;
  h *= 1099511628211ULL;
  return fcc->resolve_base_port + (unsigned)((size_t)h % fcc->resolve_max_channels);
}

void sds_broadcast_item(FILE *f, const sds_service_t *s, const sds_ret_t *ret, const sds_fcc_t *fcc) {
  fprintf(f, "<SingleService><ServiceLocation><IPMulticastAddress Address=\"%s\" Port=\"%u\" Streaming=\"%s\"", s->address, s->port, s->rtp ? "rtp" : "udp");
  if (ret || fcc) {
    fputs(">", f);
    if (ret) {
      fprintf(f, "<RTPRetransmission><RTCPReporting DestinationAddress=\"%s\" DestinationPort=\"%u\"", ret->addr, ret->port);
      if (ret->rsi_mc_ret)
        fputs(" dvb-rsi-mc-ret=\"true\"", f);
      fputs("/>", f);
      fprintf(f, "<UnicastRET rtx-time=\"%u\" RTPPayloadTypeNumber=\"%u\"/>", ret->rtx_time_ms, ret->rtx_pt);
      if (ret->mc)
        fprintf(f, "<MulticastRET GroupAddress=\"%s\" DestinationPort=\"%u\" rtx-time=\"%u\" RTPPayloadTypeNumber=\"%u\"/>", s->address, ret->mc_port ? ret->mc_port : s->port, ret->rtx_time_ms, ret->rtx_pt);
      fputs("</RTPRetransmission>", f);
    }
    if (fcc) {
      unsigned port = fcc->resolve_by_port ? fcc_resolve_port(s, fcc) : fcc->port;
      fputs("<ServerBasedEnhancementServiceInfo><EnhancementService>FCC</EnhancementService>", f);
      fprintf(f, "<RTCPReporting DestinationAddress=\"%s\" DestinationPort=\"%u\"/>", fcc->addr, port);
      fprintf(f, "<Retransmission_session DestinationPort=\"%u\" rtx-time=\"%u\" RTPPayloadTypeNumber=\"%u\"/>", port, fcc->rtx_time_ms, fcc->rtx_pt);
      fputs("</ServerBasedEnhancementServiceInfo>", f);
    }
    fputs("</IPMulticastAddress>", f);
  } else {
    fputs("/>", f);
  }
  fputs("</ServiceLocation><TextualIdentifier ServiceName=\"", f);
  xml_escape(f, s->name);
  fprintf(f, "\"/><DVBTriplet OrigNetId=\"%u\" TSId=\"%u\" ServiceId=\"%u\"/></SingleService>\n", s->onid, s->tsid, s->sid);
}

void sds_broadcast_close(FILE *f) {
  fputs("</ServiceList>\n</BroadcastDiscovery>\n</ServiceDiscovery>\n", f);
}

size_t sds_build_broadcast(const char *domain, unsigned version, const sds_service_t *svcs, int count, const sds_ret_t *ret, const sds_fcc_t *fcc, unsigned char *buf, size_t cap) {
  char *ptr;
  size_t len;
  int i;
  FILE *f = open_memstream(&ptr, &len);
  if (!f)
    return 0;
  sds_broadcast_open(f, domain, version);
  for (i = 0; i < count; i++)
    sds_broadcast_item(f, &svcs[i], ret, fcc);
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
  xml_escape(f, domain);
  fprintf(f, "\" Version=\"%u\">\n<Name Language=\"%.3s\">", version, lang);
  xml_escape(f, display_name);
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

int sds_parse_broadcast(const char *xml, sds_service_t *out, int max, int *truncated) {
  const char *p = xml;
  int n = 0;

  if (truncated)
    *truncated = 0;
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
    if (xml_attr(tag, end, "Address", s->address, sizeof s->address) == 0 && xml_attr(tag, end, "Port", tmp, sizeof tmp) == 0) {
      s->port = (unsigned)strtoul(tmp, NULL, 10);
      s->family = strchr(s->address, ':') ? AF_INET6 : AF_INET;
      if (xml_attr(tag, end, "ServiceName", s->name, sizeof s->name))
        s->name[0] = '\0';
      s->rtp = xml_attr(tag, end, "Streaming", tmp, sizeof tmp) == 0 && !strcmp(tmp, "rtp");
      s->onid = xml_attr(tag, end, "OrigNetId", tmp, sizeof tmp) == 0 ? (unsigned)strtoul(tmp, NULL, 10) : 1;
      s->tsid = xml_attr(tag, end, "TSId", tmp, sizeof tmp) == 0 ? (unsigned)strtoul(tmp, NULL, 10) : 1;
      s->sid = xml_attr(tag, end, "ServiceId", tmp, sizeof tmp) == 0 ? (unsigned)strtoul(tmp, NULL, 10) : (unsigned)(n + 1);
      n++;
    }
    p = end + 16;
  }
  if (truncated && n == max && strstr(p, "<SingleService"))
    *truncated = 1;
  return n;
}
