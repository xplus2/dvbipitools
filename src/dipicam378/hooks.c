/* Copyright 2026 dvbipitools authors. Licensed under GPL-3.0-or-later.
 * See NOTICE and LICENSE for details and authorship information. */

#include "lib/sys/signal.h"

#include "hooks.h"
#include "version.h"

void cam378_push_metrics(metrics_exporter_t *mx, const cs378x_server_t *srv, device_state_t *dev, const char *algo_name) {
  cs378x_metrics_t m;
  metrics_writer_t w;

  if (!metrics_exporter_due(mx, mono_seconds()) || metrics_exporter_begin(mx, &w, TOOL_VERSION))
    return;
  cs378x_server_get_metrics(srv, &m);
  metrics_writer_put(&w, METRICS_ID_CAM_CONNECTIONS_ACTIVE, NULL, m.connections_active);
  metrics_writer_put(&w, METRICS_ID_CAM_CONNECTIONS_TOTAL, NULL, m.connections_total);
  for (int i = 0; i < CAM_AUTH_REASON_COUNT; i++) {
    if (m.auth_errors_total[i])
      metrics_writer_put(&w, METRICS_ID_CAM_AUTH_ERRORS_TOTAL, cs378x_auth_reason_name((cam_auth_reason_t)i), m.auth_errors_total[i]);
  }
  metrics_writer_put(&w, METRICS_ID_CAM_SERVICES_ACTIVE, NULL, device_state_services_active(dev));
  metrics_writer_put(&w, METRICS_ID_CAS_ECM_TOTAL, algo_name, m.ecm_total);
  metrics_writer_put(&w, METRICS_ID_CAS_ECM_ERRORS_TOTAL, algo_name, m.ecm_errors_total);
  metrics_writer_put(&w, METRICS_ID_CAS_EMM_TOTAL, algo_name, m.emm_total);
  metrics_exporter_send(mx, &w);
}

int cam378_ecm_cb(const unsigned char *ecm, size_t ecm_len, unsigned srvid, unsigned caid, unsigned prid, unsigned char cw_out[16], void *user) {
  (void)prid;
  return device_resolve_cw((device_state_t *)user, ecm, ecm_len, srvid, caid, cw_out);
}

void cam378_emm_cb(const unsigned char *emm, size_t emm_len, unsigned caid, unsigned provid, void *user) {
  (void)caid;
  (void)provid;
  device_on_emm((device_state_t *)user, emm, emm_len);
}
