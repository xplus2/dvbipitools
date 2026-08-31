/* Copyright 2026 dvbipitools authors. Licensed under GPL-3.0-or-later.
 * See NOTICE and LICENSE for details and authorship information. */

#include "gena.h"

#include <stdatomic.h>
#include <time.h>

#include "lib/helper/ioutil.h"

static _Atomic unsigned g_sid_counter;

static size_t hex_min(char *dst, unsigned long v, size_t min_width) {
  char tmp[2 * sizeof(unsigned long)];
  size_t n = 0, pad, i;
  if (!v) {
    tmp[n++] = '0';
  } else {
    while (v) {
      unsigned d = (unsigned)(v & 0xf);
      tmp[n++] = (char)(d < 10 ? '0' + d : 'a' + d - 10);
      v >>= 4;
    }
  }
  pad = n < min_width ? min_width - n : 0;
  for (i = 0; i < pad; i++)
    dst[i] = '0';
  for (i = 0; i < n; i++)
    dst[pad + i] = tmp[n - 1 - i];
  return pad + n;
}

static void make_sid(char out[64]) {
  unsigned n = atomic_fetch_add(&g_sid_counter, 1u);
  size_t off = bufcpy(out, 64, "uuid:dipixy-sub-");
  off += hex_min(out + off, (unsigned long)time(NULL), 8);
  out[off++] = '-';
  off += hex_min(out + off, n & 0xffffu, 4);
  bufcpy(out + off, 64 - off, "-4a11-8a11-000000000000");
}

void gena_subscribe_new(const config_t *cfg, const char *service, const char *callback_hdr, char *out_sid,
                         size_t out_sidsz) {
  (void)cfg;
  (void)service;
  (void)callback_hdr;
  (void)out_sidsz;
  make_sid(out_sid);
}

void gena_renew(const char *sid_hdr, char *out_sid, size_t out_sidsz) {
  if (sid_hdr)
    bufcpy(out_sid, out_sidsz, sid_hdr);
  else
    make_sid(out_sid);
}

void gena_unsubscribe(const char *sid_hdr) {
  (void)sid_hdr;
}

void gena_notify_system_update(void) {
}

unsigned gena_system_update_id(void) {
  return 1;
}
