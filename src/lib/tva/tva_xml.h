/* Copyright 2026 dvbipitools authors. Licensed under GPL-3.0-or-later.
 * See NOTICE and LICENSE for details and authorship information. */

#ifndef DVBIPITOOLS_LIB_TVA_TVA_XML_H
#define DVBIPITOOLS_LIB_TVA_TVA_XML_H

#include <stdio.h>

#include "bcg_doc.h"

/* ContentCS 2011 term href for free-text category (case-insensitive). unknown text maps to 3.0 (proprietary) */
const char *tva_genre_href(const char *category);

#define TVA_CRID_BUF (BCG_ID_LEN * 3 + 64)

/* crid://dipixmltv.invalid/<percent-encoded-channel-id>/<14-digit-timestamp> */
void tva_build_crid(const char *channel_id, const char *start_iso, char *out, size_t outcap);

/* caches crid in pr->crid. 0 ok, -1 OOM (pr unchanged) */
int tva_programme_cache_crid(bcg_programme_t *pr);

/* pr->crid if cached, else built into buf (cap >= TVA_CRID_BUF) */
const char *tva_programme_crid(const bcg_programme_t *pr, char *buf, size_t cap);

/* channels with no uri are dropped, along with their programmes */
void tva_xml_write(FILE *f, const bcg_doc_t *doc);

/* 0 ok, -1 error on stderr */
int tva_xml_read(FILE *f, bcg_doc_t *doc);

#endif
