/* Copyright 2026 dvbipitools authors. Licensed under GPL-3.0-or-later.
 * See NOTICE and LICENSE for details and authorship information. */

#include "priv.h"

#include <sched.h>
#include <stdatomic.h>
#include <stdlib.h>
#include <string.h>

#include "lib/helper/ioutil.h"

#include "../../core/route.h"

extern _Thread_local int t_reactor_tid;

#define CHANNELS_MAX_READER_THREADS 256

static _Atomic uint64_t g_reader_gen[CHANNELS_MAX_READER_THREADS];

static void reader_enter(void) {
  int tid = t_reactor_tid;
  if (tid >= 0 && tid < CHANNELS_MAX_READER_THREADS)
    atomic_fetch_add_explicit(&g_reader_gen[tid], 1, memory_order_release);
}

static void reader_exit(void) {
  int tid = t_reactor_tid;
  if (tid >= 0 && tid < CHANNELS_MAX_READER_THREADS)
    atomic_fetch_add_explicit(&g_reader_gen[tid], 1, memory_order_release);
}

void wait_readers_quiescent(void) {
  int i;
  for (i = 0; i < CHANNELS_MAX_READER_THREADS; i++) {
    uint64_t g = atomic_load_explicit(&g_reader_gen[i], memory_order_acquire);
    if (g & 1)
      while (atomic_load_explicit(&g_reader_gen[i], memory_order_acquire) == g)
        sched_yield();
  }
}

channels_t *channels_build(const config_t *cfg) {
  channels_t *ch = calloc(1, sizeof *ch);
  int i;
  if (!ch)
    return NULL;
  if (cfg->n_sources == 0)
    return ch;
  ch->n_lists = cfg->sources[cfg->n_sources - 1].ordinal;
  ch->lists = calloc((size_t)ch->n_lists, sizeof *ch->lists);
  if (!ch->lists) {
    free(ch);
    return NULL;
  }
  for (i = 0; i < ch->n_lists; i++) {
    channel_list_t *l = calloc(1, sizeof *l);
    if (l)
      atomic_init(&ch->lists[i], l);
  }
  for (i = 0; i < cfg->n_sources; i++) {
    channel_list_t *l = atomic_load_explicit(&ch->lists[cfg->sources[i].ordinal - 1], memory_order_relaxed);
    if (!l)
      continue;
    switch (cfg->sources[i].kind) {
      case SRC_SDS:
        build_from_sds(l, cfg->sources[i].value, cfg->iface, cfg->sds_timeout_s);
        break;
      case SRC_M3U:
        build_from_m3u(l, cfg->sources[i].value, cfg->insecure_tls);
        break;
      case SRC_XSPF:
        build_from_xspf(l, cfg->sources[i].value, cfg->insecure_tls);
        break;
      case SRC_CSV:
        build_from_csv(l, cfg->sources[i].value, cfg->insecure_tls);
        break;
      case SRC_XML:
        build_from_xml(l, cfg->sources[i].value);
        break;
      case SRC_HTTP:
        build_from_http(l, cfg->sources[i].value, cfg->insecure_tls);
        break;
    }
  }
  return ch;
}

static const channel_list_t *channels_get(const channels_t *ch, unsigned list_num) {
  if (!ch || list_num == 0 || list_num > (unsigned)ch->n_lists)
    return NULL;
  return atomic_load_explicit(&ch->lists[list_num - 1], memory_order_acquire);
}

static const channel_item_t *channel_list_get_item(const channel_list_t *l, unsigned item_num) {
  if (!l || item_num == 0 || item_num > (unsigned)l->count)
    return NULL;
  return &l->items[item_num - 1];
}

static const channel_item_t *channel_list_find_name(const channel_list_t *l, const char *name) {
  int i;
  if (!l)
    return NULL;
  for (i = 0; i < l->count; i++)
    if (strcmp(l->items[i].name, name) == 0)
      return &l->items[i];
  return NULL;
}

int channels_resolve(const channels_t *ch, unsigned list_num, unsigned item_num, const char *item_name, int *family, char *addr, size_t addrsz, unsigned *port, int *rtp, channel_ret_fcc_t *rf) {
  char uri[256];
  const channel_list_t *l;
  const channel_item_t *it;
  reader_enter();
  l = channels_get(ch, list_num);
  it = l ? (item_name ? channel_list_find_name(l, item_name) : channel_list_get_item(l, item_num)) : NULL;
  if (!it || strlen(it->uri) >= sizeof uri) {
    reader_exit();
    return -1;
  }
  bufcpy(uri, sizeof uri, it->uri);
  if (rf) {
    rf->has_ret = it->has_ret;
    rf->ret = it->ret;
    rf->has_fcc = it->has_fcc;
    rf->fcc = it->fcc;
  }
  reader_exit();
  return route_resolve_channel_uri(uri, family, addr, addrsz, port, rtp);
}

capture_ctx_t *channels_resolve_static(const channels_t *ch, unsigned list_num, unsigned item_num, const char *item_name) {
  const channel_list_t *l;
  const channel_item_t *it;
  capture_ctx_t *ctx;
  reader_enter();
  l = channels_get(ch, list_num);
  it = l ? (item_name ? channel_list_find_name(l, item_name) : channel_list_get_item(l, item_num)) : NULL;
  ctx = it ? it->static_ctx : NULL;
  if (ctx)
    ctx = capture_ref(ctx);
  reader_exit();
  return ctx;
}

int channels_list_for_each(const channels_t *ch, unsigned list_num, void (*emit)(void *ctx, const channel_item_t *item),
                            void *ctx) {
  const channel_list_t *l;
  int i;
  reader_enter();
  l = channels_get(ch, list_num);
  if (!l) {
    reader_exit();
    return 0;
  }
  if (emit)
    for (i = 0; i < l->count; i++)
      emit(ctx, &l->items[i]);
  reader_exit();
  return l->count;
}

int channels_item_lookup(const channels_t *ch, unsigned list_num, unsigned item_num, const char *item_name, unsigned *out_item_num, char *out_name, size_t out_namesz,
                         char *out_proto, size_t out_protosz, char *out_addr, size_t out_addrsz) {
  const channel_list_t *l;
  const channel_item_t *it;
  int i;

  reader_enter();
  l = channels_get(ch, list_num);
  if (!l) {
    reader_exit();
    return -1;
  }
  if (item_name) {
    it = channel_list_find_name(l, item_name);
    if (it)
      for (i = 0; i < l->count; i++)
        if (&l->items[i] == it) {
          *out_item_num = (unsigned)(i + 1);
          break;
        }
  } else {
    it = channel_list_get_item(l, item_num);
    if (it)
      *out_item_num = item_num;
  }
  if (!it) {
    reader_exit();
    return -1;
  }
  bufcpy(out_name, out_namesz, it->name);
  {
    const char *scheme_end = it->uri ? strstr(it->uri, "://") : NULL;
    if (out_proto && out_protosz) {
      size_t len = scheme_end ? (size_t)(scheme_end - it->uri) : 0;
      if (len >= out_protosz)
        len = out_protosz - 1;
      memcpy(out_proto, it->uri, len);
      out_proto[len] = '\0';
    }
    if (out_addr && out_addrsz) {
      const char *rest = scheme_end ? scheme_end + 3 : NULL;
      size_t len = 0;
      if (rest && *rest == '@')
        rest++;
      if (rest) {
        const char *slash = strchr(rest, '/');
        len = slash ? (size_t)(slash - rest) : strlen(rest);
      }
      if (len >= out_addrsz)
        len = out_addrsz - 1;
      if (rest)
        memcpy(out_addr, rest, len);
      out_addr[len] = '\0';
    }
  }
  reader_exit();
  return 0;
}
