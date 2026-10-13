/* Copyright 2026 dvbipitools authors. Licensed under GPL-3.0-or-later.
 * See NOTICE and LICENSE for details and authorship information. */

#include <stdlib.h>
#include <string.h>

#include "lib/helper/beutil.h"

#include "container.h"

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

int container_build(const unsigned char *access_unit, size_t au_len, const unsigned char *string_repo, size_t sr_len, const accessunit_ref_t *refs, int nrefs, unsigned char **out, size_t *out_len) {
  size_t enc_len = nrefs > 0 ? ENCAP_HEADER_LEN + (size_t)nrefs * ENCAP_ENTRY_LEN : 0;
  size_t nstruct = nrefs > 0 ? 3 : 2;
  size_t header_len = 1 + nstruct * STRUCT_ENTRY_LEN;
  size_t au_offset;
  size_t sr_offset;
  size_t total;
  unsigned char *buf;
  unsigned char *p;

  if (au_len > UINT24_MAX || sr_len > UINT24_MAX || enc_len > UINT24_MAX)
    return -1;
  for (int i = 0; i < nrefs; i++) {
    if (refs[i].au_off > UINT16_MAX || refs[i].str_off > UINT16_MAX)
      return -1;
  }
  au_offset = header_len + enc_len;
  sr_offset = au_offset + au_len;
  if (sr_offset > UINT24_MAX)
    return -1;

  total = sr_offset + sr_len;
  buf = malloc(total);
  if (!buf)
    return -1;

  buf[0] = (unsigned char)nstruct;
  p = buf + 1;
  if (nrefs > 0) {
    put_struct(p, CONTAINER_STRUCT_ENCAPSULATION, CONTAINER_ID_UNUSED, header_len, enc_len);
    p += STRUCT_ENTRY_LEN;
  }
  put_struct(p, CONTAINER_STRUCT_DATA_REPOSITORY, CONTAINER_DATAREPO_BINARY, au_offset, au_len);
  put_struct(p + STRUCT_ENTRY_LEN, CONTAINER_STRUCT_DATA_REPOSITORY, CONTAINER_DATAREPO_STRINGS, sr_offset, sr_len);

  if (nrefs > 0) {
    p = buf + header_len;
    p[0] = ENCAP_RESERVED_BYTE;
    p[1] = CONTAINER_FRAGREF_DVB_BIM;
    p += ENCAP_HEADER_LEN;
    for (int i = 0; i < nrefs; i++, p += ENCAP_ENTRY_LEN) {
      be16_put(p, (uint16_t)refs[i].au_off);
      be16_put(p + 2, (uint16_t)refs[i].str_off);
      p[4] = 0;
      be24_put(p + 5, (uint32_t)i + 1);
    }
  }

  memcpy(buf + au_offset, access_unit, au_len);
  memcpy(buf + sr_offset, string_repo, sr_len);

  *out = buf;
  *out_len = total;
  return 0;
}

/* last structure of type/id wins. 0 found, 1 absent, -1 malformed table */
static int find_struct(const unsigned char *buf, size_t len, unsigned want_type, unsigned want_id, const unsigned char **body, size_t *body_len) {
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

int container_parse(const unsigned char *buf, size_t len, const unsigned char **au, size_t *au_len, const unsigned char **sr, size_t *sr_len) {
  *au = NULL;
  *sr = NULL;
  if (find_struct(buf, len, CONTAINER_STRUCT_DATA_REPOSITORY, CONTAINER_DATAREPO_BINARY, au, au_len) < 0)
    return -1;
  if (find_struct(buf, len, CONTAINER_STRUCT_DATA_REPOSITORY, CONTAINER_DATAREPO_STRINGS, sr, sr_len) < 0)
    return -1;
  return (*au && *sr) ? 0 : -1;
}

int container_parse_refs(const unsigned char *buf, size_t len, accessunit_ref_t **refs, int *nrefs) {
  const unsigned char *enc = NULL;
  size_t enc_len = 0;
  size_t n;
  int rc;

  *refs = NULL;
  *nrefs = 0;
  rc = find_struct(buf, len, CONTAINER_STRUCT_ENCAPSULATION, CONTAINER_ID_UNUSED, &enc, &enc_len);
  if (rc != 0)
    return rc < 0 ? -1 : 0;
  if (enc_len < ENCAP_HEADER_LEN || enc[1] != CONTAINER_FRAGREF_DVB_BIM)
    return 0;
  if ((enc_len - ENCAP_HEADER_LEN) % ENCAP_ENTRY_LEN)
    return -1;
  n = (enc_len - ENCAP_HEADER_LEN) / ENCAP_ENTRY_LEN;
  if (n == 0)
    return 0;
  *refs = malloc(n * sizeof **refs);
  if (!*refs)
    return -1;
  for (size_t i = 0; i < n; i++) {
    const unsigned char *e = enc + ENCAP_HEADER_LEN + i * ENCAP_ENTRY_LEN;
    (*refs)[i].au_off = be16_get(e);
    (*refs)[i].str_off = be16_get(e + 2);
  }
  *nrefs = (int)n;
  return 0;
}
