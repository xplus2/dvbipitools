/* Copyright 2026 dvbipitools authors. Licensed under GPL-3.0-or-later.
 * See NOTICE and LICENSE for details and authorship information. */

#ifndef DVBIPITOOLS_LIB_HELPER_XML_UTIL_H
#define DVBIPITOOLS_LIB_HELPER_XML_UTIL_H

#include <stddef.h>
#include <stdio.h>

/* &<>"' -> entities */
void xml_escape(FILE *f, const char *s);

/* name="v" or name='v' (spaces around '=' ok, prefix ignored), first start tag in [s,end) having it. value decoded.
   0 ok, -1 not found */
int xml_attr(const char *s, const char *end, const char *name, char *out, size_t outcap);

/* as xml_attr, *truncated (if non-NULL) set to 1 when value did not fit outcap, else 0. untouched on -1 */
int xml_attr_chk(const char *s, const char *end, const char *name, char *out, size_t outcap, int *truncated);

/* as xml_attr, start tag at s only (s at its '<') */
int xml_tag_attr(const char *s, const char *end, const char *name, char *out, size_t outcap);

/* as xml_tag_attr, *truncated as in xml_attr_chk */
int xml_tag_attr_chk(const char *s, const char *end, const char *name, char *out, size_t outcap, int *truncated);

/* as xml_attr on a bare attribute list (no tag), e.g. M3U #EXTINF text */
int xml_attr_list(const char *s, const char *end, const char *name, char *out, size_t outcap);

/* text of first element tag (prefix ignored) in [s,end): text and CDATA, entities decoded, child markup dropped.
   0 ok, -1 not found, self-closing or unclosed */
int xml_elem_text(const char *s, const char *end, const char *tag, char *out, size_t outcap);

/* as xml_elem_text, *truncated (if non-NULL) set to 1 when text did not fit outcap (cut on a UTF-8 boundary), else 0.
   untouched on -1 */
int xml_elem_text_chk(const char *s, const char *end, const char *tag, char *out, size_t outcap, int *truncated);

/* element span: tag at start tag '<', end at close tag '<' or at '/' of "/>" if empty */
typedef struct {
  const char *tag;
  const char *end;
} xml_span_t;

/* as xml_elem_text_chk for element [tag,blk_end) itself. -1 if empty or not an element */
int xml_span_text_chk(const char *tag, const char *blk_end, char *out, size_t outcap, int *truncated);

/* first element name (prefix ignored) in [s,end). 0 ok, -1 not found or unclosed */
int xml_find_elem(const char *s, const char *end, const char *name, xml_span_t *sp);

/* '<' of first start tag name (prefix ignored) in [s,end), close tag not needed. NULL if none */
const char *xml_find_start(const char *s, const char *end, const char *name);

/* called once per element: [tag,blk_end) as in xml_span_t. -1 aborts scan */
typedef int (*xml_block_cb)(const char *tag, const char *blk_end, void *ctx);

/* elements name (prefix ignored) in [buf,end), in order, same-name nesting skipped.
   0 ok, even with no matches or a truncated trailing element. -1 if cb returned -1 */
int for_each_xml_elem(const char *buf, const char *end, const char *name, xml_block_cb cb, void *ctx);

#endif
