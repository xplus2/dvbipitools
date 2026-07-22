/* Copyright 2026 dvbipitools authors. Licensed under GPL-3.0-or-later.
 * See NOTICE and LICENSE for details and authorship information. */

#ifndef DIPIREC_XML_UTIL_H
#define DIPIREC_XML_UTIL_H

#include <stddef.h>
#include <stdio.h>

/* &<>"' -> entities */
void xml_escape(FILE *f, const char *s);

/* name="..." bounded to [s,end). 0 ok, -1 not found */
int xml_attr(const char *s, const char *end, const char *name, char *out, size_t outcap);

/* <tag ...>TEXT</tag> bounded to [s,end), entities decoded. 0 ok, -1 not found */
int xml_elem_text(const char *s, const char *end, const char *tag, char *out, size_t outcap);

#endif
