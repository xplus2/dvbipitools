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
      log_code("AE000695", "%s: %zu bytes, max %u", path, len, DVBSTP_MAX_SEGMENT);
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
