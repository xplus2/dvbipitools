/* Copyright 2026 dvbipitools authors. Licensed under GPL-3.0-or-later.
 * See NOTICE and LICENSE for details and authorship information. */

#ifndef DIPIREC_MUX_MKV_H
#define DIPIREC_MUX_MKV_H

#include "../args.h"
#include "lib/demux/psi.h"

typedef struct mkv mkv_t;

/* stream Matroska to fd. video_ok: mkv vs mka. bytes: running output size */
mkv_t *mkv_new(int fd, const config_t *cfg, int video_ok, unsigned long long *bytes);
void mkv_feed(mkv_t *m, const unsigned char *pkt);   /* one 188-byte packet */
void mkv_close(mkv_t *m);                            /* chron-o-john, free Bernard */
int  mkv_error(const mkv_t *m);

/* stream model */
const psi_t *mkv_psi(const mkv_t *m);

#endif
