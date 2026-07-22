/* Copyright 2026 dvbipitools authors. Licensed under GPL-3.0-or-later.
 * See NOTICE and LICENSE for details and authorship information. */

#ifndef DIPIBIM_CODEC_H
#define DIPIBIM_CODEC_H

#include <stddef.h>

#include "bitreader.h"
#include "bitwriter.h"
#include "strrepo.h"

/* dvbStringCodec, TS 102 323 9.4.3.3 */
int dvb_string_encode(strrepo_writer_t *sw, const char *s);
int dvb_string_decode(strrepo_reader_t *sr, char *out, size_t outcap);

/* dvbLocatorCodec, TS 102 323 9.4.3.4, string-fallback branch only */
int dvb_locator_encode(bitwriter_t *bw, strrepo_writer_t *sw, const char *uri);
int dvb_locator_decode(bitreader_t *br, strrepo_reader_t *sr, char *out, size_t outcap);

/* dvbDateTimeCodec, dateTime_flag==00 branch only, full precision */
int dvb_datetime_encode(bitwriter_t *bw, const char *iso8601);
int dvb_datetime_decode(bitreader_t *br, char *out, size_t outcap);

/* dvbControlledTermCodec, TS 102 323 9.4.3.7 */
int dvb_controlledterm_encode(bitwriter_t *bw, strrepo_writer_t *sw, const char *href);
int dvb_controlledterm_decode(bitreader_t *br, strrepo_reader_t *sr, char *out, size_t outcap);

#endif
