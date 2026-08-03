/* Copyright 2026 dvbipitools authors. Licensed under GPL-3.0-or-later.
 * See NOTICE and LICENSE for details and authorship information. */

#include <errno.h>
#include <poll.h>
#include <string.h>
#include <sys/socket.h>
#include <unistd.h>

#include "lib/mux/psi_build.h"

#include "simulcrypt_msg.h"

int simulcrypt_hdr_parse(const unsigned char *buf, size_t buf_len, simulcrypt_hdr_t *hdr) {
  if (buf_len < SIMULCRYPT_HDR_LEN)
    return -1;
  hdr->version = buf[0];
  hdr->type = ((unsigned)buf[1] << 8) | buf[2];
  hdr->payload_len = ((unsigned)buf[3] << 8) | buf[4];
  return 0;
}

size_t simulcrypt_hdr_write(unsigned char version, unsigned short type, unsigned short payload_len, unsigned char *out, size_t cap) {
  if (cap < SIMULCRYPT_HDR_LEN)
    return 0;
  out[0] = version;
  psi_put16(out + 1, type);
  psi_put16(out + 3, payload_len);
  return SIMULCRYPT_HDR_LEN;
}

void simulcrypt_tlv_reader_init(simulcrypt_tlv_reader_t *r, const unsigned char *payload, size_t payload_len) {
  r->buf = payload;
  r->len = payload_len;
  r->pos = 0;
}

int simulcrypt_tlv_reader_next(simulcrypt_tlv_reader_t *r, unsigned short *tag, const unsigned char **value, unsigned short *value_len) {
  unsigned short vlen;
  if (r->pos == r->len)
    return 0;
  if (r->len - r->pos < 4)
    return -1;
  vlen = ((unsigned)r->buf[r->pos + 2] << 8) | r->buf[r->pos + 3];
  if (r->len - (r->pos + 4) < vlen)
    return -1;
  *tag = ((unsigned)r->buf[r->pos] << 8) | r->buf[r->pos + 1];
  *value = r->buf + r->pos + 4;
  *value_len = vlen;
  r->pos += 4 + vlen;
  return 1;
}

int simulcrypt_writer_begin(simulcrypt_writer_t *w, unsigned char *buf, size_t cap, unsigned char version, unsigned short type) {
  w->buf = buf;
  w->cap = cap;
  w->len = simulcrypt_hdr_write(version, type, 0, buf, cap);
  return w->len ? 0 : -1;
}

int simulcrypt_writer_put_tlv(simulcrypt_writer_t *w, unsigned short tag, const unsigned char *value, unsigned short value_len) {
  size_t cur_payload;
  if (w->len == 0)
    return -1;
  cur_payload = w->len - SIMULCRYPT_HDR_LEN;
  if (cur_payload + 4 + value_len > SIMULCRYPT_MAX_PAYLOAD || w->cap - w->len < (size_t)4 + value_len) {
    w->len = 0;
    return -1;
  }
  psi_put16(w->buf + w->len, tag);
  psi_put16(w->buf + w->len + 2, value_len);
  if (value_len)
    memcpy(w->buf + w->len + 4, value, value_len);
  w->len += 4 + value_len;
  return 0;
}

size_t simulcrypt_writer_finish(simulcrypt_writer_t *w) {
  if (w->len == 0)
    return 0;
  psi_put16(w->buf + 3, (unsigned)(w->len - SIMULCRYPT_HDR_LEN));
  return w->len;
}

void simulcrypt_reader_init(simulcrypt_reader_t *r) {
  r->have = 0;
  r->need = 0;
}

int simulcrypt_reader_poll(simulcrypt_reader_t *r, int fd, int timeout_ms, simulcrypt_hdr_t *hdr, const unsigned char **payload) {
  struct pollfd pfd;
  int pret;

  pfd.fd = fd;
  pfd.events = POLLIN;
  pret = poll(&pfd, 1, timeout_ms);
  if (pret < 0)
    return (errno == EINTR) ? 0 : -1;
  if (pret == 0)
    return 0;

  for (;;) {
    size_t want = r->need ? r->need - r->have : SIMULCRYPT_HDR_LEN - r->have;
    ssize_t n = read(fd, r->buf + r->have, want);
    if (n == 0)
      return -1;
    if (n < 0) {
      if (errno == EINTR)
        continue;
      return (errno == EAGAIN || errno == EWOULDBLOCK) ? 0 : -1;
    }
    r->have += (size_t)n;

    if (!r->need && r->have >= SIMULCRYPT_HDR_LEN) {
      simulcrypt_hdr_parse(r->buf, r->have, &r->hdr);
      r->need = SIMULCRYPT_HDR_LEN + r->hdr.payload_len;
      /* payload_len is 16-bit so this can't happen with today's SIMULCRYPT_MAX_PAYLOAD,
         but the buffer write below relies on it - check locally, don't just trust that */
      if (r->need > sizeof r->buf)
        return -1;
    }
    if (r->need && r->have >= r->need) {
      *hdr = r->hdr;
      *payload = r->buf + SIMULCRYPT_HDR_LEN;
      r->have = 0;
      r->need = 0;
      return 1;
    }
  }
}

int simulcrypt_send_all(int fd, const unsigned char *buf, size_t len, int timeout_ms) {
  size_t sent = 0;
  while (sent < len) {
    ssize_t n = send(fd, buf + sent, len - sent, MSG_NOSIGNAL);
    if (n > 0) {
      sent += (size_t)n;
      continue;
    }
    if (n < 0 && (errno == EAGAIN || errno == EWOULDBLOCK)) {
      struct pollfd pfd = {fd, POLLOUT, 0};
      if (poll(&pfd, 1, timeout_ms) <= 0)
        return -1;
      continue;
    }
    if (n < 0 && errno == EINTR)
      continue;
    return -1;
  }
  return 0;
}
