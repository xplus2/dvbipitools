/* Copyright 2026 dvbipitools authors. Licensed under GPL-3.0-or-later.
 * See NOTICE and LICENSE for details and authorship information. */

#ifndef DIPIXY_STATUS_H
#define DIPIXY_STATUS_H

#include <stddef.h>

#include "../args.h"

/* captures argv and start time. call once, as early in main() as possible */
void dipixy_status_init(int argc, char **argv);

/* pump thread, roughly once per second: samples in/out bitrate */
void dipixy_status_tick(void);

/* JSON body for /ui/status.js, thread-local, valid until next call, no free. 0 ok, -1 fail */
int dipixy_status_render_json(const config_t *cfg, char **out, size_t *out_len);

#endif
