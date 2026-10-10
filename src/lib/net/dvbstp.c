/* Copyright 2026 dvbipitools authors. Licensed under GPL-3.0-or-later.
 * See NOTICE and LICENSE for details and authorship information. */

#include <stdint.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

#include "../demux/crc32.h"
#include "../helper/log.h"
#include "dvbstp.h"

/* gap between sections of one segment, keeps receivers' socket buffers from overflowing */
#define SECTION_GAP_NS 1000000L

size_t dvbstp_parse_header(const unsigned char *buf, size_t len, dvbstp_header_t *h) {
  unsigned ver;
  unsigned priv_words;
  unsigned compr;
  unsigned payload_id;
  size_t hdrlen;
  if (len < 12) return 0;
  ver = (buf[0] >> 6) & 0x03;
  if (ver != 0) return 0;
  payload_id = buf[4];
  compr = (buf[11] >> 5) & 0x07;
  if (compr != 0 && (payload_id == DVBSTP_PAYLOAD_SP_DISCOVERY || payload_id == DVBSTP_PAYLOAD_BROADCAST_DISCOVERY))
    return 0; /* only "none" is valid for these two, clause 5.4.1.3.4 table 12 */

  memset(h, 0, sizeof *h);
  h->crc_present = buf[0] & 0x01;
  h->total_segment_size = ((unsigned)buf[1] << 16) | ((unsigned)buf[2] << 8) | buf[3];
  h->payload_id = payload_id;
  h->compr = compr;
  h->segment_id = ((unsigned)buf[5] << 8) | buf[6];
  h->segment_version = buf[7];
  h->section_number = ((unsigned)buf[8] << 4) | (buf[9] >> 4);
  h->last_section_number = (((unsigned)buf[9] & 0x0F) << 8) | buf[10];
  h->has_provider_id = (buf[11] >> 4) & 0x01;

  hdrlen = 12;
  if (h->has_provider_id) {
    if (len < hdrlen + 4) return 0;
    h->provider_id = ((unsigned)buf[12] << 24) | ((unsigned)buf[13] << 16) | ((unsigned)buf[14] << 8) | buf[15];
    hdrlen += 4;
  }
  priv_words = buf[11] & 0x0F;
  hdrlen += 4 * (size_t)priv_words;
  if (len < hdrlen) return 0;
  return hdrlen;
}

int dvbstp_send_segment(mcast_t *m, const dvbstp_send_t *seg, const unsigned char *data, size_t len) {
  size_t nsections;
  size_t i;
  unsigned last_section;
  uint32_t crc = 0;

  nsections = len ? (len + DVBSTP_MAX_SECTION - 1) / DVBSTP_MAX_SECTION : 1;
  if (nsections > 4096 || len > DVBSTP_MAX_SEGMENT) return -1; /* section_number is 12 bit */

  last_section = (unsigned)(nsections - 1);
  if (seg->want_crc) crc = crc32_mpeg(data, len);

  for (i = 0; i < nsections; i++) {
    unsigned char pkt[16 + DVBSTP_MAX_SECTION + 4];
    size_t off = i * DVBSTP_MAX_SECTION;
    size_t seclen = len - off;
    size_t hpos;
    int is_last = (i == last_section);
    int crc_here = seg->want_crc && is_last;

    if (seclen > DVBSTP_MAX_SECTION) seclen = DVBSTP_MAX_SECTION;

    pkt[0] = (unsigned char)(crc_here ? 0x01 : 0x00);
    pkt[1] = (unsigned char)((len >> 16) & 0xFF);
    pkt[2] = (unsigned char)((len >> 8) & 0xFF);
    pkt[3] = (unsigned char)(len & 0xFF);
    pkt[4] = (unsigned char)(seg->payload_id & 0xFF);
    pkt[5] = (unsigned char)((seg->segment_id >> 8) & 0xFF);
    pkt[6] = (unsigned char)(seg->segment_id & 0xFF);
    pkt[7] = (unsigned char)(seg->segment_version & 0xFF);
    pkt[8] = (unsigned char)((i >> 4) & 0xFF);
    pkt[9] = (unsigned char)(((i & 0x0F) << 4) | ((last_section >> 8) & 0x0F));
    pkt[10] = (unsigned char)(last_section & 0xFF);
    pkt[11] = (unsigned char)(((seg->compr & 0x07) << 5) | (seg->has_provider_id ? 0x10 : 0x00));
    hpos = 12;
    if (seg->has_provider_id) {
      pkt[12] = (unsigned char)((seg->provider_id >> 24) & 0xFF);
      pkt[13] = (unsigned char)((seg->provider_id >> 16) & 0xFF);
      pkt[14] = (unsigned char)((seg->provider_id >> 8) & 0xFF);
      pkt[15] = (unsigned char)(seg->provider_id & 0xFF);
      hpos = 16;
    }
    memcpy(pkt + hpos, data + off, seclen);
    hpos += seclen;
    if (crc_here) {
      pkt[hpos++] = (unsigned char)((crc >> 24) & 0xFF);
      pkt[hpos++] = (unsigned char)((crc >> 16) & 0xFF);
      pkt[hpos++] = (unsigned char)((crc >> 8) & 0xFF);
      pkt[hpos++] = (unsigned char)(crc & 0xFF);
    }
    if (mcast_send(m, pkt, hpos) < 0) return -1;
    if (!is_last) {
      struct timespec gap = {0, SECTION_GAP_NS};
      nanosleep(&gap, NULL);
    }
  }
  return 0;
}

#define REASM_SLOTS 8

typedef struct {
  int used;
  unsigned payload_id;
  unsigned segment_id;
  int has_provider_id;
  unsigned provider_id;
  unsigned version;
  unsigned last_section_number;
  unsigned total_segment_size;
  unsigned have_count;
  size_t bytes;
  unsigned char **sec; /* NULL until section arrives */
  unsigned *sec_len;
  int crc_present; /* set when final section seen, whatever its arrival order */
  uint32_t crc;
} reasm_slot_t;

struct dvbstp_reasm {
  reasm_slot_t slots[REASM_SLOTS];
  unsigned char *assembled;
  size_t assembled_cap;
  int malformed_logged; /* re-armed on next accepted packet */
  int slots_full_logged; /* re-armed once a slot is free again */
  unsigned next_evict;
};

dvbstp_reasm_t *dvbstp_reasm_new(void) { return calloc(1, sizeof(dvbstp_reasm_t)); }

static void slot_release(reasm_slot_t *s) {
  if (s->sec) {
    for (unsigned i = 0; i <= s->last_section_number; i++) free(s->sec[i]);
  }
  free(s->sec);
  free(s->sec_len);
  memset(s, 0, sizeof *s);
}

void dvbstp_reasm_free(dvbstp_reasm_t *r) {
  if (!r) return;
  for (int i = 0; i < REASM_SLOTS; i++) slot_release(&r->slots[i]);
  free(r->assembled);
  free(r);
}

/* first malformed/oversized packet after a healthy run. re-armed at accepted pkg */
static void log_malformed_once(dvbstp_reasm_t *r, const char *reason) {
  if (r->malformed_logged) return;
  log_line("dvbstp: rejecting malformed packet (%s)", reason);
  r->malformed_logged = 1;
}

static int slot_reset(reasm_slot_t *s, const dvbstp_header_t *h) {
  size_t n = (size_t)h->last_section_number + 1;
  slot_release(s);
  s->sec = calloc(n, sizeof *s->sec);
  s->sec_len = calloc(n, sizeof *s->sec_len);
  if (!s->sec || !s->sec_len) {
    free(s->sec);
    free(s->sec_len);
    memset(s, 0, sizeof *s);
    return -1;
  }
  s->used = 1;
  s->payload_id = h->payload_id;
  s->segment_id = h->segment_id;
  s->has_provider_id = h->has_provider_id;
  s->provider_id = h->provider_id;
  s->version = h->segment_version;
  s->last_section_number = h->last_section_number;
  s->total_segment_size = h->total_segment_size;
  return 0;
}

int dvbstp_reasm_feed(dvbstp_reasm_t *r, const unsigned char *pkt, size_t len, dvbstp_header_t *out_header, const unsigned char **out_data, size_t *out_len) {
  dvbstp_header_t h;
  size_t hdrlen;
  size_t paylen;
  size_t o;
  const unsigned char *payload;
  reasm_slot_t *s = NULL;
  int i;
  int free_slot = -1;

  hdrlen = dvbstp_parse_header(pkt, len, &h);
  if (!hdrlen) {
    log_malformed_once(r, "bad header");
    return 0;
  }
  if (h.section_number > h.last_section_number) {
    log_malformed_once(r, "bad section numbering");
    return 0;
  }
  if (h.crc_present && h.section_number != h.last_section_number) {
    log_malformed_once(r, "crc flag on non-final section"); /* clause 5.4.1.2 */
    return 0;
  }

  payload = pkt + hdrlen;
  paylen = len - hdrlen;
  if (h.crc_present) {
    if (paylen < 4) {
      log_malformed_once(r, "truncated crc");
      return 0;
    }
    paylen -= 4;
  }
  if (paylen > DVBSTP_MAX_SECTION) {
    log_malformed_once(r, "oversized section");
    return 0;
  }

  for (i = 0; i < REASM_SLOTS; i++) {
    if (r->slots[i].used && r->slots[i].payload_id == h.payload_id && r->slots[i].segment_id == h.segment_id &&
        r->slots[i].has_provider_id == h.has_provider_id && r->slots[i].provider_id == h.provider_id) {
      s = &r->slots[i];
      break;
    }
    if (!r->slots[i].used && free_slot < 0) free_slot = i;
  }
  if (!s) {
    if (free_slot >= 0) {
      s = &r->slots[free_slot];
      r->slots_full_logged = 0;
    } else {
      s = &r->slots[r->next_evict];
      r->next_evict = (r->next_evict + 1) % REASM_SLOTS;
      if (!r->slots_full_logged) {
        log_line("dvbstp: reassembly slots full (%d), evicting in-progress payload_id=0x%02x segment_id=%u", REASM_SLOTS, s->payload_id, s->segment_id);
        r->slots_full_logged = 1;
      }
    }
    if (slot_reset(s, &h) < 0) return 0;
  } else if (s->version != h.segment_version || s->last_section_number != h.last_section_number ||
             s->total_segment_size != h.total_segment_size) {
    if (slot_reset(s, &h) < 0) return 0;
  }

  if (s->sec[h.section_number]) return 0;
  if (s->bytes + paylen > DVBSTP_MAX_SEGMENT) {
    slot_release(s);
    log_malformed_once(r, "segment exceeds size cap");
    return 0;
  }
  s->sec[h.section_number] = malloc(paylen + 1);
  if (!s->sec[h.section_number]) {
    slot_release(s);
    return 0;
  }
  memcpy(s->sec[h.section_number], payload, paylen);
  s->sec_len[h.section_number] = (unsigned)paylen;
  s->bytes += paylen;
  s->have_count++;
  if (h.crc_present) {
    const unsigned char *crcp = pkt + len - 4;
    s->crc_present = 1;
    s->crc = ((uint32_t)crcp[0] << 24) | ((uint32_t)crcp[1] << 16) | ((uint32_t)crcp[2] << 8) | crcp[3];
  }
  r->malformed_logged = 0;

  if (s->have_count != s->last_section_number + 1) return 0;

  if (r->assembled_cap < s->bytes + 1) {
    unsigned char *nb = realloc(r->assembled, s->bytes + 1);
    if (!nb) {
      slot_release(s);
      return 0;
    }
    r->assembled = nb;
    r->assembled_cap = s->bytes + 1;
  }
  o = 0;
  for (i = 0; i <= (int)s->last_section_number; i++) {
    memcpy(r->assembled + o, s->sec[i], s->sec_len[i]);
    o += s->sec_len[i];
  }
  if (o != s->total_segment_size) {
    slot_release(s);
    log_malformed_once(r, "total_segment_size mismatch on reassembled segment");
    return 0;
  }
  if (s->crc_present && crc32_mpeg(r->assembled, o) != s->crc) {
    slot_release(s);
    log_malformed_once(r, "crc mismatch on reassembled segment");
    return 0;
  }

  r->assembled[o] = '\0';
  *out_data = r->assembled;
  *out_len = o;
  if (out_header) *out_header = h;
  slot_release(s); /* free for the next cycle's repeat, or a version bump */
  return 1;
}
