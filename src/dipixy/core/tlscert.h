/* Copyright 2026 dvbipitools authors. Licensed under GPL-3.0-or-later.
 * See NOTICE and LICENSE for details and authorship information. */

#ifndef DIPIXY_TLSCERT_H
#define DIPIXY_TLSCERT_H

/* explicit_cert/explicit_key if both given, else searches cwd,
   /etc/dvbipitools/, /etc/dvbipitools/dipixy/ for server.crt/server.key.
   1 ok (cert_out/key_out point at static buffers), 0 nothing usable found (no err) */
int tlscert_find(const char *explicit_cert, const char *explicit_key, const char **cert_out, const char **key_out);

#endif
