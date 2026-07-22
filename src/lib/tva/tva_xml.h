/* Copyright 2026 dvbipitools authors. Licensed under GPL-3.0-or-later.
 * See NOTICE and LICENSE for details and authorship information. */

#ifndef LIB_TVA_XML_H
#define LIB_TVA_XML_H

#include <stdio.h>

#include "epg_doc.h"

/* crid://dipixmltv.invalid/<percent-encoded-channel-id>/<14-digit-timestamp> */
void tva_build_crid(const char *channel_id, const char *start_iso, char *out, size_t outcap);

/* channels with no uri are dropped, along with their programmes */
void tva_xml_write(FILE *f, const epg_doc_t *doc);

/* 0 ok, -1 error on stderr */
int tva_xml_read(FILE *f, epg_doc_t *doc);

#endif
