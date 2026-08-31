/* Copyright 2026 dvbipitools authors. Licensed under GPL-3.0-or-later.
 * See NOTICE and LICENSE for details and authorship information. */

#ifndef DIPIXY_DLNA_DLNA_H
#define DIPIXY_DLNA_DLNA_H

#include <stddef.h>

#include "../args.h"
#include "../ts/channels/channels.h"

/* out/out_len point into static thread-local buf, until this thread's next call, caller must not free. 0 ok, -1 on error */
int dlna_device_desc_xml(const config_t *cfg, char **out, size_t *out_len);

/* points to a static compile-time string, caller must not free */
void dlna_cd_scpd_xml(const char **out, size_t *out_len);
void dlna_cm_scpd_xml(const char **out, size_t *out_len);

/* "urn:...:service:ContentDirectory:1#Browse" = Browse. 0 ok, -1 malformed/missing */
int dlna_soap_action_name(const char *soapaction_header, char *out, size_t outsz);

/* fully-buffered SOAP body. service: cd/cm. out/out_len point to persistent thread-local buff, until this thread's next call, caller must not free.
   return HTTP status: 200 / 500 */
int dlna_handle_control(const config_t *cfg, const channels_t *channels, const char *service, const char *action, const char *body, size_t body_len, char **out, size_t *out_len);

/* CM::GetProtocolInfo's "Source" field: every protocolInfo string cfg can currently serve. reused by GENA initial cm-service NOTIFY */
void dlna_source_protocol_info(const config_t *cfg, char *out, size_t outsz);

#endif
