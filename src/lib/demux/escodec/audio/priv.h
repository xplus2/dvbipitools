/* Copyright 2026 dvbipitools authors. Licensed under GPL-3.0-or-later.
 * See NOTICE and LICENSE for details and authorship information. */

#ifndef DVBIPITOOLS_LIB_DEMUX_ESCODEC_AUDIO_PRIV_H
#define DVBIPITOOLS_LIB_DEMUX_ESCODEC_AUDIO_PRIV_H

#include "../escodec.h"

int next_ac3(const esc_track_t *t, const unsigned char *d, size_t len, esc_frame_t *f);
int next_eac3(const esc_track_t *t, const unsigned char *d, size_t len, esc_frame_t *f);
int next_mpa(const esc_track_t *t, const unsigned char *d, size_t len, esc_frame_t *f);
int next_aac(esc_track_t *t, const unsigned char *d, size_t len, esc_frame_t *f);
int next_latm(esc_track_t *t, const unsigned char *d, size_t len, esc_frame_t *f);
int next_opus(esc_track_t *t, const unsigned char *d, size_t len, esc_frame_t *f);
int next_truehd(esc_track_t *t, const unsigned char *d, size_t len, esc_frame_t *f);
int next_dts(esc_track_t *t, const unsigned char *d, size_t len, esc_frame_t *f);
int next_ac4(esc_track_t *t, const unsigned char *d, size_t len, esc_frame_t *f);

#endif
