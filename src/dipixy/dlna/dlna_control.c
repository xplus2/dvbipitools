/* Copyright 2026 dvbipitools authors. Licensed under GPL-3.0-or-later.
 * See NOTICE and LICENSE for details and authorship information. */

#include "dlna_int.h"

#include "lib/helper/growutil.h"
#include "lib/helper/strutil.h"
#include "lib/helper/xml_util.h"

#include "gena.h"

#include <stdlib.h>
#include <string.h>

static int handle_browse(const config_t *cfg, const channels_t *channels, const char *body, size_t body_len, char **out, size_t *out_len) {
  char objid[64] = "0";
  char flag[32] = "BrowseDirectChildren";
  char startbuf[16] = "0";
  char countbuf[16] = "0";
  oid_t oid;
  unsigned starting_index;
  unsigned requested_count;
  unsigned number_returned = 0;
  unsigned total_matches = 0;
  char *didl = NULL;
  int metadata;
  int status;
  const char *end = body + body_len;
  char nr[16];
  char tm[16];
  char uid[16];
  soap_field_t fields[4];

  xml_elem_text(body, end, "ObjectID", objid, sizeof objid);
  xml_elem_text(body, end, "BrowseFlag", flag, sizeof flag);
  xml_elem_text(body, end, "StartingIndex", startbuf, sizeof startbuf);
  xml_elem_text(body, end, "RequestedCount", countbuf, sizeof countbuf);
  metadata = !strcmp(flag, "BrowseMetadata");
  starting_index = (unsigned)strtoul(startbuf, NULL, 10);
  requested_count = (unsigned)strtoul(countbuf, NULL, 10);

  if (parse_object_id(objid, &oid) || build_didl(cfg, channels, &oid, metadata, starting_index, requested_count, &didl, &number_returned, &total_matches))
    return soap_fault(out, out_len, 701, "No such object");

  uint_to_str(nr, number_returned);
  uint_to_str(tm, total_matches);
  uint_to_str(uid, gena_system_update_id());
  fields[0] = (soap_field_t){.name = "Result", .value = didl};
  fields[1] = (soap_field_t){.name = "NumberReturned", .value = nr};
  fields[2] = (soap_field_t){.name = "TotalMatches", .value = tm};
  fields[3] = (soap_field_t){.name = "UpdateID", .value = uid};
  status = soap_action_response(out, out_len, CD_URN, "BrowseResponse", fields, 4);
  return status;
}

void dlna_source_protocol_info(const config_t *cfg, char *out, size_t outsz) {
  size_t off = bufcpy(out, outsz, TS_PROTOCOL_INFO "," RADIO_PROTOCOL_INFO "," RADIO_AAC_PROTOCOL_INFO);
  if (cfg->dlna_keep_multicast) {
    off += bufcpy(out + off, outsz - off, "," MCAST_IGMP_PROTOCOL_INFO ",");
    bufcpy(out + off, outsz - off, MCAST_MLD_PROTOCOL_INFO);
  }
}

typedef enum { ACT_NONE, ACT_BROWSE, ACT_SEARCH_CAPS, ACT_SORT_CAPS, ACT_UPDATE_ID, ACT_PROTOCOL_INFO, ACT_CONN_IDS, ACT_CONN_INFO } dlna_action_t;

static dlna_action_t action_lookup(gena_svc_t svc, const char *name) {
  static const struct {
    gena_svc_t svc;
    const char *name;
    dlna_action_t act;
  } tab[] = {
      {GENA_SVC_CD, "Browse", ACT_BROWSE},
      {GENA_SVC_CD, "GetSearchCapabilities", ACT_SEARCH_CAPS},
      {GENA_SVC_CD, "GetSortCapabilities", ACT_SORT_CAPS},
      {GENA_SVC_CD, "GetSystemUpdateID", ACT_UPDATE_ID},
      {GENA_SVC_CM, "GetProtocolInfo", ACT_PROTOCOL_INFO},
      {GENA_SVC_CM, "GetCurrentConnectionIDs", ACT_CONN_IDS},
      {GENA_SVC_CM, "GetCurrentConnectionInfo", ACT_CONN_INFO},
  };
  for (size_t i = 0; i < ARRAY_SIZE(tab); i++)
    if (tab[i].svc == svc && !strcmp(tab[i].name, name)) return tab[i].act;
  return ACT_NONE;
}

int dlna_handle_control(const config_t *cfg, const channels_t *channels, gena_svc_t svc, const char *action, const char *body, size_t body_len, char **out, size_t *out_len) {
  switch (action_lookup(svc, action)) {
    case ACT_BROWSE:
      return handle_browse(cfg, channels, body, body_len, out, out_len);
    case ACT_SEARCH_CAPS: {
      soap_field_t f[1] = {{"SearchCaps", ""}};
      return soap_action_response(out, out_len, CD_URN, "GetSearchCapabilitiesResponse", f, 1);
    }
    case ACT_SORT_CAPS: {
      soap_field_t f[1] = {{"SortCaps", ""}};
      return soap_action_response(out, out_len, CD_URN, "GetSortCapabilitiesResponse", f, 1);
    }
    case ACT_UPDATE_ID: {
      char id[16];
      uint_to_str(id, gena_system_update_id());
      soap_field_t f[1] = {{"Id", id}};
      return soap_action_response(out, out_len, CD_URN, "GetSystemUpdateIDResponse", f, 1);
    }
    case ACT_PROTOCOL_INFO: {
      char source[512];
      dlna_source_protocol_info(cfg, source, sizeof source);
      soap_field_t f[2] = {{"Source", source}, {"Sink", ""}};
      return soap_action_response(out, out_len, CM_URN, "GetProtocolInfoResponse", f, 2);
    }
    case ACT_CONN_IDS: {
      soap_field_t f[1] = {{"ConnectionIDs", "0"}};
      return soap_action_response(out, out_len, CM_URN, "GetCurrentConnectionIDsResponse", f, 1);
    }
    case ACT_CONN_INFO: {
      soap_field_t f[7] = {{"RcsID", "-1"},
                            {"AVTransportID", "-1"},
                            {"ProtocolInfo", TS_PROTOCOL_INFO},
                            {"PeerConnectionManager", ""},
                            {"PeerConnectionID", "-1"},
                            {"Direction", "Output"},
                            {"Status", "OK"}};
      return soap_action_response(out, out_len, CM_URN, "GetCurrentConnectionInfoResponse", f, 7);
    }
    case ACT_NONE:
      break;
  }
  return soap_fault(out, out_len, 401, "Invalid Action");
}
