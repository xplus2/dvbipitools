/* Copyright 2026 dvbipitools authors. Licensed under GPL-3.0-or-later.
 * See NOTICE and LICENSE for details and authorship information. */

#include "htdocs.h"

#include "../version.h"
#include "../reactor/internal.h"
#include "../reactor/qsbr.h"

#include "lib/sys/ioutil.h"
#include "lib/helper/log.h"
#include "lib/sys/signal.h"

#include <stdatomic.h>
#include <stdlib.h>

typedef struct {
  const char *buf;
  size_t len;
} htdocs_page_t;

#define HTDOCS_RETIRED_MAX 8

typedef struct {
  htdocs_page_t *p;
  uint64_t mark[QSBR_MAX_WORKERS];
} htdocs_retiree_t;

static htdocs_page_t g_builtin_page;
static _Atomic(htdocs_page_t *) g_page;
static const char *g_path;
static htdocs_retiree_t g_retiring[HTDOCS_RETIRED_MAX];
static int g_retiring_n;

/* only called from the single thread that wins signal_template_reload_requested()
   for a given SIGHUP: no lock needed on g_retiring */
static void reclaim_retired(void) {
  for (int i = 0; i < g_retiring_n;) {
    if (qsbr_mark_passed(reactor_qsbr(), g_retiring[i].mark)) {
      htdocs_page_t *p = g_retiring[i].p;
      g_retiring[i] = g_retiring[--g_retiring_n];
      free((char *)p->buf);
      free(p);
    } else {
      i++;
    }
  }
}

static void retire_page(htdocs_page_t *p) {
  reclaim_retired();
  if (g_retiring_n < HTDOCS_RETIRED_MAX) {
    qsbr_mark(reactor_qsbr(), g_retiring[g_retiring_n].mark);
    g_retiring[g_retiring_n].p = p;
    g_retiring_n++;
  } else {
    log_line(TOOL_NAME ": --status-tpl reload queue full, leaking one old page");
  }
}

static int load_file(const char *path, char **out_buf, size_t *out_len) {
  FILE *f = fopen(path, "r");
  int rc;
  if (!f)
    return -1;
  rc = read_all(f, out_buf, out_len);
  fclose(f);
  return rc;
}

void htdocs_template_init(const config_t *cfg) {
  char *buf;
  size_t len;
  htdocs_page_t *p;

  g_builtin_page.buf = g_htdocs_index_html;
  g_builtin_page.len = g_htdocs_index_html_len;
  atomic_store_explicit(&g_page, &g_builtin_page, memory_order_relaxed);

  if (!cfg->status_template)
    return;
  g_path = cfg->status_template;
  if (load_file(g_path, &buf, &len)) {
    log_line(TOOL_NAME ": --status-tpl %s: cannot read, using built-in page", g_path);
    return;
  }
  p = malloc(sizeof *p);
  if (!p) {
    free(buf);
    return;
  }
  p->buf = buf;
  p->len = len;
  atomic_store_explicit(&g_page, p, memory_order_relaxed);
}

void htdocs_template_reload_check(void) {
  char *buf;
  size_t len;
  htdocs_page_t *p;
  htdocs_page_t *old;
  if (!g_path || !signal_template_reload_requested())
    return;
  if (load_file(g_path, &buf, &len)) {
    log_line(TOOL_NAME ": --status-tpl %s: reload failed, keeping previous content", g_path);
    return;
  }
  p = malloc(sizeof *p);
  if (!p) {
    free(buf);
    log_line(TOOL_NAME ": --status-tpl %s: reload failed (OOM), keeping previous content", g_path);
    return;
  }
  p->buf = buf;
  p->len = len;
  old = atomic_exchange_explicit(&g_page, p, memory_order_acq_rel);
  if (old != &g_builtin_page)
    retire_page(old);
  log_line(TOOL_NAME ": SIGHUP: --status-tpl reloaded");
}

void htdocs_get(const char **buf, size_t *len) {
  const htdocs_page_t *p = atomic_load_explicit(&g_page, memory_order_acquire);
  *buf = p->buf;
  *len = p->len;
}
