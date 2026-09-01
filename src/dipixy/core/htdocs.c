/* Copyright 2026 dvbipitools authors. Licensed under GPL-3.0-or-later.
 * See NOTICE and LICENSE for details and authorship information. */

#include "htdocs.h"

#include "../version.h"

#include "lib/helper/ioutil.h"
#include "lib/helper/log.h"
#include "lib/helper/signal.h"

#include <stdatomic.h>
#include <stdlib.h>

typedef struct {
  const char *buf;
  size_t len;
} htdocs_page_t;

static htdocs_page_t g_builtin_page;
static _Atomic(htdocs_page_t *) g_page;
static const char *g_path;

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
  atomic_store_explicit(&g_page, p, memory_order_release);
  log_line(TOOL_NAME ": SIGHUP: --status-tpl reloaded");
  /* old page/buf never freed: a reader may still hold its pointer */
}

void htdocs_get(const char **buf, size_t *len) {
  const htdocs_page_t *p = atomic_load_explicit(&g_page, memory_order_acquire);
  *buf = p->buf;
  *len = p->len;
}
