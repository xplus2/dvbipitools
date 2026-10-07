/* Copyright 2026 dvbipitools authors. Licensed under GPL-3.0-or-later.
 * See NOTICE and LICENSE for details and authorship information. */

/* AFL++ harness: first byte of file argv[1] picks the target, the rest is input.
   even: amf_skip_value()/amf_object_find_string()/amf_read_*().
   odd: rtmp session fed a fake handshake, input as chunk stream, whole and in 7B chunks. */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "lib/mux/amf.h"
#include "lib/net/rtmp/rtmp.h"

static unsigned char *read_input(const char *path, size_t *len_out) {
  FILE *f = fopen(path, "rb");
  unsigned char *buf;
  long len;
  size_t n;

  if (!f) return NULL;
  if (fseek(f, 0, SEEK_END) || (len = ftell(f)) < 0 || fseek(f, 0, SEEK_SET)) {
    fclose(f);
    return NULL;
  }
  buf = malloc((size_t)len + 1);
  if (!buf) {
    fclose(f);
    return NULL;
  }
  n = fread(buf, 1, (size_t)len, f);
  fclose(f);
  buf[n] = '\0';
  *len_out = n;
  return buf;
}

#define HANDSHAKE_BYTES (1 + 2 * 1536)
#define SLICE 7

static void sink_write(void *ctx, const unsigned char *data, size_t len) {
  (void)ctx;
  (void)data;
  (void)len;
}

static void sink_ready(void *ctx) { (void)ctx; }

static void sink_error(void *ctx, const char *msg) {
  (void)ctx;
  (void)msg;
}

static void fuzz_amf(const unsigned char *p, size_t len) {
  const unsigned char *end = p + len;
  char out[64];
  double v;

  amf_skip_value(p, end);
  amf_read_number(p, end, &v);
  amf_read_string(p, end, out, sizeof out);
  amf_object_find_string(p, end, "code", out, sizeof out);
}

static void fuzz_session(const unsigned char *data, size_t len, size_t slice) {
  static unsigned char zeros[HANDSHAKE_BYTES];
  rtmp_cfg_t cfg;
  rtmp_t *r;

  memset(&cfg, 0, sizeof cfg);
  cfg.app = "live";
  cfg.tcurl = "rtmp://127.0.0.1/live";
  cfg.stream_name = "key";
  cfg.write_cb = sink_write;
  cfg.ready_cb = sink_ready;
  cfg.error_cb = sink_error;
  r = rtmp_new(&cfg);
  if (!r) return;
  rtmp_start(r);
  if (rtmp_feed(r, zeros, sizeof zeros) == 0) {
    for (size_t off = 0; off < len; off += slice) {
      size_t n = len - off < slice ? len - off : slice;

      if (rtmp_feed(r, data + off, n) != 0) break;
    }
  }
  rtmp_free(r);
}

int main(int argc, char **argv) {
  unsigned char *buf;
  size_t len;

  if (argc != 2) {
    fprintf(stderr, "usage: %s <input-file>\n", argv[0]);
    return 1;
  }
  buf = read_input(argv[1], &len);
  if (!buf) return 1;
  if (len > 0 && (buf[0] & 1)) {
    fuzz_session(buf + 1, len - 1, len);
    fuzz_session(buf + 1, len - 1, SLICE);
  } else if (len > 0) {
    fuzz_amf(buf + 1, len - 1);
  }
  free(buf);
  return 0;
}
