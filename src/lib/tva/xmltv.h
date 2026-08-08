/* Copyright 2026 dvbipitools authors. Licensed under GPL-3.0-or-later.
 * See NOTICE and LICENSE for details and authorship information. */

#ifndef LIB_TVA_XMLTV_H
#define LIB_TVA_XMLTV_H

#include <stdio.h>

#include "bcg_doc.h"

/* 0 ok, -1 error on stderr */
int xmltv_read(FILE *f, bcg_doc_t *doc);
/* generator_name -> <tv generator-info-name="..."> */
void xmltv_write(FILE *f, const bcg_doc_t *doc, const char *generator_name);

#endif
