/* Copyright 2026 dvbipitools authors. Licensed under GPL-3.0-or-later.
 * See NOTICE and LICENSE for details and authorship information. */

#include "announce_driver.h"
#include "lib/helper/log.h"
#include "lib/helper/signal.h"

int announce_driver_run(const announce_driver_t *d) {
  mcast_t *m;
  unsigned cycles = 0;
  int rc = 0;

  m = mcast_open_send(d->family, d->mcast_group, d->mcast_port, d->iface, 0);
  if (!m) {
    log_line("cannot open %s:%u for sending", d->mcast_group, d->mcast_port);
    d->cleanup(d->ctx);
    return 1;
  }
  mcast_set_tos(m, d->dscp);
  if (d->on_ready) d->on_ready(d->ctx, m);
  while (!signal_stop_requested()) {
    if (signal_reload_requested()) d->reload(d->ctx);
    if (d->run_cycle(d->ctx, m, cycles + 1)) {
      rc = 1;
      break;
    }
    cycles++;
    sleep_interruptible((double)d->interval_s);
  }

  d->cleanup(d->ctx);
  mcast_close(m);
  log_line("stopped after %u cycle%s", cycles, cycles == 1 ? "" : "s");
  return rc;
}
