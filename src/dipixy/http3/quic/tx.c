/* Copyright 2026 dvbipitools authors. Licensed under GPL-3.0-or-later.
 * See NOTICE and LICENSE for details and authorship information. */

#ifdef HAVE_HTTP3

#include "../http3.h"
#include "../http3_int.h"

#include <string.h>
#include <sys/uio.h>

_Thread_local int t_h3_udp4 = -1;
_Thread_local int t_h3_udp6 = -1;

void flush_tx(h3_conn_t *c, int udp_fd) {
  ngtcp2_tstamp ts = h3_ts();
  uint8_t *buf = h3_udp_tx_buf();
  size_t max_udp = ngtcp2_conn_get_path_max_tx_udp_payload_size(c->qconn);
  size_t max_segs = ngtcp2_conn_get_send_quantum(c->qconn) / max_udp;
  struct sockaddr_storage dst;
  socklen_t dstlen = 0;
  int tx_fd = udp_fd;
  size_t pos = 0;
  size_t seg = 0;
  size_t nseg = 0;

  if (max_segs > H3_GSO_MAX_SEGS) max_segs = H3_GSO_MAX_SEGS;
  if (max_segs > h3_udp_tx_cap() / max_udp) max_segs = h3_udp_tx_cap() / max_udp;
  if (max_segs < 1) max_segs = 1;

  for (;;) {
    int64_t stream_id = -1;
    int fin = 0;
    ngtcp2_vec qvec[16];
    size_t qvcnt = 0;
    if (c->h3conn) {
      nghttp3_vec h3vec[16];
      nghttp3_ssize nread = nghttp3_conn_writev_stream(c->h3conn, &stream_id, &fin, h3vec, 16);
      if (nread > 0) {
        for (int i = 0; i < (int)nread; i++) {
          qvec[i].base = h3vec[i].base;
          qvec[i].len = h3vec[i].len;
        }
        qvcnt = (size_t)nread;
      }
    }
    ngtcp2_ssize ndatalen = 0;
    /* bare ngtcp2_path has uninit addr pointers, path_storage needed */
    ngtcp2_path_storage ps;
    ngtcp2_path_storage_zero(&ps);
    ngtcp2_pkt_info pi;
    /* qvcnt==0 with fin set: must still carry FIN and report via add_write_offset(...,0), or stream never terminates */
    uint32_t flags = fin ? NGTCP2_WRITE_STREAM_FLAG_FIN : 0;
    ngtcp2_ssize pktlen = ngtcp2_conn_writev_stream_versioned(c->qconn, &ps.path, NGTCP2_PKT_INFO_VERSION, &pi, buf + pos, seg ? seg : max_udp, &ndatalen, flags, stream_id, qvcnt > 0 ? qvec : NULL, qvcnt, ts);
    if (pktlen == 0) {
      if (nseg == 0) break;
      /* no room at gso size: send queued. retry at full size */
      h3_udp_send(tx_fd, &dst, dstlen, buf, pos, seg);
      pos = 0;
      seg = 0;
      nseg = 0;
      continue;
    }
    if (pktlen < 0) {
      if (pktlen == NGTCP2_ERR_WRITE_MORE) {
        if (ndatalen >= 0 && c->h3conn) nghttp3_conn_add_write_offset(c->h3conn, stream_id, (uint64_t)ndatalen);
        continue;
      }
      c->done = 1;
      break;
    }
    socklen_t plen = ps.path.remote.addrlen;
    if (plen > sizeof dst) plen = sizeof dst;
    if (nseg > 0 && (plen != dstlen || memcmp(ps.path.remote.addr, &dst, plen) != 0)) {
      /* validation probes to new path, rest to old */
      h3_udp_send(tx_fd, &dst, dstlen, buf, pos, seg);
      memmove(buf, buf + pos, (size_t)pktlen);
      pos = 0;
      seg = 0;
      nseg = 0;
    }
    if (nseg == 0) {
      memcpy(&dst, ps.path.remote.addr, plen);
      dstlen = plen;
      tx_fd = dst.ss_family == AF_INET6 ? t_h3_udp6 : t_h3_udp4;
      if (tx_fd < 0) tx_fd = udp_fd;
      seg = (size_t)pktlen;
    }
    pos += (size_t)pktlen;
    nseg++;
    if (ndatalen >= 0 && c->h3conn) nghttp3_conn_add_write_offset(c->h3conn, stream_id, (uint64_t)ndatalen);
    if ((size_t)pktlen < seg || nseg >= max_segs) {
      h3_udp_send(tx_fd, &dst, dstlen, buf, pos, seg);
      pos = 0;
      seg = 0;
      nseg = 0;
    }
  }
  h3_udp_send(tx_fd, &dst, dstlen, buf, pos, seg);
  ngtcp2_conn_update_pkt_tx_time(c->qconn, ts);
}

#endif /* HAVE_HTTP3 */
