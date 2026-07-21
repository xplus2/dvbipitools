/* Copyright 2026 dvbipitools authors. Licensed under GPL-3.0-or-later.
 * See NOTICE and LICENSE for details and authorship information. */

#include <stdlib.h>
#include <time.h>

#include "lib/log.h"
#include "lib/net/dvbstp.h"
#include "lib/net/multicast.h"
#include "lib/signal.h"
#include "announce.h"
#include "input.h"
#include "lib/sds_xml.h"

#define DOC_CAP 65536

static double mono(void) {
  struct timespec t;
  clock_gettime(CLOCK_MONOTONIC, &t);
  return (double)t.tv_sec + (double)t.tv_nsec / 1e9;
}

static void sleep_interruptible(double secs) {
  double deadline = mono() + secs;
  while (mono() < deadline && !signal_stop_requested()) {
    struct timespec req = {0, 100000000L};
    nanosleep(&req, NULL);
  }
}

int announce_run(const config_t *cfg) {
  input_t in;
  mcast_t *m;
  unsigned char *broadcast_doc = NULL, *sp_doc = NULL;
  size_t broadcast_len = 0, sp_len = 0;
  unsigned cycles = 0;

  if (input_load(cfg->input_path, &in))
    return 1;

  m = mcast_open_send(cfg->family, cfg->mcast_group, cfg->mcast_port, cfg->iface, 0);
  if (!m) {
    log_line("cannot open %s:%u for sending", cfg->mcast_group, cfg->mcast_port);
    input_free(&in);
    return 1;
  }

  if (in.kind == INPUT_RAW_XML) {
    log_line("announcing raw %s (payload 0x%02x) on %s:%u every %lds", cfg->input_path, in.raw_payload_id, cfg->mcast_group, cfg->mcast_port, cfg->interval_s);
  } else {
    broadcast_doc = malloc(DOC_CAP);
    sp_doc = malloc(DOC_CAP);
    broadcast_len = sds_build_broadcast(cfg->provider, 1, in.services, in.service_count, broadcast_doc, DOC_CAP);
    sp_len = sds_build_sp(cfg->provider, cfg->offering, cfg->lang, 1, cfg->mcast_group, cfg->mcast_port, sp_doc, DOC_CAP);
    if (!broadcast_len || !sp_len) {
      log_line("SD&S document too large (max %d bytes), reduce the service list", DOC_CAP);
      free(broadcast_doc);
      free(sp_doc);
      mcast_close(m);
      input_free(&in);
      return 1;
    }
    log_line("announcing %d service%s on %s:%u every %lds", in.service_count, in.service_count == 1 ? "" : "s", cfg->mcast_group, cfg->mcast_port, cfg->interval_s);
  }

  while (!signal_stop_requested()) {
    if (in.kind == INPUT_RAW_XML) {
      dvbstp_send_segment(m, in.raw_payload_id, 1, 1, 0, 0, 1, in.raw_xml, in.raw_xml_len);
    } else {
      dvbstp_send_segment(m, DVBSTP_PAYLOAD_BROADCAST_DISCOVERY, 1, 1, 0, 0, 1, broadcast_doc, broadcast_len);
      dvbstp_send_segment(m, DVBSTP_PAYLOAD_SP_DISCOVERY, 1, 1, 0, 0, 1, sp_doc, sp_len);
    }
    cycles++;
    if (cfg->verbose)
      log_line("cycle %u sent", cycles);
    sleep_interruptible((double)cfg->interval_s);
  }

  free(broadcast_doc);
  free(sp_doc);
  mcast_close(m);
  input_free(&in);
  log_line("stopped after %u cycle%s", cycles, cycles == 1 ? "" : "s");
  return 0;
}
