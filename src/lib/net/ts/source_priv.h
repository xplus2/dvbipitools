/* Copyright 2026 dvbipitools authors. Licensed under GPL-3.0-or-later.
 * See NOTICE and LICENSE for details and authorship information. */

#ifndef DVBIPITOOLS_LIB_NET_TS_SOURCE_PRIV_H
#define DVBIPITOOLS_LIB_NET_TS_SOURCE_PRIV_H

#include "../../dash/live.h"
#include "../../demux/fec2022.h"
#include "../../hls/live.h"
#include "../../hls/tspassthrough.h"
#include "../../mux/esbuild/remux.h"

#include "../httpclient/httpclient.h"
#include "../jitbuf.h"
#include "../multicast.h"
#include "../rist/ristin.h"
#include "../srt/srtsrc.h"
#include "source.h"

#define TSSRC_HTTP_SNIFF_CAP 2048
#define TSSRC_HTTP_MANIFEST_CAP (64 * 1024)
#define TSSRC_HTTP_INIT_CAP (256 * 1024)

/* max plausible RTP/MPEGTS record under a 1500-byte MTU: 12 + 7*188 */
#define TSSRC_RTP_MAX_RECORD (12 + 7 * 188)
/* bytes buffered before deciding raw vs RTP framing, TSSRC_STDIN/TSSRC_FILE */
#define TSSRC_DETECT_CAP (2 * TSSRC_RTP_MAX_RECORD + 20)
#define TSSRC_RAWBUF_CAP 65536

typedef enum { HTTP_CONTENT_TS, HTTP_CONTENT_HLS, HTTP_CONTENT_DASH, HTTP_CONTENT_UNKNOWN } http_content_kind_t;

typedef enum { DEFRAME_DETECT, DEFRAME_RAW, DEFRAME_RTP } deframe_state_t;

typedef enum { HTTP_SUB_TS, HTTP_SUB_HLS, HTTP_SUB_DASH } http_sub_kind_t;

typedef struct {
  struct tssrc *owner;
  unsigned stream_idx;
  int got_init;
} tssrc_media_ctx_t;

struct tssrc {
  tssrc_kind_t kind;
  mcast_t *m;
  mcast_t *fec_m;
  fec2022_dec_t *fec_dec;
  http_t *h;
  ristin_t *rist;
  srtsrc_t *srt;
  int fd; /* TSSRC_FILE */
  /* TSSRC_STDIN/TSSRC_FILE byte-stream framing */
  deframe_state_t deframe_state;
  unsigned char rawbuf[TSSRC_RAWBUF_CAP];
  size_t raw_len;
  size_t rtp_stride; /* DEFRAME_RTP: 12 + 188*N */
  size_t rtp_pos;     /* DEFRAME_RTP: offset within current stride, carried across reads */
  uint32_t last_rtp_ts;
  /* de-jitter. epfd = src fd + tfd, returned by tssrc_fd() */
  jitbuf_t *jb;
  int epfd;
  int tfd;
  int jb_failed; /* src failed, queue drains first */
  net_err_reason_t jb_reason;

  http_sub_kind_t http_sub;
  unsigned char http_sniff_buf[TSSRC_HTTP_SNIFF_CAP];
  size_t http_sniff_len;
  size_t http_sniff_pos;
  hls_live_t *hls[2];
  dash_live_t *dash[2];
  unsigned n_media;
  tssrc_media_ctx_t media_ctx[2];
  hls_ts_passthrough_t ts_pass;
  esbuild_remux_t remux;
};

int tssrc_src_fd(const tssrc_t *s);

ssize_t tssrc_raw_fd_read(int fd, unsigned char *buf, size_t cap, net_err_reason_t *reason_out);
ssize_t tssrc_deframe_read(tssrc_t *s, int fd, unsigned char *buf, size_t cap, net_err_reason_t *reason_out);
ssize_t tssrc_rtp_strip(unsigned char *buf, size_t n);

int tssrc_jitter_attach(tssrc_t *s, unsigned delay_ms);
ssize_t tssrc_jitter_read(tssrc_t *s, unsigned char *buf, size_t cap, net_err_reason_t *reason_out);
void tssrc_jitter_free(tssrc_t *s);

http_content_kind_t tssrc_http_classify(const unsigned char *b, size_t n);
ssize_t tssrc_http_read(tssrc_t *s, unsigned char *buf, size_t cap, net_err_reason_t *reason_out);
int tssrc_http_fd(const tssrc_t *s);
void tssrc_http_free_media(const tssrc_t *s);
void tssrc_hls_fmp4_segment_feed(void *ctx, hls_live_t *h, const unsigned char *data, size_t len);
void tssrc_dash_fmp4_segment_feed(void *ctx, dash_live_t *h, const unsigned char *data, size_t len);

#endif
