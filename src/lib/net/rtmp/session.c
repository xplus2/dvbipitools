/* Copyright 2026 dvbipitools authors. Licensed under GPL-3.0-or-later.
 * See NOTICE and LICENSE for details and authorship information. */

#include <stdlib.h>
#include <string.h>

#include "priv.h"

#define RTMP_MAX_MESSAGE_BYTES (10u << 20)

static rtmp_out_chan_t *out_chan_find(struct rtmp *r, uint32_t cid) {
  unsigned i;
  for (i = 0; i < RTMP_N_CHAN; i++)
    if (r->out_chan[i].used && r->out_chan[i].cid == cid)
      return &r->out_chan[i];
  return NULL;
}

static rtmp_out_chan_t *out_chan_alloc(struct rtmp *r, uint32_t cid) {
  unsigned i;
  for (i = 0; i < RTMP_N_CHAN; i++)
    if (!r->out_chan[i].used) {
      memset(&r->out_chan[i], 0, sizeof r->out_chan[i]);
      r->out_chan[i].cid = cid;
      r->out_chan[i].used = 1;
      return &r->out_chan[i];
    }
  return NULL;
}

int rtmp_session_write_message(struct rtmp *r, uint32_t cid, unsigned char type, uint32_t stream_id, uint32_t timestamp, const unsigned char *payload, size_t len) {
  rtmp_out_chan_t *chan;
  rtmp_chunk_header_t h;
  unsigned char hdr[RTMP_MAX_HEADER];
  size_t hn, sent;

  if (len >= 0xFFFFFF)
    return -1;

  h.cid = cid;
  h.type = type;
  h.stream_id = stream_id;
  h.length = (uint32_t)len;
  h.timestamp = timestamp;
  h.fmt = RTMP_CHUNK_FMT_0;

  chan = out_chan_find(r, cid);
  if (chan && timestamp >= chan->clock && timestamp - chan->clock < 0xFFFFFF && stream_id == chan->header.stream_id) {
    h.fmt = RTMP_CHUNK_FMT_1;
    h.timestamp = timestamp - chan->clock;
    if (type == chan->header.type && h.length == chan->header.length) {
      h.fmt = RTMP_CHUNK_FMT_2;
      if (h.timestamp == chan->header.timestamp)
        h.fmt = RTMP_CHUNK_FMT_3;
    }
  }
  if (!chan) {
    chan = out_chan_alloc(r, cid);
    if (!chan)
      return -1;
  }
  chan->header = h;
  chan->clock = timestamp;

  hn = rtmp_chunk_basic_header_write(hdr, h.fmt, cid);
  hn += rtmp_chunk_message_header_write(hdr + hn, &h);
  if (h.timestamp >= 0xFFFFFF) {
    rtmp_chunk_extended_timestamp_write(hdr + hn, h.timestamp);
    hn += 4;
  }
  r->write_cb(r->cb_ctx, hdr, hn);

  sent = 0;
  while (sent < len) {
    size_t chunk = len - sent < r->out_chunk_size ? len - sent : r->out_chunk_size;
    r->write_cb(r->cb_ctx, payload + sent, chunk);
    sent += chunk;
    if (sent < len) {
      hn = rtmp_chunk_basic_header_write(hdr, RTMP_CHUNK_FMT_3, cid);
      if (h.timestamp >= 0xFFFFFF) {
        rtmp_chunk_extended_timestamp_write(hdr + hn, h.timestamp);
        hn += 4;
      }
      r->write_cb(r->cb_ctx, hdr, hn);
    }
  }
  return 0;
}

static rtmp_in_chan_t *in_chan_find(struct rtmp *r, uint32_t cid) {
  unsigned i;
  for (i = 0; i < RTMP_N_CHAN; i++)
    if (r->in_chan[i].used && r->in_chan[i].cid == cid)
      return &r->in_chan[i];
  return NULL;
}

static rtmp_in_chan_t *in_chan_alloc(struct rtmp *r, uint32_t cid) {
  unsigned i;
  for (i = 0; i < RTMP_N_CHAN; i++)
    if (!r->in_chan[i].used) {
      memset(&r->in_chan[i], 0, sizeof r->in_chan[i]);
      r->in_chan[i].cid = cid;
      r->in_chan[i].used = 1;
      return &r->in_chan[i];
    }
  return NULL;
}

static int in_chan_alloc_payload(rtmp_in_chan_t *chan) {
  if (chan->header.length > RTMP_MAX_MESSAGE_BYTES)
    return -1;
  if (chan->capacity < chan->header.length) {
    unsigned char *p = realloc(chan->payload, chan->header.length + 1);
    if (!p)
      return -1;
    chan->payload = p;
    chan->capacity = chan->header.length;
  }
  return 0;
}

int rtmp_session_feed(struct rtmp *r, const unsigned char *data, size_t bytes) {
  static const size_t msg_hdr_size[4] = {11, 7, 3, 0};
  rtmp_parser_t *p = &r->parser;
  size_t offset = 0;

  while (offset < bytes) {
    switch (p->state) {
      case RTMP_PARSE_INIT:
        p->buffer[0] = data[offset++];
        p->bytes = 1;
        p->basic_bytes = (0 == (p->buffer[0] & 0x3F)) ? 2 : (1 == (p->buffer[0] & 0x3F)) ? 3 : 1;
        p->pkt = NULL;
        p->state = RTMP_PARSE_BASIC_HEADER;
        break;

      case RTMP_PARSE_BASIC_HEADER:
        while (p->bytes < p->basic_bytes && offset < bytes)
          p->buffer[p->bytes++] = data[offset++];
        if (p->bytes >= p->basic_bytes)
          p->state = RTMP_PARSE_MESSAGE_HEADER;
        break;

      case RTMP_PARSE_MESSAGE_HEADER: {
        size_t need = msg_hdr_size[p->buffer[0] >> 6] + p->basic_bytes;
        while (p->bytes < need && offset < bytes)
          p->buffer[p->bytes++] = data[offset++];
        if (p->bytes >= need) {
          unsigned fmt;
          uint32_t cid;
          rtmp_chunk_basic_header_read(p->buffer, &fmt, &cid);
          p->pkt = in_chan_find(r, cid);
          if (!p->pkt) {
            if (fmt != RTMP_CHUNK_FMT_0 && fmt != RTMP_CHUNK_FMT_1)
              return -1;
            p->pkt = in_chan_alloc(r, cid);
            if (!p->pkt)
              return -1;
          }
          p->pkt->header.cid = cid;
          p->pkt->header.fmt = fmt;
          rtmp_chunk_message_header_read(p->buffer + p->basic_bytes, &p->pkt->header);
          p->state = RTMP_PARSE_EXTENDED_TIMESTAMP;
        }
        break;
      }

      case RTMP_PARSE_EXTENDED_TIMESTAMP: {
        /* fmt 3 has no header bytes of its own but still inherits an extended
           timestamp if chunk stream's last full header carried one */
        int has_ext = (p->pkt->header.timestamp == 0xFFFFFF);
        size_t need = msg_hdr_size[p->pkt->header.fmt] + p->basic_bytes + (has_ext ? 4 : 0);
        while (p->bytes < need && offset < bytes)
          p->buffer[p->bytes++] = data[offset++];
        if (p->bytes >= need) {
          uint32_t ts = has_ext ? rtmp_chunk_extended_timestamp_read(p->buffer + msg_hdr_size[p->pkt->header.fmt] + p->basic_bytes) : p->pkt->header.timestamp;
          if (0 == p->pkt->bytes) {
            if (RTMP_CHUNK_FMT_0 == p->pkt->header.fmt)
              p->pkt->clock = ts;
            else
              p->pkt->clock += ts;
            if (in_chan_alloc_payload(p->pkt) != 0)
              return -1;
          }
          p->state = RTMP_PARSE_PAYLOAD;
        }
        break;
      }

      case RTMP_PARSE_PAYLOAD: {
        size_t room = r->in_chunk_size - (p->pkt->bytes % r->in_chunk_size);
        size_t need = p->pkt->header.length - p->pkt->bytes;
        size_t chunk = room < need ? room : need;
        if (chunk > bytes - offset)
          chunk = bytes - offset;
        if (chunk) {
          memcpy(p->pkt->payload + p->pkt->bytes, data + offset, chunk);
          p->pkt->bytes += chunk;
          offset += chunk;
        }
        if (p->pkt->bytes >= p->pkt->header.length) {
          unsigned char type = p->pkt->header.type;
          uint32_t clock = p->pkt->clock;
          size_t len = p->pkt->bytes;
          p->pkt->bytes = 0;
          p->state = RTMP_PARSE_INIT;
          rtmp_on_message(r, type, clock, p->pkt->payload, len);
        } else if (0 == p->pkt->bytes % r->in_chunk_size) {
          p->state = RTMP_PARSE_INIT;
        }
        break;
      }
    }
  }
  return 0;
}
