/* Copyright 2026 dvbipitools authors. Licensed under GPL-3.0-or-later.
 * See NOTICE and LICENSE for details and authorship information. */

#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <unistd.h>

#include "../helper/beutil.h"
#include "fec2022.h"

typedef struct {
  unsigned char p, x, cc, m, pt_xor;
  uint32_t ts_xor;
  uint16_t len_xor;
  uint16_t snb;
  size_t payload_len;
  unsigned count;
  unsigned char payload[FEC2022_MAX_PKT - 12];
} fec2022_col_t;

struct fec2022_enc {
  unsigned l, d;
  unsigned char pt;
  uint16_t seq;
  fec2022_col_t cols[FEC2022_MAX_L];
};

fec2022_enc_t *fec2022_enc_new(unsigned l, unsigned d, unsigned char pt) {
  fec2022_enc_t *e;

  if (l == 0 || d == 0 || l > FEC2022_MAX_L || l * d > FEC2022_MAX_LD) return NULL;
  e = calloc(1, sizeof *e);
  if (!e) return NULL;
  e->l = l;
  e->d = d;
  e->pt = pt;
  srand((unsigned)(time(NULL) ^ getpid()));
  e->seq = (uint16_t)rand();
  return e;
}

void fec2022_enc_free(fec2022_enc_t *e) { free(e); }

int fec2022_parse_ld(const char *s, unsigned *l_out, unsigned *d_out) {
  char *end;
  unsigned long l, d;

  l = strtoul(s, &end, 10);
  if (*end != ':' || l == 0) return -1;
  d = strtoul(end + 1, &end, 10);
  if (*end != '\0' || d == 0) return -1;
  if (l > FEC2022_MAX_L || l * d > FEC2022_MAX_LD) return -1;
  *l_out = (unsigned)l;
  *d_out = (unsigned)d;
  return 0;
}

size_t fec2022_enc_feed(fec2022_enc_t *e, const unsigned char *pkt, size_t len, uint32_t pts_90k, unsigned char *out, size_t cap) {
  uint16_t seq;
  fec2022_col_t *c;
  size_t plen;

  if (len < 12 || len > FEC2022_MAX_PKT || cap < FEC2022_MAX_REPAIR) return 0;
  seq = be16_get(pkt + 2);
  c = &e->cols[seq % e->l];
  plen = len - 12;

  if (c->count == 0) c->snb = seq;
  c->p ^= (pkt[0] >> 5) & 1;
  c->x ^= (pkt[0] >> 4) & 1;
  c->cc ^= pkt[0] & 0xF;
  c->m ^= (pkt[1] >> 7) & 1;
  c->pt_xor ^= pkt[1] & 0x7F;
  c->ts_xor ^= be32_get(pkt + 4);
  c->len_xor ^= (uint16_t)plen;
  for (size_t i = 0; i < plen; i++) c->payload[i] ^= pkt[12 + i];
  if (plen > c->payload_len) c->payload_len = plen;
  c->count++;
  if (c->count < e->d) return 0;
  out[0] = (unsigned char)(0x80 | (c->p << 5) | (c->x << 4) | c->cc);
  out[1] = (unsigned char)((c->m << 7) | (e->pt & 0x7F));
  be16_put(out + 2, e->seq++);
  be32_put(out + 4, pts_90k);
  be32_put(out + 8, 0);                   /* Annex E Table E.2: SSRC fixed 0 not RFC6015 random */
  be16_put(out + 12, c->snb);               /* RFC6015 SN base low */
  be16_put(out + 14, c->len_xor);           /* RFC6015 Length recovery */
  out[16] = (unsigned char)(0x80 | c->pt_xor); /* RFC6015 E, PT recovery */
  out[17] = out[18] = out[19] = 0;             /* RFC6015 Mask */
  be32_put(out + 20, c->ts_xor);            /* RFC6015 TS recovery */
  out[24] = 0;                                 /* RFC6015 N, D, Type, Index */
  out[25] = (unsigned char)e->l;               /* RFC6015 Offset = L */
  out[26] = (unsigned char)e->d;               /* RFC6015 NA = D */
  out[27] = 0;                                 /* RFC6015 SN base ext */
  memcpy(out + 28, c->payload, c->payload_len);

  plen = 28 + c->payload_len;
  memset(c, 0, sizeof *c);
  return plen;
}
