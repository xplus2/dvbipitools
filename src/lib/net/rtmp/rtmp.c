/* Copyright 2026 dvbipitools authors. Licensed under GPL-3.0-or-later.
 * See NOTICE and LICENSE for details and authorship information. */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

#include "lib/ioutil.h"
#include "priv.h"

rtmp_t *rtmp_new(const rtmp_cfg_t *cfg) {
  struct rtmp *r = calloc(1, sizeof *r);
  if (!r)
    return NULL;
  bufcpy(r->app, sizeof r->app, cfg->app ? cfg->app : "");
  bufcpy(r->tcurl, sizeof r->tcurl, cfg->tcurl ? cfg->tcurl : "");
  bufcpy(r->stream_name, sizeof r->stream_name, cfg->stream_name ? cfg->stream_name : "");
  if (cfg->user && cfg->user[0]) {
    bufcpy(r->user, sizeof r->user, cfg->user);
    bufcpy(r->password, sizeof r->password, cfg->password ? cfg->password : "");
    snprintf(r->auth_query, sizeof r->auth_query, "?authmod=adobe&user=%s", r->user);
  }
  r->in_chunk_size = 128;
  r->out_chunk_size = 128;
  r->write_cb = cfg->write_cb;
  r->ready_cb = cfg->ready_cb;
  r->error_cb = cfg->error_cb;
  r->cb_ctx = cfg->cb_ctx;
  r->state = RTMP_ST_UNINIT;
  return r;
}

void rtmp_free(rtmp_t *r) {
  unsigned i;
  if (!r)
    return;
  for (i = 0; i < RTMP_N_CHAN; i++)
    free(r->in_chan[i].payload);
  free(r);
}

void rtmp_start(rtmp_t *r) {
  unsigned char c0c1[1 + RTMP_HANDSHAKE_SIZE];
  c0c1[0] = RTMP_VERSION;
  rtmp_handshake_c1(c0c1 + 1, (uint32_t)time(NULL));
  r->state = RTMP_ST_WAIT_S0;
  r->hs_bytes = 0;
  r->write_cb(r->cb_ctx, c0c1, sizeof c0c1);
}

int rtmp_feed(rtmp_t *r, const unsigned char *data, size_t bytes) {
  size_t off = 0;

  while (off < bytes) {
    switch (r->state) {
      case RTMP_ST_WAIT_S0:
        off++;
        r->state = RTMP_ST_WAIT_S1;
        r->hs_bytes = 0;
        break;

      case RTMP_ST_WAIT_S1: {
        size_t need = RTMP_HANDSHAKE_SIZE - r->hs_bytes;
        size_t take = bytes - off < need ? bytes - off : need;
        memcpy(r->s1 + r->hs_bytes, data + off, take);
        r->hs_bytes += take;
        off += take;
        if (r->hs_bytes == RTMP_HANDSHAKE_SIZE) {
          unsigned char c2[RTMP_HANDSHAKE_SIZE];
          rtmp_handshake_c2(c2, r->s1, (uint32_t)time(NULL));
          r->write_cb(r->cb_ctx, c2, sizeof c2);
          r->state = RTMP_ST_WAIT_S2;
          r->hs_bytes = 0;
        }
        break;
      }

      case RTMP_ST_WAIT_S2: {
        size_t need = RTMP_HANDSHAKE_SIZE - r->hs_bytes;
        size_t take = bytes - off < need ? bytes - off : need;
        off += take;
        r->hs_bytes += take;
        if (r->hs_bytes == RTMP_HANDSHAKE_SIZE) {
          r->state = RTMP_ST_WAIT_CONNECT_RESULT;
          rtmp_command_connect(r);
        }
        break;
      }

      default:
        return rtmp_session_feed(r, data + off, bytes - off);
    }
  }
  return 0;
}

void rtmp_on_message(struct rtmp *r, unsigned char type, uint32_t timestamp, const unsigned char *payload, size_t len) {
  (void)timestamp;
  switch (type) {
    case RTMP_TYPE_SET_CHUNK_SIZE:
      if (len >= 4)
        r->in_chunk_size = (((uint32_t)payload[0] << 24) | ((uint32_t)payload[1] << 16) | ((uint32_t)payload[2] << 8) | payload[3]) & 0x7FFFFFFF;
      break;
    case RTMP_TYPE_INVOKE:
      rtmp_command_on_invoke(r, payload, len);
      break;
    default:
      break;
  }
}

int rtmp_send_video(rtmp_t *r, uint32_t timestamp_ms, const unsigned char *data, size_t len) {
  if (RTMP_ST_READY != r->state)
    return -1;
  return rtmp_session_write_message(r, RTMP_CID_VIDEO, RTMP_TYPE_VIDEO, r->stream_id, timestamp_ms, data, len);
}

int rtmp_send_audio(rtmp_t *r, uint32_t timestamp_ms, const unsigned char *data, size_t len) {
  if (RTMP_ST_READY != r->state)
    return -1;
  return rtmp_session_write_message(r, RTMP_CID_AUDIO, RTMP_TYPE_AUDIO, r->stream_id, timestamp_ms, data, len);
}

int rtmp_send_data(rtmp_t *r, const unsigned char *data, size_t len) {
  if (RTMP_ST_READY != r->state)
    return -1;
  return rtmp_session_write_message(r, RTMP_CID_INVOKE, RTMP_TYPE_DATA, r->stream_id, 0, data, len);
}
