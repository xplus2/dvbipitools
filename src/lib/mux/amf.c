/* Copyright 2026 dvbipitools authors. Licensed under GPL-3.0-or-later.
 * See NOTICE and LICENSE for details and authorship information. */

#include <stddef.h>
#include <stdint.h>
#include <string.h>

#include "../beutil.h"
#include "amf.h"

#define AMF_T_NUMBER 0x00
#define AMF_T_BOOLEAN 0x01
#define AMF_T_STRING 0x02
#define AMF_T_OBJECT 0x03
#define AMF_T_NULL 0x05
#define AMF_T_UNDEFINED 0x06
#define AMF_T_ECMA_ARRAY 0x08
#define AMF_OBJECT_END 0x09
#define AMF_T_STRICT_ARRAY 0x0A
#define AMF_T_DATE 0x0B
#define AMF_T_LONG_STRING 0x0C

static void amf_be16(ebuf_t *b, unsigned v) {
  unsigned char t[2];
  be16_put(t, (uint16_t)v);
  eb_bytes(b, t, 2);
}

static void amf_be32(ebuf_t *b, unsigned v) {
  unsigned char t[4];
  be32_put(t, (uint32_t)v);
  eb_bytes(b, t, 4);
}

/* AMF0 short string: UI16 len + bytes, no marker. String body + Object/ECMA-array keys */
static void amf_shortstr(ebuf_t *b, const char *s) {
  size_t n = strlen(s);
  if (n > 0xFFFF)
    n = 0xFFFF;
  amf_be16(b, (unsigned)n);
  eb_bytes(b, s, n);
}

void amf_number(ebuf_t *b, double v) {
  union {
    double d;
    uint64_t u;
  } c;
  unsigned char m = AMF_T_NUMBER, t[8];
  c.d = v;
  eb_bytes(b, &m, 1);
  for (int i = 0; i < 8; i++)
    t[i] = (unsigned char)(c.u >> (8 * (7 - i)));
  eb_bytes(b, t, 8);
}

void amf_boolean(ebuf_t *b, int v) {
  unsigned char t[2] = {AMF_T_BOOLEAN, v ? 1 : 0};
  eb_bytes(b, t, 2);
}

void amf_string(ebuf_t *b, const char *s) {
  unsigned char m = AMF_T_STRING;
  eb_bytes(b, &m, 1);
  amf_shortstr(b, s);
}

void amf_null(ebuf_t *b) {
  unsigned char m = AMF_T_NULL;
  eb_bytes(b, &m, 1);
}

void amf_object_start(ebuf_t *b) {
  unsigned char m = AMF_T_OBJECT;
  eb_bytes(b, &m, 1);
}

void amf_object_key(ebuf_t *b, const char *key) {
  amf_shortstr(b, key);
}

void amf_object_end(ebuf_t *b) {
  unsigned char t[3] = {0x00, 0x00, AMF_OBJECT_END};
  eb_bytes(b, t, 3);
}

void amf_ecma_array_start(ebuf_t *b, unsigned approx_count) {
  unsigned char m = AMF_T_ECMA_ARRAY;
  eb_bytes(b, &m, 1);
  amf_be32(b, approx_count);
}

void amf_ecma_array_end(ebuf_t *b) {
  amf_object_end(b); /* same 00 00 09 terminator as Object */
}

const unsigned char *amf_read_number(const unsigned char *p, const unsigned char *end, double *v) {
  union {
    double d;
    uint64_t u;
  } c;
  if (end - p < 9 || p[0] != AMF_T_NUMBER)
    return NULL;
  c.u = 0;
  for (int i = 0; i < 8; i++)
    c.u = (c.u << 8) | p[1 + i];
  *v = c.d;
  return p + 9;
}

const unsigned char *amf_read_string(const unsigned char *p, const unsigned char *end, char *out, size_t cap) {
  unsigned n;
  if (end - p < 3 || p[0] != AMF_T_STRING)
    return NULL;
  n = be16_get(p + 1);
  if (end - (p + 3) < (ptrdiff_t)n)
    return NULL;
  if (cap) {
    size_t cn = n < cap - 1 ? n : cap - 1;
    memcpy(out, p + 3, cn);
    out[cn] = '\0';
  }
  return p + 3 + n;
}

/* object/ecma-array body: key/value pairs until 00 00 09 terminator */
static const unsigned char *amf_skip_props(const unsigned char *p, const unsigned char *end) {
  for (;;) {
    unsigned keylen;
    if (end - p < 2)
      return NULL;
    keylen = be16_get(p);
    if (0 == keylen) {
      if (end - p < 3 || p[2] != AMF_OBJECT_END)
        return NULL;
      return p + 3;
    }
    p += 2;
    if (end - p < (ptrdiff_t)keylen)
      return NULL;
    p += keylen;
    p = amf_skip_value(p, end);
    if (!p)
      return NULL;
  }
}

const unsigned char *amf_skip_value(const unsigned char *p, const unsigned char *end) {
  if (end - p < 1)
    return NULL;
  switch (p[0]) {
    case AMF_T_NUMBER:
      return end - p < 9 ? NULL : p + 9;
    case AMF_T_BOOLEAN:
      return end - p < 2 ? NULL : p + 2;
    case AMF_T_STRING:
      if (end - p < 3)
        return NULL;
      return amf_read_string(p, end, NULL, 0);
    case AMF_T_OBJECT:
      return amf_skip_props(p + 1, end);
    case AMF_T_NULL:
    case AMF_T_UNDEFINED:
      return p + 1;
    case AMF_T_ECMA_ARRAY:
      return end - p < 5 ? NULL : amf_skip_props(p + 5, end);
    case AMF_T_STRICT_ARRAY:
      if (end - p < 5)
        return NULL;
      uint32_t count = be32_get(p + 1);
      p += 5;
      for (uint32_t i = 0; i < count; i++) {
        p = amf_skip_value(p, end);
        if (!p)
          return NULL;
      }
      return p;
    case AMF_T_DATE:
      return end - p < 11 ? NULL : p + 11;
    case AMF_T_LONG_STRING:
      if (end - p < 5)
        return NULL;
      return p + 5 + be32_get(p + 1);
    default:
      return NULL; /* unknown/unsupported marker: can't safely skip */
  }
}

int amf_object_find_string(const unsigned char *p, const unsigned char *end, const char *key, char *out, size_t cap) {
  size_t keylen = strlen(key);
  if (end - p < 1 || p[0] != AMF_T_OBJECT)
    return -1;
  p++;
  for (;;) {
    unsigned klen;
    if (end - p < 2)
      return -1;
    klen = be16_get(p);
    if (0 == klen) {
      if (end - p < 3 || p[2] != AMF_OBJECT_END)
        return -1;
      return 0;
    }
    p += 2;
    if (end - p < (ptrdiff_t)klen)
      return -1;
    if (klen == keylen && 0 == memcmp(p, key, klen))
      return amf_read_string(p + klen, end, out, cap) ? 1 : -1;
    p += klen;
    p = amf_skip_value(p, end);
    if (!p)
      return -1;
  }
}
