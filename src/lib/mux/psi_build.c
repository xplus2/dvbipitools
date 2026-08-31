/* Copyright 2026 dvbipitools authors. Licensed under GPL-3.0-or-later.
 * See NOTICE and LICENSE for details and authorship information. */

#include <stdint.h>
#include <string.h>

#include "../helper/beutil.h"
#include "../demux/crc32.h"

#include "psi_build.h"

void psi_put16(unsigned char *p, unsigned v) { be16_put(p, (uint16_t)v); }

size_t psi_utf8_clamp(const char *s, size_t len, size_t max_bytes) {
  size_t i = 0;
  if (len <= max_bytes)
    return len;
  while (i < max_bytes) {
    unsigned char b = (unsigned char)s[i];
    size_t seqlen = (b < 0x80) ? 1 : ((b & 0xE0) == 0xC0) ? 2 : ((b & 0xF0) == 0xE0) ? 3 : ((b & 0xF8) == 0xF0) ? 4 : 1;
    if (i + seqlen > max_bytes)
      break;
    i += seqlen;
  }
  return i;
}

size_t psi_put_text(unsigned char *out, size_t cap, const char *s) {
  size_t len;
  if (cap < 1)
    return 0;
  len = psi_utf8_clamp(s, strlen(s), cap - 1);
  out[0] = 0x15;
  memcpy(out + 1, s, len);
  return len + 1;
}

size_t psi_finish_section(unsigned char *out, size_t len, size_t cap, unsigned char flags_nibble) {
  uint32_t crc;
  unsigned seclen;

  if (len + 4 > cap)
    return 0;
  seclen = (unsigned)(len + 4 - 3); /* section_length is itself CRC-protected, patch before hashing */
  out[1] = (unsigned char)(flags_nibble | ((seclen >> 8) & 0x0F));
  out[2] = (unsigned char)seclen;
  crc = crc32_mpeg(out, len);
  be32_put(out + len, crc);
  len += 4;
  return len;
}

size_t psi_build_pat(unsigned tsid, unsigned version, unsigned program_number, unsigned pmt_pid, unsigned char *out, size_t cap) {
  size_t n = 0;

  if (cap < 16)
    return 0;
  out[n++] = 0x00;
  n += 2;
  psi_put16(out + n, tsid);
  n += 2;
  out[n++] = (unsigned char)(0xC0 | ((version & 0x1F) << 1) | 0x01);
  out[n++] = 0x00;
  out[n++] = 0x00;
  psi_put16(out + n, program_number);
  n += 2;
  psi_put16(out + n, 0xE000 | (pmt_pid & 0x1FFF));
  n += 2;

  return psi_finish_section(out, n, cap, 0xB0);
}

size_t psi_build_pat_multi(unsigned tsid, unsigned version, const psi_pat_entry_t *programs, size_t n_programs, unsigned char *out, size_t cap) {
  size_t n = 0, i;

  if (cap < 12 + n_programs * 4)
    return 0;
  out[n++] = 0x00;
  n += 2;
  psi_put16(out + n, tsid);
  n += 2;
  out[n++] = (unsigned char)(0xC0 | ((version & 0x1F) << 1) | 0x01);
  out[n++] = 0x00;
  out[n++] = 0x00;
  for (i = 0; i < n_programs; i++) {
    psi_put16(out + n, programs[i].program_number);
    n += 2;
    psi_put16(out + n, 0xE000 | (programs[i].pmt_pid & 0x1FFF));
    n += 2;
  }

  return psi_finish_section(out, n, cap, 0xB0);
}

size_t psi_build_cat(unsigned version, const unsigned char *desc, size_t desc_len, unsigned char *out, size_t cap) {
  size_t n = 0;

  if (cap < 12)
    return 0;
  out[n++] = 0x01;
  n += 2;
  psi_put16(out + n, 0xFFFF);
  n += 2;
  out[n++] = (unsigned char)(0xC0 | ((version & 0x1F) << 1) | 0x01);
  out[n++] = 0x00;
  out[n++] = 0x00;
  if (desc_len) {
    if (n + desc_len > cap)
      return 0;
    memcpy(out + n, desc, desc_len);
    n += desc_len;
  }
  return psi_finish_section(out, n, cap, 0xB0);
}

size_t psi_build_sdt(unsigned version, unsigned tsid, unsigned onid, unsigned service_id, unsigned service_type, const char *provider, const char *service, unsigned char *out, size_t cap) {
  size_t n = 0, f16_pos, desc_start, dlen_pos, plen_pos, slen_pos, plen, slen;
  unsigned dll, field16;

  if (cap < 24)
    return 0;
  out[n++] = 0x42;
  n += 2;
  psi_put16(out + n, tsid);
  n += 2;
  out[n++] = (unsigned char)(0xC0 | ((version & 0x1F) << 1) | 0x01);
  out[n++] = 0x00;
  out[n++] = 0x00;
  psi_put16(out + n, onid);
  n += 2;
  out[n++] = 0xFF;

  psi_put16(out + n, service_id);
  n += 2;
  out[n++] = 0xFD; /* reserved(6)=111111, EIT_schedule=0, EIT_present_following=1 */

  f16_pos = n;
  n += 2;
  desc_start = n;
  out[n++] = 0x48; /* service_descriptor */
  dlen_pos = n;
  n++;
  out[n++] = (unsigned char)service_type;
  plen_pos = n;
  n++;
  plen = psi_put_text(out + n, cap - n, provider);
  n += plen;
  out[plen_pos] = (unsigned char)plen;
  slen_pos = n;
  n++;
  slen = psi_put_text(out + n, cap - n, service);
  n += slen;
  out[slen_pos] = (unsigned char)slen;
  out[dlen_pos] = (unsigned char)(n - (dlen_pos + 1));

  dll = (unsigned)(n - desc_start);
  field16 = ((4u & 0x7) << 13) | (0u << 12) | (dll & 0x0FFF); /* running_status=running, free_CA=0 */
  out[f16_pos] = (unsigned char)(field16 >> 8);
  out[f16_pos + 1] = (unsigned char)field16;
  return psi_finish_section(out, n, cap, 0xF0);
}

size_t psi_build_sdt_multi(unsigned version, unsigned tsid, unsigned onid, const psi_sdt_entry_t *services, size_t n_services, unsigned char *out, size_t cap) {
  size_t n = 0, i;

  if (cap < 12 || n_services == 0)
    return 0;
  out[n++] = 0x42;
  n += 2;
  psi_put16(out + n, tsid);
  n += 2;
  out[n++] = (unsigned char)(0xC0 | ((version & 0x1F) << 1) | 0x01);
  out[n++] = 0x00;
  out[n++] = 0x00;
  psi_put16(out + n, onid);
  n += 2;
  out[n++] = 0xFF;

  for (i = 0; i < n_services; i++) {
    size_t f16_pos, desc_start, dlen_pos, plen_pos, slen_pos, plen, slen;
    unsigned dll, field16;

    if (n + 9 > cap)
      return 0;
    psi_put16(out + n, services[i].service_id);
    n += 2;
    out[n++] = 0xFD; /* reserved(6)=111111, EIT_schedule=0, EIT_present_following=1 */

    f16_pos = n;
    n += 2;
    desc_start = n;
    out[n++] = 0x48; /* service_descriptor */
    dlen_pos = n;
    n++;
    out[n++] = (unsigned char)services[i].service_type;
    plen_pos = n;
    n++;
    plen = psi_put_text(out + n, cap - n, services[i].provider);
    n += plen;
    out[plen_pos] = (unsigned char)plen;
    slen_pos = n;
    n++;
    slen = psi_put_text(out + n, cap - n, services[i].service_name);
    n += slen;
    out[slen_pos] = (unsigned char)slen;
    out[dlen_pos] = (unsigned char)(n - (dlen_pos + 1));

    dll = (unsigned)(n - desc_start);
    field16 = ((4u & 0x7) << 13) | (0u << 12) | (dll & 0x0FFF); /* running_status=running, free_CA=0 */
    out[f16_pos] = (unsigned char)(field16 >> 8);
    out[f16_pos + 1] = (unsigned char)field16;
  }

  return psi_finish_section(out, n, cap, 0xF0);
}

size_t psi_build_nit(unsigned version, unsigned onid, unsigned tsid, const char *network_name, unsigned char *out, size_t cap) {
  size_t n = 0, ndl_pos, nd_start, dlen_pos, nlen, tsl_pos, ts_start;
  unsigned ndl, tsl;
  if (cap < 32)
    return 0;
  out[n++] = 0x40;
  n += 2;
  psi_put16(out + n, onid); /* network_id */
  n += 2;
  out[n++] = (unsigned char)(0xC0 | ((version & 0x1F) << 1) | 0x01);
  out[n++] = 0x00;
  out[n++] = 0x00;

  ndl_pos = n;
  n += 2;
  nd_start = n;
  out[n++] = 0x40; /* network_name_descriptor */
  dlen_pos = n;
  n++;
  nlen = psi_put_text(out + n, cap - n, network_name);
  n += nlen;
  out[dlen_pos] = (unsigned char)nlen;
  ndl = (unsigned)(n - nd_start);
  out[ndl_pos] = (unsigned char)(0xF0 | ((ndl >> 8) & 0x0F));
  out[ndl_pos + 1] = (unsigned char)ndl;

  tsl_pos = n;
  n += 2;
  ts_start = n;
  psi_put16(out + n, tsid);
  n += 2;
  psi_put16(out + n, onid);
  n += 2;
  psi_put16(out + n, 0xF000); /* transport_descriptors_length = 0 */
  n += 2;
  tsl = (unsigned)(n - ts_start);
  out[tsl_pos] = (unsigned char)(0xF0 | ((tsl >> 8) & 0x0F));
  out[tsl_pos + 1] = (unsigned char)tsl;

  return psi_finish_section(out, n, cap, 0xF0);
}
