/* Copyright 2026 dvbipitools authors. Licensed under GPL-3.0-or-later.
 * See NOTICE and LICENSE for details and authorship information. */

#ifndef DIPIXY_HTTP3_UDP_H
#define DIPIXY_HTTP3_UDP_H

#ifdef HAVE_HTTP3

#include <stddef.h>
#include <stdint.h>
#include <sys/socket.h>

#define H3_RX_BATCH 32
#define H3_GSO_MAX_SEGS 64
#define H3_UDP_MAX_PAYLOAD 65507

typedef struct {
  const uint8_t *data;
  size_t len;
  struct sockaddr_storage peer;
  socklen_t peerlen;
} h3_rx_t;

/* per thread buffers. payload: largest datagram. 0 on success */
int h3_udp_init(size_t payload);
void h3_udp_free(void);

/* B usable by h3_udp_tx_buf() */
size_t h3_udp_tx_cap(void);
uint8_t *h3_udp_tx_buf(void);

/* 1 if fd takes UDP_SEGMENT */
int h3_udp_gso_probe(int fd);
void h3_udp_set_gso(int on);

/* datagrams read (len 0: truncated), 0 = no pending, -1 error. valid till next call */
int h3_udp_recv(int fd, h3_rx_t *out, int max);

/* buf holds total B, every datagram seg bytes except for shorter prev */
void h3_udp_send(int fd, const struct sockaddr_storage *dst, socklen_t dstlen, const uint8_t *buf, size_t total, size_t seg);

#endif /* HAVE_HTTP3 */

#endif /* DIPIXY_HTTP3_UDP_H */
