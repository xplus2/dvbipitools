/* Copyright 2026 dvbipitools authors. Licensed under GPL-3.0-or-later.
 * See NOTICE and LICENSE for details and authorship information. */

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

/* spec: at least one SingleService. triplet identifies service */
static int check_services(const char *path, const input_t *in) {
  if (!in->service_count) {
    log_code("AE000771", "%s: no %s", path, "services");
    return -1;
  }
  for (int i = 1; i < in->service_count; i++) {
    const sds_service_t *a = &in->services[i];
    for (int j = 0; j < i; j++) {
      const sds_service_t *b = &in->services[j];
      if (a->onid == b->onid && a->tsid == b->tsid && a->sid == b->sid) {
        log_code("AE400320", "%s: duplicate DVB triplet %u.%u.%u: %s, %s", path, a->onid, a->tsid, a->sid, b->name, a->name);
        return -1;
      }
    }
  }
  return 0;
}

int input_load(const char *path, input_t *in) {
  const char *sfx = suffix(path);
  memset(in, 0, sizeof *in);
  if (!strcasecmp(sfx, ".xml")) {
    char *buf;
    size_t len;
    static const struct { const char *tag; unsigned payload_id; } root_tags[] = {
      {"BroadcastDiscovery", DVBSTP_PAYLOAD_BROADCAST_DISCOVERY},
      {"ServiceProviderDiscovery", DVBSTP_PAYLOAD_SP_DISCOVERY},
      {"PackageDiscovery", DVBSTP_PAYLOAD_PACKAGE_DISCOVERY},
      {"RegionalisationDiscovery", DVBSTP_PAYLOAD_REGIONALISATION_DISCOVERY},
      {"RegionalizationDiscovery", DVBSTP_PAYLOAD_REGIONALISATION_DISCOVERY}, /* XSD spelling */
      {"RMSFUSDiscovery", DVBSTP_PAYLOAD_RMSFUS_DISCOVERY},
      {"BCGDiscovery", DVBSTP_PAYLOAD_BCG_DISCOVERY},
    };
    size_t i;
    if (read_path(path, &buf, &len)) {
      log_code("AE000227", "cannot read %s", path);
      return -1;
    }
    if (len > DVBSTP_MAX_SEGMENT) {
      log_code("AE000695", "%s: %zu bytes, max %u", path, len, DVBSTP_MAX_SEGMENT);
      free(buf);
      return -1;
    }
    in->raw_version = -1;
    for (i = 0; i < sizeof root_tags / sizeof root_tags[0]; i++) {
      const char *root = xml_find_start(buf, buf + len, root_tags[i].tag);
      if (root) {
        /* DomainName, Version: on ServiceProvider for SP discovery, on record element otherwise */
        const char *rec_tag = root_tags[i].payload_id == DVBSTP_PAYLOAD_SP_DISCOVERY ? "ServiceProvider" : root_tags[i].tag;
        const char *rec = xml_find_start(root, buf + len, rec_tag);
        char ver[8];
        char *e;
        unsigned long v;
        in->raw_payload_id = root_tags[i].payload_id;
        in->raw_provider_count = count_xml_elems(buf, len, rec_tag);
        if (rec && xml_tag_attr(rec, buf + len, "Version", ver, sizeof ver) == 0 && ver[0]) {
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
    in->raw_xml = (unsigned char *)buf;
    in->raw_xml_len = len;
    in->service_count = count_xml_elems(buf, len, "SingleService");
    return 0;
  }

  in->kind = INPUT_SERVICES;
  if (!strcasecmp(sfx, ".xspf")) {
    char *buf;
    size_t len;
    int rc;
    if (read_path(path, &buf, &len)) {
      log_code("AE000227", "cannot read %s", path);
      return -1;
    }
    rc = load_xspf(in, (const unsigned char *)buf);
    free(buf);
    return rc ? rc : check_services(path, in);
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
    return rc ? rc : check_services(path, in);
  }
}

void input_free(input_t *in) {
  free(in->raw_xml);
  in->raw_xml = NULL;
}
