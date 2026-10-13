/* Copyright 2026 dvbipitools authors. Licensed under GPL-3.0-or-later.
 * See NOTICE and LICENSE for details and authorship information. */

#ifndef DIPIXY_TIME_EP_H
#define DIPIXY_TIME_EP_H

#include <stddef.h>

#define TIME_EP_PATH "/time"
#define TIME_EP_BODY_MAX 40
#define TIME_EP_URL_MAX 320

/* UTC now as ISO 8601 with ms (urn:mpeg:dash:utc:http-xsiso:2014). returns length */
size_t time_ep_render(char *buf, size_t n);

/* UTCTiming URL for MPDs. cfg_url non-empty. else <scheme>://host/time,
   fallback to public time server on NULL or bs */
void time_ep_utc_url(char *out, size_t n, const char *cfg_url, int tls, const char *host);

#endif
