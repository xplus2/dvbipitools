/* Copyright 2026 dvbipitools authors. Licensed under GPL-3.0-or-later.
 * See NOTICE and LICENSE for details and authorship information. */

#include <stdio.h>
#include <string.h>

#include "auth.h"
#include "lib/sys/ioutil.h"
#include "lib/mux/amf.h"
#include "priv.h"

/* parses key=value out of needauth description (?reason=...&salt=...&opaque=...) */
static int desc_kv(const char *desc, const char *key, char *out, size_t cap) {
  size_t keylen = strlen(key);
  const char *p = desc;
  for (;;) {
    p = strstr(p, key);
    if (!p) return 0;
    if (p == desc || p[-1] == '?' || p[-1] == '&') {
      const char *v = p + keylen;
      const char *e = v;
      size_t n;
      while (*e && *e != '&') e++;
      n = (size_t)(e - v);
      if (n >= cap) return 0;
      memcpy(out, v, n);
      out[n] = '\0';
      return 1;
    }
    p += keylen;
  }
}

static int send_invoke(struct rtmp *r, uint32_t stream_id, ebuf_t *b) {
  int ret;
  if (b->err) {
    ebuf_free(b);
    return -1;
  }
  ret = rtmp_session_write_message(r, RTMP_CID_INVOKE, RTMP_TYPE_INVOKE, stream_id, 0, NULL, 0, b->p, b->len);
  ebuf_free(b);
  return ret;
}

#define RTMP_OUT_CHUNK_SIZE 4096

static void send_set_chunk_size(struct rtmp *r, uint32_t size) {
  unsigned char body[4];
  body[0] = (unsigned char)(size >> 24);
  body[1] = (unsigned char)(size >> 16);
  body[2] = (unsigned char)(size >> 8);
  body[3] = (unsigned char)size;
  rtmp_session_write_message(r, RTMP_CID_PROTOCOL, RTMP_TYPE_SET_CHUNK_SIZE, 0, 0, NULL, 0, body, sizeof body);
  r->out_chunk_size = size;
}

int rtmp_command_connect(struct rtmp *r) {
  ebuf_t b;
  char appbuf[sizeof r->app + sizeof r->auth_query];
  memset(&b, 0, sizeof b);
  amf_string(&b, "connect");
  amf_number(&b, RTMP_TRANSACTION_CONNECT);
  amf_object_start(&b);
  amf_object_key(&b, "app");
  if (r->auth_query[0]) {
    size_t n = bufcpy(appbuf, sizeof appbuf, r->app);
    bufcpy(appbuf + n, sizeof appbuf - n, r->auth_query);
    amf_string(&b, appbuf);
  } else {
    amf_string(&b, r->app);
  }
  amf_object_key(&b, "flashVer");
  amf_string(&b, "FMLE/3.0 (compatible; dvbipitools)");
  if (r->tcurl[0]) {
    amf_object_key(&b, "tcUrl");
    amf_string(&b, r->tcurl);
  }
  amf_object_key(&b, "fpad");
  amf_boolean(&b, 0);
  amf_object_key(&b, "capabilities");
  amf_number(&b, 15);
  amf_object_key(&b, "audioCodecs");
  amf_number(&b, 3191);
  amf_object_key(&b, "videoCodecs");
  amf_number(&b, 252);
  amf_object_key(&b, "videoFunction");
  amf_number(&b, 1);
  amf_object_key(&b, "objectEncoding");
  amf_number(&b, 0);
  amf_object_end(&b);
  return send_invoke(r, 0, &b);
}

int rtmp_command_release_stream(struct rtmp *r) {
  ebuf_t b;
  memset(&b, 0, sizeof b);
  amf_string(&b, "releaseStream");
  amf_number(&b, 0);
  amf_null(&b);
  amf_string(&b, r->stream_name);
  return send_invoke(r, 0, &b);
}

int rtmp_command_fcpublish(struct rtmp *r) {
  ebuf_t b;
  memset(&b, 0, sizeof b);
  amf_string(&b, "FCPublish");
  amf_number(&b, 0);
  amf_null(&b);
  amf_string(&b, r->stream_name);
  return send_invoke(r, 0, &b);
}

int rtmp_command_create_stream(struct rtmp *r) {
  ebuf_t b;
  memset(&b, 0, sizeof b);
  amf_string(&b, "createStream");
  amf_number(&b, RTMP_TRANSACTION_CREATE_STREAM);
  amf_null(&b);
  return send_invoke(r, 0, &b);
}

int rtmp_command_publish(struct rtmp *r) {
  ebuf_t b;
  memset(&b, 0, sizeof b);
  amf_string(&b, "publish");
  amf_number(&b, 0);
  amf_null(&b);
  amf_string(&b, r->stream_name);
  amf_string(&b, "live");
  return send_invoke(r, r->stream_id, &b);
}

void rtmp_command_on_invoke(struct rtmp *r, const unsigned char *payload, size_t len) {
  const unsigned char *p = payload;
  const unsigned char *end = payload + len;
  char cmd[32];
  double transaction;

  p = amf_read_string(p, end, cmd, sizeof cmd);
  if (!p) return;
  p = amf_read_number(p, end, &transaction);
  if (!p) return;
  if (strcmp(cmd, "_error") == 0) {
    const unsigned char *info = amf_skip_value(p, end); /* command object, normally Null */
    if (r->state == RTMP_ST_WAIT_CONNECT_RESULT && r->user[0] && !r->auth_tried && info) {
      char desc[400];
      char salt[128];
      char opaque[128];
      char challenge[128];
      char challenge2[24];
      char response[24];
      opaque[0] = challenge[0] = '\0';
      if (amf_object_find_string(info, end, "description", desc, sizeof desc) == 1 && desc_kv(desc, "salt=", salt, sizeof salt) &&
          (desc_kv(desc, "opaque=", opaque, sizeof opaque) || desc_kv(desc, "challenge=", challenge, sizeof challenge)) &&
          rtmp_auth_adobe_response(r->user, r->password, salt, opaque[0] ? opaque : NULL, challenge[0] ? challenge : NULL, challenge2, sizeof challenge2, response, sizeof response) == 0) {
        r->auth_tried = 1;
        snprintf(r->auth_query, sizeof r->auth_query, "?authmod=adobe&user=%s&challenge=%s&response=%s%s%s", r->user, challenge2, response, opaque[0] ? "&opaque=" : "", opaque);
        rtmp_command_connect(r);
        return;
      }
    }
    r->state = RTMP_ST_FAILED;
    if (r->error_cb) r->error_cb(r->cb_ctx, cmd);
    return;
  }
  if (strcmp(cmd, "_result") != 0) return;
  if (r->state == RTMP_ST_WAIT_CONNECT_RESULT && (int)transaction == RTMP_TRANSACTION_CONNECT) {
    rtmp_command_release_stream(r);
    rtmp_command_fcpublish(r);
    r->state = RTMP_ST_WAIT_CREATE_STREAM_RESULT;
    rtmp_command_create_stream(r);
  } else if (r->state == RTMP_ST_WAIT_CREATE_STREAM_RESULT && (int)transaction == RTMP_TRANSACTION_CREATE_STREAM) {
    double stream_id = 0;
    p = amf_skip_value(p, end); /* command object: always Null here */
    if (p) p = amf_read_number(p, end, &stream_id);
    if (!p || !(stream_id >= 1.0 && stream_id < 4294967296.0)) {
      r->state = RTMP_ST_FAILED;
      if (r->error_cb) r->error_cb(r->cb_ctx, "invalid stream id");
      return;
    }
    r->stream_id = (uint32_t)stream_id;
    r->state = RTMP_ST_READY;
    send_set_chunk_size(r, RTMP_OUT_CHUNK_SIZE);
    rtmp_command_publish(r);
    if (r->ready_cb) r->ready_cb(r->cb_ctx);
  }
}
