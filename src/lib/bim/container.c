/* Copyright 2026 dvbipitools authors. Licensed under GPL-3.0-or-later.
 * See NOTICE and LICENSE for details and authorship information. */

#include <stdlib.h>
#include <string.h>

#include "lib/helper/beutil.h"

#include "lib/bim/container.h"

#define STRUCT_ENTRY_LEN 8 /* type, id, ptr24, length24 */
#define UINT24_MAX 0xFFFFFFu
#define ENCAP_HEADER_LEN 2
#define ENCAP_ENTRY_LEN 8 /* BiM_fragment_ptr, external_string_buffer_ptr, version, fragment_id24 */
#define ENCAP_RESERVED_BYTE 0xFF /* reserved_other_use 11, reserved all ones */

static void put_struct(unsigned char *p, unsigned type, unsigned id, size_t ptr, size_t length) {
  p[0] = (unsigned char)type;
  p[1] = (unsigned char)id;
  be24_put(p + 2, (uint32_t)ptr);
  be24_put(p + 5, (uint32_t)length);
}

int bim_container_build_structs(const bim_struct_t *st, int n, unsigned char **out, size_t *out_len) {
  size_t off = 1 + (size_t)n * STRUCT_ENTRY_LEN;
  unsigned char *buf;
  size_t total = off;
  if (n < 1 || n > 0xFF) return -1;
  for (int i = 0; i < n; i++) {
    if (st[i].len > UINT24_MAX) return -1;
    total += st[i].len;
  }
  if (total > UINT24_MAX) return -1;
  buf = malloc(total);
  if (!buf) return -1;
  buf[0] = (unsigned char)n;
  for (int i = 0; i < n; i++) {
    put_struct(buf + 1 + (size_t)i * STRUCT_ENTRY_LEN, st[i].type, st[i].id, off, st[i].len);
    if (st[i].len) memcpy(buf + off, st[i].body, st[i].len);
    off += st[i].len;
  }
  *out = buf;
  *out_len = total;
  return 0;
}

static size_t entry_len(unsigned format) { return format == BIM_FRAGREF_DVB_BIM ? ENCAP_ENTRY_LEN : ENCAP_ENTRY_LEN - 2; }

int bim_container_build(unsigned format, const unsigned char *access_unit, size_t au_len, const unsigned char *string_repo, size_t sr_len, const bim_fragref_t *refs, int nrefs, unsigned char **out, size_t *out_len) {
  size_t el = entry_len(format);
  size_t enc_len = nrefs > 0 ? ENCAP_HEADER_LEN + (size_t)nrefs * el : 0;
  unsigned char *enc = NULL;
  bim_struct_t st[3];
  int n = 0;
  int rc;

  if (format != BIM_FRAGREF_BIM && format != BIM_FRAGREF_DVB_BIM) return -1;
  for (int i = 0; i < nrefs; i++) {
    if (refs[i].au_off > UINT16_MAX || (format == BIM_FRAGREF_DVB_BIM && refs[i].str_off > UINT16_MAX))
      return -1;
  }
  if (nrefs > 0) {
    unsigned char *p;
    if (enc_len > UINT24_MAX) return -1;
    enc = malloc(enc_len);
    if (!enc) return -1;
    enc[0] = ENCAP_RESERVED_BYTE;
    enc[1] = (unsigned char)format;
    p = enc + ENCAP_HEADER_LEN;
    for (int i = 0; i < nrefs; i++, p += el) {
      unsigned char *q = p;
      be16_put(q, (uint16_t)refs[i].au_off);
      q += 2;
      if (format == BIM_FRAGREF_DVB_BIM) {
        be16_put(q, (uint16_t)refs[i].str_off);
        q += 2;
      }
      q[0] = refs[i].version;
      be24_put(q + 1, refs[i].frag_id);
    }
    st[n++] = (bim_struct_t){BIM_STRUCT_ENCAPSULATION, BIM_STRUCT_ID_UNUSED, enc, enc_len};
  }
  if (string_repo) st[n++] = (bim_struct_t){BIM_STRUCT_DATA_REPOSITORY, BIM_DATAREPO_STRINGS, (unsigned char *)string_repo, sr_len};
  st[n++] = (bim_struct_t){BIM_STRUCT_DATA_REPOSITORY, BIM_DATAREPO_BINARY, (unsigned char *)access_unit, au_len};
  rc = bim_container_build_structs(st, n, out, out_len);
  free(enc);
  return rc;
}

int bim_container_find(const unsigned char *buf, size_t len, unsigned want_type, unsigned want_id, const unsigned char **body, size_t *body_len) {
  unsigned num_structures;
  const unsigned char *p;
  int found = 1;

  if (len < 1)
    return -1;
  num_structures = buf[0];
  if (len < 1 + (size_t)num_structures * STRUCT_ENTRY_LEN)
    return -1;

  p = buf + 1;
  for (unsigned i = 0; i < num_structures; i++, p += STRUCT_ENTRY_LEN) {
    unsigned type = p[0], id = p[1];
    size_t ptr = be24_get(p + 2), length = be24_get(p + 5);
    if (ptr > len || length > len - ptr)
      return -1;
    if (type == want_type && id == want_id) {
      *body = buf + ptr;
      *body_len = length;
      found = 0;
    }
  }
  return found;
}

int bim_container_parse(const unsigned char *buf, size_t len, const unsigned char **au, size_t *au_len, const unsigned char **sr, size_t *sr_len) {
  *au = NULL;
  *sr = NULL;
  *sr_len = 0;
  if (bim_container_find(buf, len, BIM_STRUCT_DATA_REPOSITORY, BIM_DATAREPO_BINARY, au, au_len) < 0)
    return -1;
  if (bim_container_find(buf, len, BIM_STRUCT_DATA_REPOSITORY, BIM_DATAREPO_STRINGS, sr, sr_len) < 0)
    return -1;
  return *au ? 0 : -1;
}

int bim_container_parse_refs(const unsigned char *buf, size_t len, unsigned *format, bim_fragref_t **refs, int *nrefs) {
  const unsigned char *enc = NULL;
  size_t enc_len = 0;
  size_t n;
  size_t el;
  int rc;

  *refs = NULL;
  *nrefs = 0;
  *format = 0;
  rc = bim_container_find(buf, len, BIM_STRUCT_ENCAPSULATION, BIM_STRUCT_ID_UNUSED, &enc, &enc_len);
  if (rc != 0)
    return rc < 0 ? -1 : 0;
  if (enc_len < ENCAP_HEADER_LEN)
    return -1;
  *format = enc[1];
  if (*format != BIM_FRAGREF_BIM && *format != BIM_FRAGREF_DVB_BIM)
    return 0;
  el = entry_len(*format);
  if ((enc_len - ENCAP_HEADER_LEN) % el)
    return -1;
  n = (enc_len - ENCAP_HEADER_LEN) / el;
  if (n == 0)
    return 0;
  *refs = malloc(n * sizeof **refs);
  if (!*refs)
    return -1;
  for (size_t i = 0; i < n; i++) {
    const unsigned char *e = enc + ENCAP_HEADER_LEN + i * el;
    (*refs)[i].au_off = be16_get(e);
    (*refs)[i].str_off = *format == BIM_FRAGREF_DVB_BIM ? be16_get(e + 2) : 0;
    e += *format == BIM_FRAGREF_DVB_BIM ? 4 : 2;
    (*refs)[i].version = e[0];
    (*refs)[i].frag_id = be24_get(e + 1);
  }
  *nrefs = (int)n;
  return 0;
}
