/* Copyright 2026 dvbipitools authors. Licensed under GPL-3.0-or-later.
 * See NOTICE and LICENSE for details and authorship information. */

#ifndef DIPIXY_CHANNELS_H
#define DIPIXY_CHANNELS_H

#include <stdatomic.h>
#include <stddef.h>

#include "lib/helper/sds_xml.h"

#include "../../args.h"
#include "../capture/capture.h"

typedef struct {
  char *name; /* malloc'd */
  char *uri;  /* malloc'd, e.g. rtp://@239.0.0.1:8000 */
  char *icon_uri; /* malloc'd, NULL if absent. m3u/xspf/csv sources only */
  unsigned tsid, onid, sid;
  unsigned max_bitrate_kbps;
  int has_bitrate;
  unsigned content_nibble;
  int has_content_nibble;
  int has_ret;
  sds_ret_t ret;
  int has_fcc;
  sds_fcc_t fcc;
  capture_ctx_t *static_ctx; /* srt/http items only, opened at channels_build() */
} channel_item_t;

typedef struct {
  int has_ret;
  sds_ret_t ret;
  int has_fcc;
  sds_fcc_t fcc;
} channel_ret_fcc_t;

typedef struct {
  channel_item_t *items;
  int count;
  int cap;
} channel_list_t;

typedef struct {
  _Atomic(channel_list_t *) *lists; /* index = ordinal-1, sized to max ordinal in cfg->sources */
  int n_lists;
} channels_t;

/* list index = source's ordinal (-i position), 1-based.
   -/rist:// slot or failed source: empty list at that number. NULL only on alloc failure */
channels_t *channels_build(const config_t *cfg);

/* spawns background thread: re-runs SDS discovery per --sds source on interval.
   rebuild src on SIGHUP, swap results to ch under internal lock. noop w/ no sources */
void channels_start_refresh(channels_t *ch, const config_t *cfg);

/* re-reads every source (same as a SIGHUP), swaps results into ch under an internal lock */
void channels_reload_all(channels_t *ch, const config_t *cfg);

/* stops and joins refresh thread, safe even if never started. must run before channels_free() */
void channels_stop_refresh(void);

/* 1 if refresh thread running, 0 otherwise */
int channels_refresh_active(void);

/* list_num/item_num 1-based (URL convention).
   item_name non-NULL: sel by name[item_num]. rf: NULL ok. thread-safe vs concurrent refresh. 0 ok, -1 not found/malformed */
int channels_resolve(const channels_t *ch, unsigned list_num, unsigned item_num, const char *item_name, int *family, char *addr, size_t addrsz, unsigned *port, int *rtp, channel_ret_fcc_t *rf);

/* same params as channels_resolve(). srt/http items only: ref++'d ctx instead of family/addr/port. NULL otherwise */
capture_ctx_t *channels_resolve_static(const channels_t *ch, unsigned list_num, unsigned item_num, const char *item_name);

/* emit(ctx,item) per item, locked vs refresh. NULL emit: count only, no walk. ret count, 0 if empty/invalid */
int channels_list_for_each(const channels_t *ch, unsigned list_num, void (*emit)(void *ctx, const channel_item_t *item), void *ctx);

/* list_num/item_num 1-based. item_name set: match by name not item_num.
   out_item_num: 1-based position on success. out_name: item name, empty if none. 0 ok, -1 not found */
int channels_item_lookup(const channels_t *ch, unsigned list_num, unsigned item_num, const char *item_name,
                         unsigned *out_item_num, char *out_name, size_t out_namesz, char *out_proto, size_t out_protosz, char *out_addr, size_t out_addrsz);

void channels_free(channels_t *ch);

#endif
