/* Copyright 2026 dvbipitools authors. Licensed under GPL-3.0-or-later.
 * See NOTICE and LICENSE for details and authorship information. */

#ifndef DIPIXY_ALTSVC_H
#define DIPIXY_ALTSVC_H

/* call before workers start. port=0 turns it off */
void altsvc_set(unsigned port);

/* "Alt-Svc: ...\r\n" line of TLS HTTP/1.x responses, empty if off or non-tls */
const char *altsvc_h1_line(int is_tls);

/* HTTP/2 header val, NULL if off */
const char *altsvc_value(void);

#endif /* DIPIXY_ALTSVC_H */
