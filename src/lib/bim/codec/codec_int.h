/* Copyright 2026 dvbipitools authors. Licensed under GPL-3.0-or-later.
 * See NOTICE and LICENSE for details and authorship information. */

#ifndef LIB_BIM_CODEC_CODEC_INT_H
#define LIB_BIM_CODEC_CODEC_INT_H

#include <ctype.h>
#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "lib/sys/ioutil.h"
#include "lib/bim/bimio.h"
#include "lib/bim/codec.h"

#define DECODING_MODES 0x0F /* lengthCodingMode 00, hasDeferredNodes 0, hasTypeCasting 0, reserved 1111 */
#define MAX_NODES 100000
#define ZLIB_BUF_MAX 1000 /* BufferSize default, TS 102 822-3-2 4.4.1 */
/* compressed chunk of a buffer of size B, zlib expansion bound */
#define ZLIB_CHUNK_MAX(size) ((size) + ((size) >> 8) + 64)

static inline int qn_name_char(int c) { return isalnum(c) || c == '_' || c == '-' || c == '.'; }

int node_put_attr(bim_node_t *n, const char *name, char *v);
/* ISO 15938-1 8.5.2.2.5.2 (case b, c-2), 8.5.2.4.3: *shunt = presence bit first,
   returns width of count-min, -1 = vluimsbf5, 0 = count not coded */
int occ_coding(const bim_part_t *p, int *shunt);
/* ISO 15938-1 8.5.4.2.1: width of value-min, -1 = vluimsbf5 */
int range_bits(const bim_range_t *r);

int dvb_flagged_string_encode(bitwriter_t *bw, strrepo_writer_t *sw, const char *s);
/* next repository string appended to b. 0 ok, -1 none or oom */
int dvb_string_append(strrepo_reader_t *sr, dstrbuf_t *b);
/* dvb_locator_decode_st, dvb_controlledterm_decode_cs appending to b, no length limit */
int dvb_locator_decode_buf(bitreader_t *br, strrepo_reader_t *sr, dvb_codec_state_t *st, dstrbuf_t *b);
int dvb_controlledterm_decode_buf(bitreader_t *br, strrepo_reader_t *sr, const dvb_cs_t *cs, dstrbuf_t *b);
/* ISO 8601 dateTime fraction in ms. -1: nonzero digit beyond ms */
int iso_fraction_ms(const char *s, long *ms);

/* basicTimePointType sections, ISO 15938-1:2002 8.5.4.2.5 */
typedef struct {
  int date;
  int neg;
  int has_month;
  int has_day;
  int time;
  int has_min;
  int has_sec;
  int has_n;
  int has_ff;
  int has_frac;
  int tz;
  uint64_t year;
  uint64_t n;
  uint64_t frac;
  unsigned month;
  unsigned day;
  unsigned hour;
  unsigned min;
  unsigned sec;
  unsigned ff;
  int tzh;
  unsigned tzm;
} m7_timepoint_t;

/* basicDurationType sections, ISO 15938-1:2002 8.5.4.2.6. has/v: H, M, S, N */
typedef struct {
  int neg;
  int has_d;
  int t;
  int has[4];
  int has_ff;
  int has_frac;
  int tz;
  uint64_t d;
  uint64_t v[4];
  uint64_t frac;
  unsigned ff;
  int tzh;
  unsigned tzm;
} m7_duration_t;

/* flags coded for s: all, M7_FLAGS: only sections patterns allow */
enum { M7T_NEG, M7T_DATE, M7T_MONTH, M7T_DAY, M7T_TIME, M7T_MIN, M7T_SEC, M7T_N, M7T_FF, M7T_FRAC, M7T_TZ };
enum { M7D_NEG, M7D_D, M7D_T, M7D_H, M7D_M, M7D_S, M7D_N, M7D_FF, M7D_FRAC, M7D_TZ };
unsigned m7_timepoint_flags(const bim_simple_t *s, unsigned compat);
unsigned m7_duration_flags(const bim_simple_t *s, unsigned compat);
#define M7_HAS(mask, f) ((mask) >> (f) & 1u)

/* 0 ok, -1 no match */
int m7_timepoint_parse(const char *s, m7_timepoint_t *t);
int m7_timepoint_render(const m7_timepoint_t *t, char *out, size_t cap);
int m7_duration_parse(const char *s, m7_duration_t *d);
int m7_duration_render(const m7_duration_t *d, char *out, size_t cap);
/* ISO 15938-1:2002 8.5.4.2.5, 8.5.4.2.6 */
int m7_timepoint_encode(const char *v, const bim_simple_t *s, unsigned compat, bitwriter_t *bw);
int m7_duration_encode(const char *v, const bim_simple_t *s, unsigned compat, bitwriter_t *bw);

#endif
