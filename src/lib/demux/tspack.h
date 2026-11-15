/* Copyright 2026 dvbipitools authors. Licensed under GPL-3.0-or-later.
 * See NOTICE and LICENSE for details and authorship information. */

#ifndef DVBIPITOOLS_LIB_DEMUX_TSPACK_H
#define DVBIPITOOLS_LIB_DEMUX_TSPACK_H

#include <stddef.h>
#include <stdint.h>
#include <string.h>

#define TS_PID_COUNT 8192
#define TS_PID_MAX (TS_PID_COUNT - 1)

/* pid -> slot index, max 255 */
typedef struct {
  uint8_t slot1[TS_PID_COUNT];
} ts_pidmap_t;

static inline void ts_pidmap_clear(ts_pidmap_t *m) { memset(m->slot1, 0, sizeof m->slot1); }

static inline void ts_pidmap_set(ts_pidmap_t *m, unsigned pid, int slot) {
  if (pid < TS_PID_COUNT && slot >= 0 && slot < 255) m->slot1[pid] = (uint8_t)(slot + 1);
}

static inline void ts_pidmap_unset(ts_pidmap_t *m, unsigned pid) {
  if (pid < TS_PID_COUNT) m->slot1[pid] = 0;
}

static inline int ts_pidmap_get(const ts_pidmap_t *m, unsigned pid) { return pid < TS_PID_COUNT ? (int)m->slot1[pid] - 1 : -1; }

typedef struct {
  unsigned char acc[188];
  size_t acclen;
  int locked; /* sync byte seen 188 B after a packet */
} tspack_t;

/* feed bytes, packetize to 188 B, resync on 0x47. cb !=0 stops */
int tspack_feed(tspack_t *pz, const unsigned char *d, size_t len, int (*cb)(void *, const unsigned char *), void *ctx);

typedef struct {
  uint64_t byte_errors; /* TR101290 1.2 */
  uint64_t losses;      /* TR101290 1.1 */
  unsigned bad_run;
  unsigned good_run;
  int in_sync;
  int candidate;
} tspack_sync_t;

void tspack_sync_good(tspack_sync_t *s);
void tspack_sync_bad(tspack_sync_t *s);

int tspack_feed_sync(tspack_t *pz, const unsigned char *d, size_t len, int (*cb)(void *, const unsigned char *), void *ctx, tspack_sync_t *sync);

/* 13-bit PID from a 188 B TS packet's header (pkt[1],pkt[2]) */
unsigned tspack_pid(const unsigned char *pkt);

/* payload past adaptation field: pl, plen, pusi set on success.
   0 if afc indicates no payload (0 or 2) or adaptation field length malformed. */
int tspack_payload(const unsigned char *pkt, const unsigned char **pl, size_t *plen, int *pusi);

/* 12-bit length field from 2 adjacent bytes: (p[0]&0x0F)<<8 | p[1] */
size_t tspack_length12(const unsigned char *p);

#endif
