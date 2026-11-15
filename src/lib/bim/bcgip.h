/* Copyright 2026 dvbipitools authors. Licensed under GPL-3.0-or-later.
 * See NOTICE and LICENSE for details and authorship information. */

#ifndef DVBIPITOOLS_LIB_BIM_BCGIP_H
#define DVBIPITOOLS_LIB_BIM_BCGIP_H

#include <stddef.h>
#include <stdint.h>

#include "bimio.h"
#include "codec.h"
#include "decoderinit.h"
#include "tvainit.h"

/* BCG over IP, TS 102 539: Data Delivery Units of TV-Anytime fragments */

/* TS 102 323 7.3.1.5 compression_wrapper. deflate NULL or failing, or size over 24 bit: not compressed.
   *out mallocd. 0 ok, -1 oom */
int bim_bcg_wrap(const unsigned char *container, size_t len, bim_deflate_fn deflate, unsigned char **out, size_t *out_len);
/* inverse. Zlib needs inflate. *out mallocd. 0 ok, -1 malformed, unsupported method or oom */
int bim_bcg_unwrap(const unsigned char *data, size_t len, bim_inflate_fn inflate, unsigned char **out, size_t *out_len);

/* payload 0xA1, clause 7.3: DVB-TVA-init (EncodingVersion 0xF0) followed by DecoderInit */
int bim_bcg_tvainit_write(const bim_tvainit_t *i, const bim_decoderinit_t *di, bitwriter_t *bw, bim_err_t *err);
/* di freed with bim_decoderinit_free. 0 ok, -1 error (err) */
int bim_bcg_tvainit_parse(const unsigned char *data, size_t len, bim_tvainit_t *i, bim_decoderinit_t *di, bim_err_t *err);

#define BIM_BCG_ERR "LE400313"

#endif
