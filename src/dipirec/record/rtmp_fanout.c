/* Copyright 2026 dvbipitools authors. Licensed under GPL-3.0-or-later.
 * See NOTICE and LICENSE for details and authorship information. */

#include <string.h>

#include "priv.h"

int rtmp_fanout_open(const config_t *cfg, rtmp_fanout_t *r) {
  r->n = 0;
  for (int i = 0; i < cfg->n_out; i++) {
    rtmpout_cfg_t rc;
    if (cfg->out[i].kind != OUT_RTMP && cfg->out[i].kind != OUT_RTMPS)
      continue;
    memset(&rc, 0, sizeof rc);
    rc.url = cfg->out[i].rtmp_url;
    rc.insecure = cfg->insecure_tls;
    r->out[r->n] = rtmpout_open(&rc);
    if (!r->out[r->n])
      return -1;
    r->had_error[r->n] = 0;
    r->errors_total[r->n] = 0;
    r->n++;
  }
  return 0;
}

void rtmp_fanout_cb(void *ctx, flv_tag_type_t type, uint32_t timestamp_ms, const unsigned char *data, size_t len) {
  rtmp_fanout_t *r = ctx;
  static const char *const labels[DIPIREC_MAX_OUT] = {
    "rtmp[0]", "rtmp[1]", "rtmp[2]", "rtmp[3]", "rtmp[4]", "rtmp[5]", "rtmp[6]", "rtmp[7]"
  };

  for (int i = 0; i < r->n; i++) {
    note_send_result(rtmpout_write(r->out[i], type, timestamp_ms, data, len) >= 0, &r->had_error[i], &r->errors_total[i], labels[i]);
  }
}

void rtmp_fanout_close(const rtmp_fanout_t *r) {
  for (int i = 0; i < r->n; i++)
    rtmpout_close(r->out[i]);
}
