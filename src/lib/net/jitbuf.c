/* Copyright 2026 dvbipitools authors. Licensed under GPL-3.0-or-later.
 * See NOTICE and LICENSE for details and authorship information. */

#include <stdlib.h>
#include <string.h>

#include "../demux/rtp.h"
#include "jitbuf.h"

#define JITBUF_SLOTS 8192 /* pow2, under 32768 */
#define JITBUF_LATE_WINDOW 1024
#define JITBUF_FAR_RESYNC 8

typedef struct {
  uint64_t at_ns;
  uint16_t len;
  uint16_t seq;
  uint8_t used;
  unsigned char data[JITBUF_MAX_DGRAM];
} slot_t;

typedef enum { MODE_UNSET, MODE_FIFO, MODE_RTP } eff_mode_t;

struct jitbuf {
  uint64_t delay_ns;
  eff_mode_t mode;
  slot_t *slots;
  size_t count;
  size_t head;       /* FIFO: oldest slot */
  uint16_t next_seq; /* RTP: next to release */
  uint32_t ssrc;
  uint32_t span;     /* RTP: slots from next_seq to highest queued, exclusive */
  unsigned far_run;
  int released;      /* RTP: start fixed after first pop */
  jitbuf_stats_t st;
};

jitbuf_t *jitbuf_new(unsigned delay_ms, jitbuf_mode_t mode) {
  jitbuf_t *j;
  if (delay_ms < 1 || delay_ms > JITBUF_MAX_DELAY_MS) return NULL;
  j = calloc(1, sizeof *j);
  if (!j) return NULL;
  j->slots = calloc(JITBUF_SLOTS, sizeof *j->slots);
  if (!j->slots) {
    free(j);
    return NULL;
  }
  j->delay_ns = (uint64_t)delay_ms * 1000000ULL;
  j->mode = mode == JITBUF_FIFO ? MODE_FIFO : MODE_UNSET;
  return j;
}

void jitbuf_free(jitbuf_t *j) {
  if (!j) return;
  free(j->slots);
  free(j);
}

static slot_t *rtp_slot(jitbuf_t *j, uint16_t seq) { return &j->slots[seq & (JITBUF_SLOTS - 1)]; }

static void flush_all(jitbuf_t *j) {
  for (size_t i = 0; i < JITBUF_SLOTS; i++) j->slots[i].used = 0;
  j->count = 0;
  j->head = 0;
  j->span = 0;
}

static void fifo_push(jitbuf_t *j, const unsigned char *d, size_t len, uint64_t now_ns) {
  slot_t *s;
  if (j->count == JITBUF_SLOTS) {
    j->slots[j->head].used = 0;
    j->head = (j->head + 1) & (JITBUF_SLOTS - 1);
    j->count--;
    j->st.dropped++;
  }
  s = &j->slots[(j->head + j->count) & (JITBUF_SLOTS - 1)];
  memcpy(s->data, d, len);
  s->len = (uint16_t)len;
  s->at_ns = now_ns;
  s->used = 1;
  j->count++;
}

static void rtp_resync(jitbuf_t *j, const rtp_hdr_t *h) {
  flush_all(j);
  j->next_seq = h->seq;
  j->ssrc = h->ssrc;
  j->far_run = 0;
  j->st.resync++;
}

static void rtp_push(jitbuf_t *j, const unsigned char *d, size_t len, uint64_t now_ns) {
  rtp_hdr_t h;
  slot_t *s;
  int16_t diff;

  if (!rtp_payload_offset(d, len) || !rtp_parse_header(d, len, &h)) {
    j->st.dropped++;
    return;
  }
  if (j->ssrc != h.ssrc) rtp_resync(j, &h);
  diff = (int16_t)(h.seq - j->next_seq);
  if (diff < 0 && !j->released && (uint32_t)-diff + j->span < JITBUF_SLOTS) {
    j->next_seq = h.seq;
    j->span += (uint32_t)-diff;
    diff = 0;
  } else if (diff < 0) {
    if (diff >= -JITBUF_LATE_WINDOW) {
      j->st.late++;
      return;
    }
    if (++j->far_run < JITBUF_FAR_RESYNC) {
      j->st.dropped++;
      return;
    }
    rtp_resync(j, &h);
    diff = 0;
  } else if (diff >= JITBUF_SLOTS) {
    if (++j->far_run < JITBUF_FAR_RESYNC) {
      j->st.dropped++;
      return;
    }
    rtp_resync(j, &h);
    diff = 0;
  } else {
    j->far_run = 0;
  }
  s = rtp_slot(j, h.seq);
  if (s->used) {
    j->st.dup++;
    return;
  }
  memcpy(s->data, d, len);
  s->len = (uint16_t)len;
  s->seq = h.seq;
  s->at_ns = now_ns;
  s->used = 1;
  j->count++;
  if ((uint32_t)diff + 1 > j->span) j->span = (uint32_t)diff + 1;
  else j->st.reordered++;
}

void jitbuf_push(jitbuf_t *j, const unsigned char *dgram, size_t len, uint64_t now_ns) {
  if (len == 0 || len > JITBUF_MAX_DGRAM) {
    j->st.dropped++;
    return;
  }
  if (j->mode == MODE_UNSET) {
    j->mode = rtp_payload_offset(dgram, len) ? MODE_RTP : MODE_FIFO;
    if (j->mode == MODE_RTP) {
      rtp_hdr_t h;
      if (!rtp_parse_header(dgram, len, &h)) {
        j->mode = MODE_FIFO;
      } else {
        j->next_seq = h.seq;
        j->ssrc = h.ssrc;
      }
    }
  }
  if (j->mode == MODE_RTP) rtp_push(j, dgram, len, now_ns);
  else fifo_push(j, dgram, len, now_ns);
}

static slot_t *rtp_first(jitbuf_t *j, uint32_t *dist) {
  for (uint32_t d = 0; d < j->span; d++) {
    slot_t *s = rtp_slot(j, (uint16_t)(j->next_seq + d));
    if (s->used) {
      *dist = d;
      return s;
    }
  }
  return NULL;
}

size_t jitbuf_pop(jitbuf_t *j, unsigned char *out, size_t cap, uint64_t now_ns) {
  slot_t *s;
  uint32_t dist = 0;
  size_t len;

  if (j->count == 0) return 0;
  if (j->mode == MODE_RTP) {
    s = rtp_first(j, &dist);
    if (!s) return 0;
  } else {
    s = &j->slots[j->head];
  }
  if (s->at_ns + j->delay_ns > now_ns) return 0;
  len = s->len;
  if (len <= cap) memcpy(out, s->data, len);
  else {
    len = 0;
    j->st.dropped++;
  }
  s->used = 0;
  j->count--;
  j->released = 1;
  if (j->mode == MODE_RTP) {
    j->st.lost += dist;
    j->next_seq = (uint16_t)(j->next_seq + dist + 1);
    j->span = j->span > dist + 1 ? j->span - (dist + 1) : 0;
  } else {
    j->head = (j->head + 1) & (JITBUF_SLOTS - 1);
  }
  return len;
}

int64_t jitbuf_next_ns(const jitbuf_t *j, uint64_t now_ns) {
  const slot_t *s = NULL;
  uint64_t due;

  if (j->count == 0) return -1;
  if (j->mode == MODE_RTP) {
    for (uint32_t d = 0; d < j->span; d++) {
      const slot_t *c = &j->slots[(uint16_t)(j->next_seq + d) & (JITBUF_SLOTS - 1)];
      if (c->used) {
        s = c;
        break;
      }
    }
    if (!s) return -1;
  } else {
    s = &j->slots[j->head];
  }
  due = s->at_ns + j->delay_ns;
  return due <= now_ns ? 0 : (int64_t)(due - now_ns);
}

uint64_t jitbuf_depth_ns(const jitbuf_t *j, uint64_t now_ns) {
  int64_t next = jitbuf_next_ns(j, now_ns);
  if (next < 0) return 0;
  return (uint64_t)next >= j->delay_ns ? 0 : j->delay_ns - (uint64_t)next;
}

size_t jitbuf_queued(const jitbuf_t *j) { return j->count; }

void jitbuf_stats(const jitbuf_t *j, jitbuf_stats_t *st) { *st = j->st; }
