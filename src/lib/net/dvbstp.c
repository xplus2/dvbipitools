/* Copyright 2026 dvbipitools authors. Licensed under GPL-3.0-or-later.
 * See NOTICE and LICENSE for details and authorship information. */

#include <stdint.h>
#include <stdlib.h>
#include <string.h>

#include "../demux/crc32.h"
#include "dvbstp.h"

size_t dvbstp_parse_header(const unsigned char *buf, size_t len, dvbstp_header_t *h) {
  unsigned ver, priv_words, compr;
  size_t hdrlen;
  if (len < 12)
    return 0;
  ver = (buf[0] >> 6) & 0x03;
  if (ver != 0)
    return 0;
  compr = (buf[11] >> 5) & 0x07;
  if (compr != 0) /* only "none" is valid for payload ids 0x01/0x02, clause 5.4.1.3.4 table 12 */
    return 0;

  memset(h, 0, sizeof *h);
  h->crc_present = buf[0] & 0x01;
  h->total_segment_size = ((unsigned)buf[1] << 16) | ((unsigned)buf[2] << 8) | buf[3];
  h->payload_id = buf[4];
  h->segment_id = ((unsigned)buf[5] << 8) | buf[6];
  h->segment_version = buf[7];
  h->section_number = ((unsigned)buf[8] << 4) | (buf[9] >> 4);
  h->last_section_number = (((unsigned)buf[9] & 0x0F) << 8) | buf[10];
  h->has_provider_id = (buf[11] >> 4) & 0x01;

  hdrlen = 12;
  if (h->has_provider_id) {
    if (len < hdrlen + 4)
      return 0;
    h->provider_id = ((unsigned)buf[12] << 24) | ((unsigned)buf[13] << 16) | ((unsigned)buf[14] << 8) | buf[15];
    hdrlen += 4;
  }
  priv_words = buf[11] & 0x0F;
  hdrlen += 4 * (size_t)priv_words;
  if (len < hdrlen)
    return 0;
  return hdrlen;
}

int dvbstp_send_segment(mcast_t *m, unsigned payload_id, unsigned segment_id, unsigned segment_version, int has_provider_id, unsigned provider_id, int want_crc, const unsigned char *data, size_t len) {
  size_t nsections, i;
  unsigned last_section;
  uint32_t crc = 0;

  nsections = len ? (len + DVBSTP_MAX_SECTION - 1) / DVBSTP_MAX_SECTION : 1;
  if (nsections > 4096) /* section_number is 12 bit */
    return -1;
  last_section = (unsigned)(nsections - 1);
  if (want_crc)
    crc = crc32_mpeg(data, len);

  for (i = 0; i < nsections; i++) {
    unsigned char pkt[16 + DVBSTP_MAX_SECTION + 4];
    size_t off = i * DVBSTP_MAX_SECTION;
    size_t seclen = len - off;
    size_t hpos;
    int is_last = (i == last_section);
    int crc_here = want_crc && is_last;

    if (seclen > DVBSTP_MAX_SECTION)
      seclen = DVBSTP_MAX_SECTION;

    pkt[0] = (unsigned char)(crc_here ? 0x01 : 0x00);
    pkt[1] = (unsigned char)((len >> 16) & 0xFF);
    pkt[2] = (unsigned char)((len >> 8) & 0xFF);
    pkt[3] = (unsigned char)(len & 0xFF);
    pkt[4] = (unsigned char)(payload_id & 0xFF);
    pkt[5] = (unsigned char)((segment_id >> 8) & 0xFF);
    pkt[6] = (unsigned char)(segment_id & 0xFF);
    pkt[7] = (unsigned char)(segment_version & 0xFF);
    pkt[8] = (unsigned char)((i >> 4) & 0xFF);
    pkt[9] = (unsigned char)(((i & 0x0F) << 4) | ((last_section >> 8) & 0x0F));
    pkt[10] = (unsigned char)(last_section & 0xFF);
    pkt[11] = (unsigned char)(has_provider_id ? 0x10 : 0x00);
    hpos = 12;
    if (has_provider_id) {
      pkt[12] = (unsigned char)((provider_id >> 24) & 0xFF);
      pkt[13] = (unsigned char)((provider_id >> 16) & 0xFF);
      pkt[14] = (unsigned char)((provider_id >> 8) & 0xFF);
      pkt[15] = (unsigned char)(provider_id & 0xFF);
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
    if (mcast_send(m, pkt, hpos) < 0)
      return -1;
  }
  return 0;
}

#define REASM_SLOTS 8
#define REASM_MAX_SECTIONS 64
#define REASM_MAX_LEN (REASM_MAX_SECTIONS * DVBSTP_MAX_SECTION)

typedef struct {
  int used;
  unsigned payload_id, segment_id, version;
  unsigned last_section_number;
  unsigned char have[REASM_MAX_SECTIONS];
  unsigned sec_len[REASM_MAX_SECTIONS];
  unsigned char buf[REASM_MAX_LEN];
} reasm_slot_t;

struct dvbstp_reasm {
  reasm_slot_t slots[REASM_SLOTS];
  unsigned char assembled[REASM_MAX_LEN];
};

dvbstp_reasm_t *dvbstp_reasm_new(void) { return calloc(1, sizeof(dvbstp_reasm_t)); }
void dvbstp_reasm_free(dvbstp_reasm_t *r) { free(r); }

static void slot_reset(reasm_slot_t *s, const dvbstp_header_t *h) {
  memset(s, 0, sizeof *s);
  s->used = 1;
  s->payload_id = h->payload_id;
  s->segment_id = h->segment_id;
  s->version = h->segment_version;
  s->last_section_number = h->last_section_number;
}

int dvbstp_reasm_feed(dvbstp_reasm_t *r, const unsigned char *pkt, size_t len, dvbstp_header_t *out_header, const unsigned char **out_data, size_t *out_len) {
  dvbstp_header_t h;
  size_t hdrlen, paylen, o;
  const unsigned char *payload;
  reasm_slot_t *s = NULL;
  int i, free_slot = -1;

  hdrlen = dvbstp_parse_header(pkt, len, &h);
  if (!hdrlen)
    return 0;
  if (h.last_section_number >= REASM_MAX_SECTIONS || h.section_number > h.last_section_number)
    return 0;
  if (h.crc_present && h.section_number != h.last_section_number)
    return 0; /* crc flag only valid on the final section, clause 5.4.1.2 */

  payload = pkt + hdrlen;
  paylen = len - hdrlen;
  if (h.crc_present) {
    if (paylen < 4)
      return 0;
    paylen -= 4;
  }
  if (paylen > DVBSTP_MAX_SECTION)
    return 0;

  for (i = 0; i < REASM_SLOTS; i++) {
    if (r->slots[i].used && r->slots[i].payload_id == h.payload_id && r->slots[i].segment_id == h.segment_id) {
      s = &r->slots[i];
      break;
    }
    if (!r->slots[i].used && free_slot < 0)
      free_slot = i;
  }
  if (!s) {
    s = &r->slots[free_slot >= 0 ? free_slot : 0];
    slot_reset(s, &h);
  } else if (s->version != h.segment_version || s->last_section_number != h.last_section_number) {
    slot_reset(s, &h);
  }

  if (s->have[h.section_number])
    return 0;
  memcpy(s->buf + (size_t)h.section_number * DVBSTP_MAX_SECTION, payload, paylen);
  s->sec_len[h.section_number] = (unsigned)paylen;
  s->have[h.section_number] = 1;

  for (i = 0; i <= (int)s->last_section_number; i++)
    if (!s->have[i])
      return 0;

  o = 0;
  for (i = 0; i <= (int)s->last_section_number; i++) {
    memcpy(r->assembled + o, s->buf + (size_t)i * DVBSTP_MAX_SECTION, s->sec_len[i]);
    o += s->sec_len[i];
  }
  if (h.crc_present) {
    uint32_t want = crc32_mpeg(r->assembled, o);
    const unsigned char *crcp = pkt + len - 4;
    uint32_t got = ((unsigned)crcp[0] << 24) | ((unsigned)crcp[1] << 16) | ((unsigned)crcp[2] << 8) | crcp[3];
    if (want != got) {
      s->used = 0;
      return 0;
    }
  }

  *out_data = r->assembled;
  *out_len = o;
  if (out_header)
    *out_header = h;
  s->used = 0; /* free for the next cycle's repeat, or a version bump */
  return 1;
}
