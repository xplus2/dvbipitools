/* Copyright 2026 dvbipitools authors. Licensed under GPL-3.0-or-later.
 * See NOTICE and LICENSE for details and authorship information. */

#ifndef DIPIBCG_LISTEN_H
#define DIPIBCG_LISTEN_H

#include "lib/net/dvbstp_seen.h"
#include "lib/tva/bcg_doc.h"

#include "args.h"

int listen_run(const config_t *cfg);

/* one line per channel with a uri: id,uri,tsid,onid,sid. logs and returns on open failure */
void write_csvmap(const char *path, const bcg_doc_t *doc);

#endif
