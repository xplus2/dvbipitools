/* Copyright 2026 dvbipitools authors. Licensed under GPL-3.0-or-later.
 * See NOTICE and LICENSE for details and authorship information. */

#ifndef DIPITVHEAD_AITBUILD_H
#define DIPITVHEAD_AITBUILD_H

#include <stddef.h>

#define OUT_PID_AIT 0x0020

/* PMT es-loop entry: stream_type 0x05 + application_signalling_descriptor. 0 on overflow */
size_t aitbuild_pmt_entry(unsigned version, unsigned char *out, size_t cap);

/* application_information_section, table_id 0x74. one app, AUTOSTART. 0 on overflow */
size_t aitbuild_ait(unsigned version, unsigned org_id, unsigned app_id, const char *url, unsigned char *out, size_t cap);

#endif
