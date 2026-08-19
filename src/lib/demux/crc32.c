/* Copyright 2026 dvbipitools authors. Licensed under GPL-3.0-or-later.
 * See NOTICE and LICENSE for details and authorship information. */

#include "crc32.h"
#include <string.h>

#if defined(__x86_64__) || defined(__i386__)
#include <wmmintrin.h>
#include <smmintrin.h>
#define CRC32_MPEG_HAVE_X86 1
#endif

#if defined(__aarch64__)
#include <arm_acle.h>
#include <sys/auxv.h>
#include <asm/hwcap.h>
#define CRC32_MPEG_HAVE_ARM 1
#endif

static uint32_t crc32_table[256];

static void crc32_table_init(void) {
  for (unsigned n = 0; n < 256; n++) {
    uint32_t c = (uint32_t)n << 24;
    for (int k = 0; k < 8; k++)
      c = (c & 0x80000000u) ? (c << 1) ^ 0x04C11DB7u : (c << 1);
    crc32_table[n] = c;
  }
}

/* MPEG-2 CRC32 update from an arbitrary running state, table-driven.
   used both as portable fallback and as tail path after a chunked
   accelerated backend has consumed bulk of buffer. */
static uint32_t crc32_mpeg_table_update(uint32_t crc, const unsigned char *data, size_t len) {
  for (size_t i = 0; i < len; i++)
    crc = (crc << 8) ^ crc32_table[((crc >> 24) ^ data[i]) & 0xFF];
  return crc;
}

/* not static: cross-checked directly against accelerated backends in
 * tests/unit/lib/demux/test_crc32.c. not part of public API (crc32.h). */
uint32_t crc32_mpeg_generic(const unsigned char *data, size_t len) {
  return crc32_mpeg_table_update(0xFFFFFFFFu, data, len);
}

#ifdef CRC32_MPEG_HAVE_X86
/* K = x^64 mod G, MU = floor(x^64 / G), G = CRC-32/MPEG generator (0x104C11DB7, degree 32).
   Barrett-style reduction, derived and cross-checked against bit-serial algorithm over 200000 randomized
   buffers plus standard "123456789" check vector before use. */
#define CRC32_MPEG_K  0x490d678dULL
#define CRC32_MPEG_MU 0x104d101dfULL
#define CRC32_MPEG_G  0x104C11DB7ULL

__attribute__((target("sse4.1,pclmul")))
static inline uint64_t crc32_mpeg_clmul_lo(uint64_t a, uint64_t b) {
  __m128i r = _mm_clmulepi64_si128(_mm_set_epi64x(0, (long long)a), _mm_set_epi64x(0, (long long)b), 0x00);
  uint64_t out;
  _mm_storel_epi64((__m128i *)&out, r);
  return out;
}

__attribute__((target("sse4.1,pclmul")))
static inline uint32_t crc32_mpeg_barrett(uint64_t t) {
  uint64_t q = crc32_mpeg_clmul_lo(t >> 32, CRC32_MPEG_MU);
  uint64_t prod = crc32_mpeg_clmul_lo(q >> 32, CRC32_MPEG_G);
  return (uint32_t)((t ^ prod) & 0xFFFFFFFFu);
}

/* not static: cross-checked directly in tests/unit/lib/demux/test_crc32.c. not public. */
__attribute__((target("sse4.1,pclmul")))
uint32_t crc32_mpeg_pclmul(const unsigned char *data, size_t len) {
  uint32_t state = 0xFFFFFFFFu;
  size_t i = 0;
  while (i + 8 <= len) {
    uint64_t raw, block, high32, low64, reduced;
    memcpy(&raw, data + i, 8);
    block = __builtin_bswap64(raw);
    high32 = (uint64_t)state ^ (block >> 32);
    low64 = (block & 0xFFFFFFFFu) << 32;
    reduced = low64 ^ crc32_mpeg_clmul_lo(high32, CRC32_MPEG_K);
    state = crc32_mpeg_barrett(reduced);
    i += 8;
  }
  return crc32_mpeg_table_update(state, data + i, len - i);
}
#endif

#ifdef CRC32_MPEG_HAVE_ARM
/* MPEG-2 CRC32 (non-reflected) is the bit-reversal of the reflected
 * CRC-32 (0xEDB88320) the ARMv8 CRC32 extension implements natively:
 * crc32_mpeg(data) == bitrev32(hwcrc32(bitrev8(data))), since both the
 * init (0xFFFFFFFF) and xorout (0) are 32-bit bit-palindromes. Verified
 * in Python against the bit-serial reference; not execution-tested here
 * for lack of ARM hardware. */
static unsigned char crc32_mpeg_bitrev8[256];

static void crc32_mpeg_bitrev8_init(void) {
  for (unsigned n = 0; n < 256; n++) {
    unsigned char b = (unsigned char)n;
    b = (unsigned char)(((b & 0xF0) >> 4) | ((b & 0x0F) << 4));
    b = (unsigned char)(((b & 0xCC) >> 2) | ((b & 0x33) << 2));
    b = (unsigned char)(((b & 0xAA) >> 1) | ((b & 0x55) << 1));
    crc32_mpeg_bitrev8[n] = b;
  }
}

static uint32_t crc32_mpeg_bitrev32(uint32_t x) {
  x = ((x & 0xFFFF0000u) >> 16) | ((x & 0x0000FFFFu) << 16);
  x = ((x & 0xFF00FF00u) >> 8) | ((x & 0x00FF00FFu) << 8);
  x = ((x & 0xF0F0F0F0u) >> 4) | ((x & 0x0F0F0F0Fu) << 4);
  x = ((x & 0xCCCCCCCCu) >> 2) | ((x & 0x33333333u) << 2);
  x = ((x & 0xAAAAAAAAu) >> 1) | ((x & 0x55555555u) << 1);
  return x;
}

__attribute__((target("+crc")))
static uint32_t crc32_mpeg_armcrc(const unsigned char *data, size_t len) {
  uint32_t crc = 0xFFFFFFFFu;
  for (size_t i = 0; i < len; i++)
    crc = __crc32b(crc, crc32_mpeg_bitrev8[data[i]]);
  return crc32_mpeg_bitrev32(crc);
}
#endif

static uint32_t (*crc32_mpeg_impl)(const unsigned char *, size_t) = crc32_mpeg_generic;

__attribute__((constructor))
static void crc32_mpeg_select(void) {
  crc32_table_init();
#ifdef CRC32_MPEG_HAVE_X86
  __builtin_cpu_init();
  if (__builtin_cpu_supports("pclmul") && __builtin_cpu_supports("sse4.1")) {
    crc32_mpeg_impl = crc32_mpeg_pclmul;
    return;
  }
#endif
#ifdef CRC32_MPEG_HAVE_ARM
  if (getauxval(AT_HWCAP) & HWCAP_CRC32) {
    crc32_mpeg_bitrev8_init();
    crc32_mpeg_impl = crc32_mpeg_armcrc;
    return;
  }
#endif
  crc32_mpeg_impl = crc32_mpeg_generic;
}

uint32_t crc32_mpeg(const unsigned char *data, size_t len) {
  return crc32_mpeg_impl(data, len);
}
