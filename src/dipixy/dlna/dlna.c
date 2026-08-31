/* Copyright 2026 dvbipitools authors. Licensed under GPL-3.0-or-later.
 * See NOTICE and LICENSE for details and authorship information. */

#include "dlna_int.h"

#include "lib/helper/xml_util.h"

#include "../version.h"
#include "ssdp.h"

int dlna_device_desc_xml(const config_t *cfg, char **out, size_t *out_len) {
  static _Thread_local char buf[4096];
  char uuid[37];
  FILE *f = fmemopen(buf, sizeof buf, "w");
  if (!f)
    return -1;
  ssdp_device_uuid(cfg, uuid);
  fputs("<?xml version=\"1.0\" encoding=\"UTF-8\"?>\r\n"
        "<root xmlns=\"urn:schemas-upnp-org:device-1-0\">"
        "<specVersion><major>1</major><minor>0</minor></specVersion>"
        "<device>"
        "<deviceType>urn:schemas-upnp-org:device:MediaServer:1</deviceType>",
        f);
  fputs("<friendlyName>", f);
  if (cfg->dlna_name && cfg->dlna_name[0]) {
    xml_escape(f, cfg->dlna_name);
  } else {
    xml_escape(f, TOOL_NAME);
    fputs(" (", f);
    xml_escape(f, cfg->dlna_host);
    fputs(")", f);
  }
  fputs("</friendlyName>"
        "<manufacturer>dvbipitools</manufacturer>"
        "<modelDescription>DVB-IPI multicast to DLNA bridge</modelDescription>"
        "<modelName>dipixy</modelName>",
        f);
  fprintf(f, "<modelNumber>%s</modelNumber><UDN>uuid:%s</UDN>", TOOL_VERSION, uuid);
  fputs("<serviceList>"
        "<service><serviceType>" CD_URN "</serviceType>"
        "<serviceId>urn:upnp-org:serviceId:ContentDirectory</serviceId>"
        "<SCPDURL>/dlna/cd_scpd.xml</SCPDURL><controlURL>/dlna/cd_control</controlURL>"
        "<eventSubURL>/dlna/cd_event</eventSubURL></service>"
        "<service><serviceType>" CM_URN "</serviceType>"
        "<serviceId>urn:upnp-org:serviceId:ConnectionManager</serviceId>"
        "<SCPDURL>/dlna/cm_scpd.xml</SCPDURL><controlURL>/dlna/cm_control</controlURL>"
        "<eventSubURL>/dlna/cm_event</eventSubURL></service>"
        "</serviceList>",
        f);
  fputs("</device></root>\r\n", f);
  {
    long pos = ftell(f);
    int err = ferror(f);
    fclose(f);
    if (err || pos < 0)
      return -1;
    *out = buf;
    *out_len = (size_t)pos;
  }
  return 0;
}

static const char CD_SCPD[] =
    "<?xml version=\"1.0\" encoding=\"UTF-8\"?>\r\n"
    "<scpd xmlns=\"urn:schemas-upnp-org:service-1-0\">"
    "<specVersion><major>1</major><minor>0</minor></specVersion>"
    "<actionList>"
    "<action><name>Browse</name><argumentList>"
    "<argument><name>ObjectID</name><direction>in</direction>"
    "<relatedStateVariable>A_ARG_TYPE_ObjectID</relatedStateVariable></argument>"
    "<argument><name>BrowseFlag</name><direction>in</direction>"
    "<relatedStateVariable>A_ARG_TYPE_BrowseFlag</relatedStateVariable></argument>"
    "<argument><name>Filter</name><direction>in</direction>"
    "<relatedStateVariable>A_ARG_TYPE_Filter</relatedStateVariable></argument>"
    "<argument><name>StartingIndex</name><direction>in</direction>"
    "<relatedStateVariable>A_ARG_TYPE_Index</relatedStateVariable></argument>"
    "<argument><name>RequestedCount</name><direction>in</direction>"
    "<relatedStateVariable>A_ARG_TYPE_Count</relatedStateVariable></argument>"
    "<argument><name>SortCriteria</name><direction>in</direction>"
    "<relatedStateVariable>A_ARG_TYPE_SortCriteria</relatedStateVariable></argument>"
    "<argument><name>Result</name><direction>out</direction>"
    "<relatedStateVariable>A_ARG_TYPE_Result</relatedStateVariable></argument>"
    "<argument><name>NumberReturned</name><direction>out</direction>"
    "<relatedStateVariable>A_ARG_TYPE_Count</relatedStateVariable></argument>"
    "<argument><name>TotalMatches</name><direction>out</direction>"
    "<relatedStateVariable>A_ARG_TYPE_Count</relatedStateVariable></argument>"
    "<argument><name>UpdateID</name><direction>out</direction>"
    "<relatedStateVariable>A_ARG_TYPE_UpdateID</relatedStateVariable></argument>"
    "</argumentList></action>"
    "<action><name>GetSearchCapabilities</name><argumentList>"
    "<argument><name>SearchCaps</name><direction>out</direction>"
    "<relatedStateVariable>SearchCapabilities</relatedStateVariable></argument>"
    "</argumentList></action>"
    "<action><name>GetSortCapabilities</name><argumentList>"
    "<argument><name>SortCaps</name><direction>out</direction>"
    "<relatedStateVariable>SortCapabilities</relatedStateVariable></argument>"
    "</argumentList></action>"
    "<action><name>GetSystemUpdateID</name><argumentList>"
    "<argument><name>Id</name><direction>out</direction>"
    "<relatedStateVariable>SystemUpdateID</relatedStateVariable></argument>"
    "</argumentList></action>"
    "</actionList>"
    "<serviceStateTable>"
    "<stateVariable sendEvents=\"no\"><name>A_ARG_TYPE_ObjectID</name><dataType>string</dataType></stateVariable>"
    "<stateVariable sendEvents=\"no\"><name>A_ARG_TYPE_BrowseFlag</name><dataType>string</dataType>"
    "<allowedValueList><allowedValue>BrowseMetadata</allowedValue>"
    "<allowedValue>BrowseDirectChildren</allowedValue></allowedValueList></stateVariable>"
    "<stateVariable sendEvents=\"no\"><name>A_ARG_TYPE_Filter</name><dataType>string</dataType></stateVariable>"
    "<stateVariable sendEvents=\"no\"><name>A_ARG_TYPE_SortCriteria</name><dataType>string</dataType></stateVariable>"
    "<stateVariable sendEvents=\"no\"><name>A_ARG_TYPE_Index</name><dataType>ui4</dataType></stateVariable>"
    "<stateVariable sendEvents=\"no\"><name>A_ARG_TYPE_Count</name><dataType>ui4</dataType></stateVariable>"
    "<stateVariable sendEvents=\"no\"><name>A_ARG_TYPE_UpdateID</name><dataType>ui4</dataType></stateVariable>"
    "<stateVariable sendEvents=\"no\"><name>A_ARG_TYPE_Result</name><dataType>string</dataType></stateVariable>"
    "<stateVariable sendEvents=\"yes\"><name>SystemUpdateID</name><dataType>ui4</dataType></stateVariable>"
    "<stateVariable sendEvents=\"no\"><name>SearchCapabilities</name><dataType>string</dataType></stateVariable>"
    "<stateVariable sendEvents=\"no\"><name>SortCapabilities</name><dataType>string</dataType></stateVariable>"
    "</serviceStateTable>"
    "</scpd>\r\n";

static const char CM_SCPD[] =
    "<?xml version=\"1.0\" encoding=\"UTF-8\"?>\r\n"
    "<scpd xmlns=\"urn:schemas-upnp-org:service-1-0\">"
    "<specVersion><major>1</major><minor>0</minor></specVersion>"
    "<actionList>"
    "<action><name>GetProtocolInfo</name><argumentList>"
    "<argument><name>Source</name><direction>out</direction>"
    "<relatedStateVariable>SourceProtocolInfo</relatedStateVariable></argument>"
    "<argument><name>Sink</name><direction>out</direction>"
    "<relatedStateVariable>SinkProtocolInfo</relatedStateVariable></argument>"
    "</argumentList></action>"
    "<action><name>GetCurrentConnectionIDs</name><argumentList>"
    "<argument><name>ConnectionIDs</name><direction>out</direction>"
    "<relatedStateVariable>CurrentConnectionIDs</relatedStateVariable></argument>"
    "</argumentList></action>"
    "<action><name>GetCurrentConnectionInfo</name><argumentList>"
    "<argument><name>ConnectionID</name><direction>in</direction>"
    "<relatedStateVariable>A_ARG_TYPE_ConnectionID</relatedStateVariable></argument>"
    "<argument><name>RcsID</name><direction>out</direction>"
    "<relatedStateVariable>A_ARG_TYPE_RcsID</relatedStateVariable></argument>"
    "<argument><name>AVTransportID</name><direction>out</direction>"
    "<relatedStateVariable>A_ARG_TYPE_AVTransportID</relatedStateVariable></argument>"
    "<argument><name>ProtocolInfo</name><direction>out</direction>"
    "<relatedStateVariable>A_ARG_TYPE_ProtocolInfo</relatedStateVariable></argument>"
    "<argument><name>PeerConnectionManager</name><direction>out</direction>"
    "<relatedStateVariable>A_ARG_TYPE_ConnectionManager</relatedStateVariable></argument>"
    "<argument><name>PeerConnectionID</name><direction>out</direction>"
    "<relatedStateVariable>A_ARG_TYPE_ConnectionID</relatedStateVariable></argument>"
    "<argument><name>Direction</name><direction>out</direction>"
    "<relatedStateVariable>A_ARG_TYPE_Direction</relatedStateVariable></argument>"
    "<argument><name>Status</name><direction>out</direction>"
    "<relatedStateVariable>A_ARG_TYPE_ConnectionStatus</relatedStateVariable></argument>"
    "</argumentList></action>"
    "</actionList>"
    "<serviceStateTable>"
    "<stateVariable sendEvents=\"yes\"><name>SourceProtocolInfo</name><dataType>string</dataType></stateVariable>"
    "<stateVariable sendEvents=\"yes\"><name>SinkProtocolInfo</name><dataType>string</dataType></stateVariable>"
    "<stateVariable sendEvents=\"yes\"><name>CurrentConnectionIDs</name><dataType>string</dataType></stateVariable>"
    "<stateVariable sendEvents=\"no\"><name>A_ARG_TYPE_ConnectionID</name><dataType>i4</dataType></stateVariable>"
    "<stateVariable sendEvents=\"no\"><name>A_ARG_TYPE_RcsID</name><dataType>i4</dataType></stateVariable>"
    "<stateVariable sendEvents=\"no\"><name>A_ARG_TYPE_AVTransportID</name><dataType>i4</dataType></stateVariable>"
    "<stateVariable sendEvents=\"no\"><name>A_ARG_TYPE_ProtocolInfo</name><dataType>string</dataType></stateVariable>"
    "<stateVariable sendEvents=\"no\"><name>A_ARG_TYPE_ConnectionManager</name><dataType>string</dataType></stateVariable>"
    "<stateVariable sendEvents=\"no\"><name>A_ARG_TYPE_Direction</name><dataType>string</dataType></stateVariable>"
    "<stateVariable sendEvents=\"no\"><name>A_ARG_TYPE_ConnectionStatus</name><dataType>string</dataType></stateVariable>"
    "</serviceStateTable>"
    "</scpd>\r\n";

void dlna_cd_scpd_xml(const char **out, size_t *out_len) {
  *out = CD_SCPD;
  *out_len = sizeof(CD_SCPD) - 1;
}

void dlna_cm_scpd_xml(const char **out, size_t *out_len) {
  *out = CM_SCPD;
  *out_len = sizeof(CM_SCPD) - 1;
}
