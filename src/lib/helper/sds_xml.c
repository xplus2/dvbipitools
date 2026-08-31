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
   xor/mul order (addr bytes, family, port) or both tools disagree on port */
static unsigned fcc_resolve_port(const sds_service_t *s, const sds_fcc_t *fcc) {
  unsigned char addr[16];
  size_t addr_len = s->family == AF_INET6 ? 16 : 4;
  uint64_t h = 1469598103934665603ULL;

  if (inet_pton(s->family, s->address, addr) != 1)
    return fcc->port;
  for (size_t i = 0; i < addr_len; i++) {
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
  FILE *f = open_memstream(&ptr, &len);
  if (!f)
    return 0;
  sds_broadcast_open(f, domain, version);
  for (int i = 0; i < count; i++)
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

size_t sds_build_sp(const char *domain, const char *display_name, const char *lang, unsigned version, const char *push_addr, unsigned push_port, const unsigned *extra_payload_ids, int extra_count, unsigned char *buf, size_t cap) {
  char *ptr;
  size_t len;
  FILE *f = open_memstream(&ptr, &len);
  if (!f)
    return 0;
  fputs("<?xml version=\"1.0\" encoding=\"UTF-8\"?>\n<ServiceDiscovery xmlns=\"urn:dvb:metadata:iptv:sdns:2008-1\">\n<ServiceProviderDiscovery>\n<ServiceProvider DomainName=\"", f);
  xml_escape(f, domain);
  fprintf(f, "\" Version=\"%u\">\n<Name Language=\"%.3s\">", version, lang);
  xml_escape(f, display_name);
  fprintf(f, "</Name>\n<Offering><Push Address=\"%s\" Port=\"%u\"><PayloadId Id=\"2\"/>", push_addr, push_port);
  for (int i = 0; i < extra_count; i++)
    fprintf(f, "<PayloadId Id=\"%u\"/>", extra_payload_ids[i]);
  fputs("</Push></Offering>\n</ServiceProvider>\n</ServiceProviderDiscovery>\n</ServiceDiscovery>\n", f);
  fclose(f);
  if (len > cap) {
    free(ptr);
    return 0;
  }
  memcpy(buf, ptr, len);
  free(ptr);
  return len;
}

void sds_package_open(FILE *f, const char *domain, unsigned version) {
  fputs("<?xml version=\"1.0\" encoding=\"UTF-8\"?>\n<ServiceDiscovery xmlns=\"urn:dvb:metadata:iptv:sdns:2008-1\">\n<PackageDiscovery DomainName=\"", f);
  xml_escape(f, domain);
  fprintf(f, "\" Version=\"%u\">\n", version);
}

static const sds_service_t *sds_find_service(const char *name, const sds_service_t *svcs, int svc_count) {
  for (int i = 0; i < svc_count; i++)
    if (!strcmp(svcs[i].name, name))
      return &svcs[i];
  return NULL;
}

void sds_package_item(FILE *f, const sds_package_t *pkg, const sds_service_t *svcs, int svc_count) {
  fprintf(f, "<Package Id=\"%u\" Visible=\"%s\">\n<PackageName Language=\"%.3s\">", pkg->id, pkg->visible ? "true" : "false", pkg->lang);
  xml_escape(f, pkg->name);
  fputs("</PackageName>\n", f);
  for (int i = 0; i < pkg->service_count; i++) {
    const sds_service_t *s = sds_find_service(pkg->service_names[i], svcs, svc_count);
    fputs("<Service><TextualID ServiceName=\"", f);
    xml_escape(f, pkg->service_names[i]);
    fputs("\"/>", f);
    if (s)
      fprintf(f, "<DVBTriplet OrigNetId=\"%u\" TSId=\"%u\" ServiceId=\"%u\"/>", s->onid, s->tsid, s->sid);
    fputs("</Service>\n", f);
  }
  fputs("</Package>\n", f);
}

void sds_package_close(FILE *f) {
  fputs("</PackageDiscovery>\n</ServiceDiscovery>\n", f);
}

size_t sds_build_package(const char *domain, unsigned version, const sds_package_t *pkgs, int pkg_count, const sds_service_t *svcs, int svc_count, unsigned char *buf, size_t cap) {
  char *ptr;
  size_t len;
  FILE *f = open_memstream(&ptr, &len);
  if (!f)
    return 0;
  sds_package_open(f, domain, version);
  for (int i = 0; i < pkg_count; i++)
    sds_package_item(f, &pkgs[i], svcs, svc_count);
  sds_package_close(f);
  fclose(f);
  if (len > cap) {
    free(ptr);
    return 0;
  }
  memcpy(buf, ptr, len);
  free(ptr);
  return len;
}

void sds_regionalisation_open(FILE *f, const char *domain, unsigned version) {
  fputs("<?xml version=\"1.0\" encoding=\"UTF-8\"?>\n<ServiceDiscovery xmlns=\"urn:dvb:metadata:iptv:sdns:2008-1\">\n<RegionalisationDiscovery DomainName=\"", f);
  xml_escape(f, domain);
  fprintf(f, "\" Version=\"%u\">\n", version);
}

void sds_regionalisation_item(FILE *f, const sds_cell_t *cell) {
  fputs("<Cell Id=\"", f);
  xml_escape(f, cell->id);
  fputs("\">\n<CountryCode>", f);
  xml_escape(f, cell->country);
  fputs("</CountryCode>\n", f);
  for (int i = 0; i < cell->ca_depth; i++) {
    fprintf(f, "<CA Type=\"%u\" Value=\"", cell->ca[i].type);
    xml_escape(f, cell->ca[i].value);
    fputs("\">", f);
  }
  for (int i = 0; i < cell->ca_depth; i++)
    fputs("</CA>", f);
  fputs("\n</Cell>\n", f);
}

void sds_regionalisation_close(FILE *f) {
  fputs("</RegionalisationDiscovery>\n</ServiceDiscovery>\n", f);
}

size_t sds_build_regionalisation(const char *domain, unsigned version, const sds_cell_t *cells, int count, unsigned char *buf, size_t cap) {
  char *ptr;
  size_t len;
  FILE *f = open_memstream(&ptr, &len);
  if (!f)
    return 0;
  sds_regionalisation_open(f, domain, version);
  for (int i = 0; i < count; i++)
    sds_regionalisation_item(f, &cells[i]);
  sds_regionalisation_close(f);
  fclose(f);
  if (len > cap) {
    free(ptr);
    return 0;
  }
  memcpy(buf, ptr, len);
  free(ptr);
  return len;
}

size_t sds_build_rms_fus(const char *domain, unsigned version, const sds_rms_t *rms, int rms_count, const sds_fus_t *fus, int fus_count, unsigned char *buf, size_t cap) {
  char *ptr;
  size_t len;
  FILE *f = open_memstream(&ptr, &len);
  if (!f)
    return 0;
  fputs("<?xml version=\"1.0\" encoding=\"UTF-8\"?>\n<ServiceDiscovery xmlns=\"urn:dvb:metadata:iptv:sdns:2008-1\">\n<RMSFUSDiscovery DomainName=\"", f);
  xml_escape(f, domain);
  fprintf(f, "\" Version=\"%u\">\n", version);
  for (int i = 0; i < rms_count; i++) {
    fputs("<RMSProvider RMSLocation=\"", f);
    xml_escape(f, rms[i].location);
    fputs("\"", f);
    if (rms[i].logo_uri) {
      fputs(" LogoURI=\"", f);
      xml_escape(f, rms[i].logo_uri);
      fputs("\"", f);
    }
    fprintf(f, "><RMSName Language=\"%.3s\">", rms[i].lang);
    xml_escape(f, rms[i].name);
    fputs("</RMSName></RMSProvider>\n", f);
  }
  for (int i = 0; i < fus_count; i++) {
    fputs("<FUSProvider", f);
    if (fus[i].logo_uri) {
      fputs(" LogoURI=\"", f);
      xml_escape(f, fus[i].logo_uri);
      fputs("\"", f);
    }
    fprintf(f, "><FUSName Language=\"%.3s\">", fus[i].lang);
    xml_escape(f, fus[i].name);
    fprintf(f, "</FUSName><FUSID>%lu</FUSID><FUSAnnouncement>", fus[i].fus_id);
    if (fus[i].announce_addr)
      fprintf(f, "<MulticastAnnouncementAddress Address=\"%s\" Port=\"%u\"/>", fus[i].announce_addr, fus[i].announce_port);
    fputs("</FUSAnnouncement></FUSProvider>\n", f);
  }
  fputs("</RMSFUSDiscovery>\n</ServiceDiscovery>\n", f);
  fclose(f);
  if (len > cap) {
    free(ptr);
    return 0;
  }
  memcpy(buf, ptr, len);
  free(ptr);
  return len;
}

typedef struct {
  const char *tag, *end;
} xml_span_t;

static int capture_first_span(const char *tag, const char *blk_end, void *ctx) {
  xml_span_t *sp = ctx;
  sp->tag = tag;
  sp->end = blk_end;
  return -1;
}

static void parse_ret(const char *tag, const char *end, sds_service_t *s) {
  xml_span_t ret = {0};
  char tmp[32];

  if (for_each_xml_block(tag, end, "<RTPRetransmission", "</RTPRetransmission>", capture_first_span, &ret) != -1)
    return;
  s->has_ret = 1;
  xml_attr(ret.tag, ret.end, "DestinationAddress", s->ret.addr, sizeof s->ret.addr);
  if (xml_attr(ret.tag, ret.end, "DestinationPort", tmp, sizeof tmp) == 0)
    s->ret.port = (unsigned)strtoul(tmp, NULL, 10);
  if (xml_attr(ret.tag, ret.end, "rtx-time", tmp, sizeof tmp) == 0)
    s->ret.rtx_time_ms = (unsigned)strtoul(tmp, NULL, 10);
  if (xml_attr(ret.tag, ret.end, "RTPPayloadTypeNumber", tmp, sizeof tmp) == 0)
    s->ret.rtx_pt = (unsigned char)strtoul(tmp, NULL, 10);
  s->ret.rsi_mc_ret = xml_attr(ret.tag, ret.end, "dvb-rsi-mc-ret", tmp, sizeof tmp) == 0 && !strcmp(tmp, "true");
  {
    xml_span_t mc = {0};
    if (for_each_xml_block(ret.tag, ret.end, "<MulticastRET", "/>", capture_first_span, &mc) == -1) {
      s->ret.mc = 1;
      if (xml_attr(mc.tag, mc.end, "DestinationPort", tmp, sizeof tmp) == 0) {
        unsigned mc_port = (unsigned)strtoul(tmp, NULL, 10);
        if (mc_port != s->port)
          s->ret.mc_port = mc_port;
      }
    }
  }
}

static void parse_fcc(const char *tag, const char *end, sds_service_t *s) {
  xml_span_t fcc = {0};
  char tmp[32];

  if (for_each_xml_block(tag, end, "<ServerBasedEnhancementServiceInfo", "</ServerBasedEnhancementServiceInfo>", capture_first_span, &fcc) != -1)
    return;
  s->has_fcc = 1;
  {
    xml_span_t rep = {0};
    if (for_each_xml_block(fcc.tag, fcc.end, "<RTCPReporting", "/>", capture_first_span, &rep) == -1) {
      xml_attr(rep.tag, rep.end, "DestinationAddress", s->fcc.addr, sizeof s->fcc.addr);
      if (xml_attr(rep.tag, rep.end, "DestinationPort", tmp, sizeof tmp) == 0)
        s->fcc.port = (unsigned)strtoul(tmp, NULL, 10);
    }
  }
  {
    xml_span_t rtx = {0};
    if (for_each_xml_block(fcc.tag, fcc.end, "<Retransmission_session", "/>", capture_first_span, &rtx) == -1) {
      if (xml_attr(rtx.tag, rtx.end, "rtx-time", tmp, sizeof tmp) == 0)
        s->fcc.rtx_time_ms = (unsigned)strtoul(tmp, NULL, 10);
      if (xml_attr(rtx.tag, rtx.end, "RTPPayloadTypeNumber", tmp, sizeof tmp) == 0)
        s->fcc.rtx_pt = (unsigned char)strtoul(tmp, NULL, 10);
    }
  }
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
      if (xml_elem_text(tag, end, "MaxBitrate", tmp, sizeof tmp) == 0) {
        s->max_bitrate_kbps = (unsigned)strtoul(tmp, NULL, 10);
        s->has_bitrate = 1;
      }
      {
        xml_span_t si = {0};
        if (for_each_xml_block(tag, end, "<SI", "</SI>", capture_first_span, &si) == -1 &&
            xml_elem_text(si.tag, si.end, "ContentGenre", tmp, sizeof tmp) == 0) {
          s->content_nibble = (unsigned)strtoul(tmp, NULL, 10);
          s->has_content_nibble = 1;
        }
      }
      parse_ret(tag, end, s);
      parse_fcc(tag, end, s);
      n++;
    }
    p = end + 16;
  }
  if (truncated && n == max && strstr(p, "<SingleService"))
    *truncated = 1;
  return n;
}
