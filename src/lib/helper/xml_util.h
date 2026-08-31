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

/* called once per block: [tag,blk_end) spans "<open_tag...>...</close_tag", blk_end at close_tag's '<'. -1 aborts scan */
typedef int (*xml_block_cb)(const char *tag, const char *blk_end, void *ctx);

/* scan [buf,end) for non-overlapping open_tag/close_tag block pairs, in order.
   0 ok, even with no matches or a truncated trailing block. -1 if cb returned -1 */
int for_each_xml_block(const char *buf, const char *end, const char *open_tag, const char *close_tag, xml_block_cb cb, void *ctx);

#endif
