/* Copyright 2026 dvbipitools authors. Licensed under GPL-3.0-or-later.
 * See NOTICE and LICENSE for details and authorship information. */

#ifndef DVBIPITOOLS_LIB_NET_TSSINK_H
#define DVBIPITOOLS_LIB_NET_TSSINK_H

#include <stddef.h>

typedef enum { TSSINK_UDP, TSSINK_RTP, TSSINK_STDOUT, TSSINK_FILE } tssink_kind_t;

typedef struct {
  tssink_kind_t kind;
  /* TSSINK_UDP / TSSINK_RTP */
  int family; /* AF_INET or AF_INET6 */
  const char *group;
  unsigned port;
  const char *iface; /* NULL = kernel default route */
  int ttl;            /* 0 = kernel default */
  /* TSSINK_FILE; O_TRUNC on open */
  const char *file_path;
} tssink_cfg_t;

typedef struct tssink tssink_t;

/* opens per cfg->kind. NULL on failure */
tssink_t *tssink_open(const tssink_cfg_t *cfg);

/* TSSINK_FILE/TSSINK_STDOUT: written as given. TSSINK_UDP/TSSINK_RTP: split into
   TS_PER_DGRAM*188-byte (or smaller final) datagrams, RTP header prepended for TSSINK_RTP.
   0 ok, -1 error */
int tssink_write(tssink_t *s, const unsigned char *buf, size_t n);

void tssink_close(tssink_t *s);

#endif
