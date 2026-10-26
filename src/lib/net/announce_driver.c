/* Copyright 2026 dvbipitools authors. Licensed under GPL-3.0-or-later.
 * See NOTICE and LICENSE for details and authorship information. */

#include <errno.h>
#include <string.h>

#include "announce_driver.h"
#include "lib/helper/log.h"
#include "lib/sys/signal.h"

int announce_driver_run(const announce_driver_t *d) {
  mcast_t *m;
  unsigned cycles = 0;
  double next;
  int rc = 0;

  m = mcast_open_send(d->family, d->mcast_group, d->mcast_port, d->iface, d->ttl);
  if (!m) {
    log_code("LS500143", "cannot open %s:%u for sending", d->mcast_group, d->mcast_port);
    d->cleanup(d->ctx);
    return 1;
  }
  if (mcast_set_tos(m, d->dscp)) log_code("LW500251", "cannot set DSCP marking: %s", strerror(errno));
  if (d->on_ready) d->on_ready(d->ctx, m);
  next = mono_seconds();
  while (!signal_stop_requested()) {
    double wait;
    if (signal_reload_requested()) d->reload(d->ctx);
    if (d->run_cycle(d->ctx, m, cycles + 1)) {
      rc = 1;
      break;
    }
    cycles++;
    /* fixed rate: cycle time inside interval, overrun restarts from now */
    next += (double)d->interval_s;
    wait = next - mono_seconds();
    if (wait < 0) {
      next -= wait;
      wait = 0;
    }
    sleep_interruptible(wait);
  }

  d->cleanup(d->ctx);
  mcast_close(m);
  log_line("stopped after %u cycle%s", cycles, cycles == 1 ? "" : "s");
  return rc;
}
