/* Copyright 2026 dvbipitools authors. Licensed under GPL-3.0-or-later.
 * See NOTICE and LICENSE for details and authorship information. */

#include "dlna_int.h"

#include "lib/helper/ioutil.h"
#include "lib/helper/xml_util.h"

#include "gena.h"

#include <stdlib.h>
#include <string.h>

static int handle_browse(const config_t *cfg, const channels_t *channels, const char *body, size_t body_len, char **out, size_t *out_len) {
  char objid[64] = "0", flag[32] = "BrowseDirectChildren", startbuf[16] = "0", countbuf[16] = "0";
  oid_t oid;
  unsigned starting_index, requested_count, number_returned = 0, total_matches = 0;
  char *didl = NULL;
  int metadata, status;
  const char *end = body + body_len;

  xml_elem_text(body, end, "ObjectID", objid, sizeof objid);
  xml_elem_text(body, end, "BrowseFlag", flag, sizeof flag);
  xml_elem_text(body, end, "StartingIndex", startbuf, sizeof startbuf);
  xml_elem_text(body, end, "RequestedCount", countbuf, sizeof countbuf);
  metadata = !strcmp(flag, "BrowseMetadata");
  starting_index = (unsigned)strtoul(startbuf, NULL, 10);
  requested_count = (unsigned)strtoul(countbuf, NULL, 10);

  if (parse_object_id(objid, &oid) || build_didl(cfg, channels, &oid, metadata, starting_index, requested_count, &didl, &number_returned, &total_matches))
    return soap_fault(out, out_len, 701, "No such object");

  {
    char nr[16], tm[16], uid[16];
    soap_field_t fields[4];
    uint_to_str(nr, number_returned);
    uint_to_str(tm, total_matches);
    uint_to_str(uid, gena_system_update_id());
    fields[0].name = "Result";
    fields[0].value = didl;
    fields[1].name = "NumberReturned";
    fields[1].value = nr;
    fields[2].name = "TotalMatches";
    fields[2].value = tm;
    fields[3].name = "UpdateID";
    fields[3].value = uid;
    status = soap_action_response(out, out_len, CD_URN, "BrowseResponse", fields, 4);
  }
  return status;
}

void dlna_source_protocol_info(const config_t *cfg, char *out, size_t outsz) {
  if (cfg->dlna_keep_multicast) {
    size_t off = bufcpy(out, outsz, TS_PROTOCOL_INFO ",");
    off += bufcpy(out + off, outsz - off, MCAST_IGMP_PROTOCOL_INFO ",");
    bufcpy(out + off, outsz - off, MCAST_MLD_PROTOCOL_INFO);
  } else
    bufcpy(out, outsz, TS_PROTOCOL_INFO);
}

int dlna_handle_control(const config_t *cfg, const channels_t *channels, const char *service, const char *action, const char *body, size_t body_len, char **out, size_t *out_len) {
  if (!strcmp(service, "cd")) {
    if (!strcmp(action, "Browse"))
      return handle_browse(cfg, channels, body, body_len, out, out_len);
    if (!strcmp(action, "GetSearchCapabilities")) {
      soap_field_t f[1] = {{"SearchCaps", ""}};
      return soap_action_response(out, out_len, CD_URN, "GetSearchCapabilitiesResponse", f, 1);
    }
    if (!strcmp(action, "GetSortCapabilities")) {
      soap_field_t f[1] = {{"SortCaps", ""}};
      return soap_action_response(out, out_len, CD_URN, "GetSortCapabilitiesResponse", f, 1);
    }
    if (!strcmp(action, "GetSystemUpdateID")) {
      char id[16];
      uint_to_str(id, gena_system_update_id());
      soap_field_t f[1] = {{"Id", id}};
      return soap_action_response(out, out_len, CD_URN, "GetSystemUpdateIDResponse", f, 1);
    }
    return soap_fault(out, out_len, 401, "Invalid Action");
  }
  if (!strcmp(service, "cm")) {
    if (!strcmp(action, "GetProtocolInfo")) {
      char source[256];
      dlna_source_protocol_info(cfg, source, sizeof source);
      soap_field_t f[2] = {{"Source", source}, {"Sink", ""}};
      return soap_action_response(out, out_len, CM_URN, "GetProtocolInfoResponse", f, 2);
    }
    if (!strcmp(action, "GetCurrentConnectionIDs")) {
      soap_field_t f[1] = {{"ConnectionIDs", "0"}};
      return soap_action_response(out, out_len, CM_URN, "GetCurrentConnectionIDsResponse", f, 1);
    }
    if (!strcmp(action, "GetCurrentConnectionInfo")) {
      soap_field_t f[7] = {{"RcsID", "-1"},
                            {"AVTransportID", "-1"},
                            {"ProtocolInfo", TS_PROTOCOL_INFO},
                            {"PeerConnectionManager", ""},
                            {"PeerConnectionID", "-1"},
                            {"Direction", "Output"},
                            {"Status", "OK"}};
      return soap_action_response(out, out_len, CM_URN, "GetCurrentConnectionInfoResponse", f, 7);
    }
    return soap_fault(out, out_len, 401, "Invalid Action");
  }
  return soap_fault(out, out_len, 401, "Invalid Action");
}
