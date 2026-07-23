/* Copyright 2026 dvbipitools authors. Licensed under GPL-3.0-or-later.
 * See NOTICE and LICENSE for details and authorship information. */

#ifndef DIPITVHEAD_BITRATE_H
#define DIPITVHEAD_BITRATE_H

typedef struct bitrate_pacer bitrate_pacer_t;

/* target_bps 0 disables both pacing and stuffing. NULL on failure */
bitrate_pacer_t *bitrate_pacer_new(double target_bps, int stuff, int burst_limit);
void bitrate_pacer_free(bitrate_pacer_t *p);

/* call before queuing a 188B packet (real or stuffing). sleeps if burst_limit and ahead of schedule */
void bitrate_pace(bitrate_pacer_t *p);

/* call once per queued 188B packet, after bitrate_pace */
void bitrate_account(bitrate_pacer_t *p);

/* whole null packets to send now to catch up to target, if stuffing enabled */
int bitrate_stuff_due(bitrate_pacer_t *p);

#endif
