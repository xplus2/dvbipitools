/* Copyright 2026 dvbipitools authors. Licensed under GPL-3.0-or-later.
 * See NOTICE and LICENSE for details and authorship information. */

#ifndef DIPISDS_FORMAT_OUT_H
#define DIPISDS_FORMAT_OUT_H

#include <stdio.h>

#include "args.h"
#include "sds_xml.h"

void format_out_init(FILE *f, out_fmt_t fmt, const char *invocation);
void format_out_item(FILE *f, out_fmt_t fmt, const sds_service_t *s);
void format_out_close(FILE *f, out_fmt_t fmt);

/* OUT_XML only: dump one reassembled document as-is */
void format_out_raw(FILE *f, out_fmt_t fmt, const unsigned char *data, size_t len);

#endif
