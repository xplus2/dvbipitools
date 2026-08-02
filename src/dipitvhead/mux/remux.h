/* Copyright 2026 dvbipitools authors. Licensed under GPL-3.0-or-later.
 * See NOTICE and LICENSE for details and authorship information. */

#ifndef DIPITVHEAD_REMUX_H
#define DIPITVHEAD_REMUX_H

#include <stddef.h>

#include "lib/demux/psi.h"

#include "../args.h"

typedef struct remux remux_t;
typedef void (*remux_packet_cb)(void *ctx, const unsigned char *pkt188);

/* psi must be discovery-ready (psi_ready()). NULL on failure (logged) */
remux_t *remux_new(const config_t *cfg, const psi_t *psi);
void remux_free(remux_t *r);

/* output pid chosen for PCR */
unsigned remux_pcr_pid_out(const remux_t *r);

struct cas;
/* attach/detach CAS orchestrator; NULL by default */
void remux_set_cas(remux_t *r, struct cas *cas);

/* feed one source-pid 188B packet. emits 0+ output packets (our pids/cc) via cb. also drives PAT/PMT/SDT/NIT resend by wall clock */
void remux_feed(remux_t *r, const unsigned char *pkt188, remux_packet_cb cb, void *ctx);

#endif
