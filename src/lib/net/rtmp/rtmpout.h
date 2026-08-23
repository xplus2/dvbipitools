/* Copyright 2026 dvbipitools authors. Licensed under GPL-3.0-or-later.
 * See NOTICE and LICENSE for details and authorship information. */

#ifndef DVBIPITOOLS_LIB_NET_RTMPOUT_H
#define DVBIPITOOLS_LIB_NET_RTMPOUT_H

#include <stddef.h>
#include <stdint.h>

#include "lib/mux/flv/flv.h"

typedef struct {
  const char *url; /* rtmp://host[:port]/app/key or rtmps://... */
  int insecure;    /* rtmps:// only */
} rtmpout_cfg_t;

typedef struct rtmpout rtmpout_t;

/* NULL: malformed url or alloc fail. connects lazily from first write, backs off on failure */
rtmpout_t *rtmpout_open(const rtmpout_cfg_t *cfg);

/* one target per rtmpout_t, fan out yourself for several. 0 sent or held back for keyframe/reconn, -1 no conn, non-fatal */
int rtmpout_write(rtmpout_t *o, flv_tag_type_t type, uint32_t timestamp_ms, const unsigned char *data, size_t len);

void rtmpout_close(rtmpout_t *o);

#endif
