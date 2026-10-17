/* Copyright 2026 dvbipitools authors. Licensed under GPL-3.0-or-later.
 * See NOTICE and LICENSE for details and authorship information. */

#ifndef LIB_TVA_TVA_XML_TVA_XML_INT_H
#define LIB_TVA_TVA_XML_TVA_XML_INT_H

#include <ctype.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>
#include "lib/sys/ioutil.h"
#include "lib/helper/log.h"
#include "lib/helper/xml_util.h"
#include "lib/tva/tva_xml.h"

typedef struct {
  const char *crid;
  int idx;
} progtext_idx_t;

typedef struct {
  bcg_progtext_t *items;
  int n, cap;
  progtext_idx_t *idx; /* sorted by crid, built once after parse_program_texts */
} progtext_list_t;

void iso8601_compact_prefix(const char *iso, char *out, size_t outcap);
int iso8601_utc_compact(const char *iso, char *out, size_t outcap);
int progtext_list_build_index(progtext_list_t *pl);
bcg_progtext_t *find_progtext(const progtext_list_t *pl, const char *crid);
int parse_program_texts(const char *buf, const char *end, progtext_list_t *pl);
int parse_service_information(const char *buf, const char *end, bcg_doc_t *doc);
void trim_ws(char *s);
int duration_to_secs(const char *s, long *out);
int iso8601_add_secs(const char *start_iso, long secs, char *out, size_t outcap);

#endif
