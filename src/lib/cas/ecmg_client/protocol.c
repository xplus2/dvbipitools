/* Copyright 2026 dvbipitools authors. Licensed under GPL-3.0-or-later.
 * See NOTICE and LICENSE for details and authorship information. */

#include <errno.h>
#include <string.h>
#include <sys/random.h>

#include "lib/log.h"
#include "lib/mux/psi_build.h"

#include "priv.h"

static int cw_gen(unsigned char *out, size_t len) {
  size_t got = 0;
  while (got < len) {
    ssize_t n = getrandom(out + got, len - got, 0);
    if (n < 0) {
      if (errno == EINTR)
        continue;
      log_line("ecmg: getrandom: %s", strerror(errno));
      return -1;
    }
    got += (size_t)n;
  }
  return 0;
}

static const unsigned char *hist_get_or_gen(cw_hist_entry_t *hist, unsigned short cp, size_t cw_len) {
  int i = cp % ECMG_CW_HIST;
  if (hist[i].valid && hist[i].cp_number == cp)
    return hist[i].cw;
  if (cw_gen(hist[i].cw, cw_len) < 0)
    return NULL;
  hist[i].cp_number = cp;
  hist[i].valid = 1;
  return hist[i].cw;
}

int ecmg_find_error_status(const unsigned char *body, size_t body_len, unsigned short *out) {
  simulcrypt_tlv_reader_t it;
  unsigned short tag, vlen;
  const unsigned char *val;
  simulcrypt_tlv_reader_init(&it, body, body_len);
  while (simulcrypt_tlv_reader_next(&it, &tag, &val, &vlen) == 1) {
    if (tag == ECMG_P_ERROR_STATUS && vlen == 2) {
      *out = ((unsigned)val[0] << 8) | val[1];
      return 0;
    }
  }
  return -1;
}

/* 0 ok (fills all five), -1 malformed or cw_per_msg missing/out of range */
int ecmg_parse_channel_status(const unsigned char *body, size_t body_len, unsigned *out_lead_cw, unsigned *out_cw_per_msg, unsigned *out_max_comp_time_ms, unsigned *out_min_cp_100ms, unsigned *out_ecm_rep_period_ms) {
  unsigned lead_cw = 0, cw_per_msg = 0, max_comp_time_ms = 0, min_cp_100ms = 0, ecm_rep_period_ms = 0;
  simulcrypt_tlv_reader_t it;
  unsigned short tag, vlen;
  const unsigned char *val;
  int rc;
  simulcrypt_tlv_reader_init(&it, body, body_len);
  while ((rc = simulcrypt_tlv_reader_next(&it, &tag, &val, &vlen)) == 1) {
    switch (tag) {
      case ECMG_P_LEAD_CW:
        if (vlen == 1)
          lead_cw = val[0];
        break;
      case ECMG_P_CW_PER_MSG:
        if (vlen == 1)
          cw_per_msg = val[0];
        break;
      case ECMG_P_MAX_COMP_TIME:
        if (vlen == 2)
          max_comp_time_ms = ((unsigned)val[0] << 8) | val[1];
        break;
      case ECMG_P_MIN_CP_DURATION:
        if (vlen == 2)
          min_cp_100ms = ((unsigned)val[0] << 8) | val[1];
        break;
      case ECMG_P_ECM_REP_PERIOD:
        if (vlen == 2)
          ecm_rep_period_ms = ((unsigned)val[0] << 8) | val[1];
        break;
      default:
        break;
    }
  }
  if (rc < 0 || !cw_per_msg || cw_per_msg > ECMG_MAX_CW_PER_MSG)
    return -1;
  *out_lead_cw = lead_cw;
  *out_cw_per_msg = cw_per_msg;
  *out_max_comp_time_ms = max_comp_time_ms;
  *out_min_cp_100ms = min_cp_100ms;
  *out_ecm_rep_period_ms = ecm_rep_period_ms;
  return 0;
}

size_t ecmg_build_channel_setup(unsigned char *out, size_t cap, unsigned char version, unsigned super_cas_id) {
  simulcrypt_writer_t w;
  unsigned char cas_id[4];
  psi_put16(cas_id, super_cas_id >> 16);
  psi_put16(cas_id + 2, super_cas_id);
  if (simulcrypt_writer_begin(&w, out, cap, version, ECMG_MSG_CHANNEL_SETUP) < 0)
    return 0;
  if (simulcrypt_writer_put_tlv(&w, ECMG_P_ECM_CHANNEL_ID, (unsigned char[]){0, ECMG_CHANNEL_ID}, 2) < 0)
    return 0;
  if (simulcrypt_writer_put_tlv(&w, ECMG_P_SUPER_CAS_ID, cas_id, sizeof cas_id) < 0)
    return 0;
  return simulcrypt_writer_finish(&w);
}

size_t ecmg_build_stream_setup(unsigned char *out, size_t cap, unsigned char version, unsigned ecm_id, unsigned nominal_cp_100ms) {
  simulcrypt_writer_t w;
  if (simulcrypt_writer_begin(&w, out, cap, version, ECMG_MSG_STREAM_SETUP) < 0)
    return 0;
  if (simulcrypt_writer_put_tlv(&w, ECMG_P_ECM_CHANNEL_ID, (unsigned char[]){0, ECMG_CHANNEL_ID}, 2) < 0)
    return 0;
  if (simulcrypt_writer_put_tlv(&w, ECMG_P_ECM_STREAM_ID, (unsigned char[]){0, ECMG_STREAM_ID}, 2) < 0)
    return 0;
  if (simulcrypt_writer_put_tlv(&w, ECMG_P_ECM_ID, (unsigned char[]){(unsigned char)(ecm_id >> 8), (unsigned char)ecm_id}, 2) < 0)
    return 0;
  if (simulcrypt_writer_put_tlv(&w, ECMG_P_NOMINAL_CP_DURATION, (unsigned char[]){(unsigned char)(nominal_cp_100ms >> 8), (unsigned char)nominal_cp_100ms}, 2) < 0)
    return 0;
  return simulcrypt_writer_finish(&w);
}

size_t ecmg_build_cw_provision(unsigned char *out, size_t cap, unsigned char version, unsigned short cp_number,
                                  cw_hist_entry_t *hist, size_t cw_len, unsigned lead_cw, unsigned cw_per_msg) {
  simulcrypt_writer_t w;
  unsigned short first_cp = (unsigned short)(cp_number + lead_cw - cw_per_msg + 1);
  unsigned i;
  if (simulcrypt_writer_begin(&w, out, cap, version, ECMG_MSG_CW_PROVISION) < 0)
    return 0;
  if (simulcrypt_writer_put_tlv(&w, ECMG_P_ECM_CHANNEL_ID, (unsigned char[]){0, ECMG_CHANNEL_ID}, 2) < 0)
    return 0;
  if (simulcrypt_writer_put_tlv(&w, ECMG_P_ECM_STREAM_ID, (unsigned char[]){0, ECMG_STREAM_ID}, 2) < 0)
    return 0;
  if (simulcrypt_writer_put_tlv(&w, ECMG_P_CP_NUMBER, (unsigned char[]){(unsigned char)(cp_number >> 8), (unsigned char)cp_number}, 2) < 0)
    return 0;
  for (i = 0; i < cw_per_msg; i++) {
    unsigned char combo[2 + ECMG_MAX_CW_LEN];
    unsigned short cp = (unsigned short)(first_cp + i);
    const unsigned char *cw = hist_get_or_gen(hist, cp, cw_len);
    if (!cw)
      return 0;
    psi_put16(combo, cp);
    memcpy(combo + 2, cw, cw_len);
    if (simulcrypt_writer_put_tlv(&w, ECMG_P_CP_CW_COMBINATION, combo, (unsigned short)(2 + cw_len)) < 0)
      return 0;
  }
  return simulcrypt_writer_finish(&w);
}
