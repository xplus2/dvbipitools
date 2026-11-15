/* Copyright 2026 dvbipitools authors. Licensed under GPL-3.0-or-later.
 * See NOTICE and LICENSE for details and authorship information. */

#include <errno.h>
#include <pthread.h>
#include <stdarg.h>
#include <stdatomic.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <unistd.h>

#include "log.h"
#include "lib/helper/dateutil.h"

#define LOG_MSG_MAX 4096
#define LOG_LINE_MAX (LOG_MSG_MAX + 64)
#define LOG_QUEUE_SLOTS 256

static _Atomic long g_pid;
static pthread_once_t g_pid_once = PTHREAD_ONCE_INIT;

static void pid_reset(void) { atomic_store_explicit(&g_pid, 0, memory_order_relaxed); }
static void pid_once(void) { pthread_atfork(NULL, NULL, pid_reset); }

/* "YYYYMMDD HHMMSS pid ", recomputed once a second or after fork */
static size_t prefix(char *out) {
  static _Thread_local time_t t_last = (time_t)-1;
  static _Thread_local long t_pid;
  static _Thread_local char t_pfx[48];
  static _Thread_local size_t t_len;
  time_t now = time(NULL);
  long pid = atomic_load_explicit(&g_pid, memory_order_relaxed);
  if (!pid) {
    pthread_once(&g_pid_once, pid_once);
    pid = (long)getpid();
    atomic_store_explicit(&g_pid, pid, memory_order_relaxed);
  }
  if (now != t_last || pid != t_pid) {
    struct tm tm;
    gmtime_r(&now, &tm);
    t_len = strftime(t_pfx, sizeof t_pfx, "%Y%m%d %H%M%S ", &tm);
    t_len += (size_t)snprintf(t_pfx + t_len, sizeof t_pfx - t_len, "%ld ", pid);
    t_last = now;
    t_pid = pid;
  }
  memcpy(out, t_pfx, t_len);
  return t_len;
}

typedef struct {
  _Atomic size_t seq;
  size_t len;
  char line[LOG_LINE_MAX];
} log_cell_t;

static log_cell_t *g_q;
static _Atomic size_t g_q_head;
static _Atomic size_t g_q_tail;
static _Atomic unsigned long g_q_dropped;
static _Atomic int g_q_on;
static _Atomic int g_q_stop;
static _Atomic int g_q_sleeping;
static pthread_t g_q_thread;
static pthread_mutex_t g_q_mtx = PTHREAD_MUTEX_INITIALIZER;
static pthread_cond_t g_q_cv = PTHREAD_COND_INITIALIZER;

static void write_all(const char *p, size_t n) {
  while (n) {
    ssize_t w = write(STDERR_FILENO, p, n);
    if (w < 0) {
      if (errno == EINTR) continue;
      return;
    }
    p += w;
    n -= (size_t)w;
  }
}

static int q_push(const char *line, size_t len) {
  size_t pos = atomic_load_explicit(&g_q_head, memory_order_relaxed);
  for (;;) {
    log_cell_t *c = &g_q[pos & (LOG_QUEUE_SLOTS - 1)];
    size_t seq = atomic_load_explicit(&c->seq, memory_order_acquire);
    intptr_t dif = (intptr_t)seq - (intptr_t)pos;
    if (dif == 0) {
      if (atomic_compare_exchange_weak_explicit(&g_q_head, &pos, pos + 1, memory_order_relaxed, memory_order_relaxed)) {
        memcpy(c->line, line, len);
        c->len = len;
        atomic_store_explicit(&c->seq, pos + 1, memory_order_release);
        if (atomic_load_explicit(&g_q_sleeping, memory_order_relaxed)) pthread_cond_signal(&g_q_cv);
        return 0;
      }
    } else if (dif < 0) {
      return -1;
    } else {
      pos = atomic_load_explicit(&g_q_head, memory_order_relaxed);
    }
  }
}

/* single consumer */
static int q_drain(void) {
  int n = 0;
  unsigned long dropped;
  for (;;) {
    size_t pos = atomic_load_explicit(&g_q_tail, memory_order_relaxed);
    log_cell_t *c = &g_q[pos & (LOG_QUEUE_SLOTS - 1)];
    if (atomic_load_explicit(&c->seq, memory_order_acquire) != pos + 1) break;
    write_all(c->line, c->len);
    atomic_store_explicit(&c->seq, pos + LOG_QUEUE_SLOTS, memory_order_release);
    atomic_store_explicit(&g_q_tail, pos + 1, memory_order_relaxed);
    n++;
  }
  dropped = atomic_exchange_explicit(&g_q_dropped, 0, memory_order_relaxed);
  if (dropped) {
    char line[128];
    size_t len = prefix(line);
    len += (size_t)snprintf(line + len, sizeof line - len, "[LW500252] log: %lu lines dropped, writer behind\n", dropped);
    write_all(line, len);
  }
  return n;
}

static void *q_writer(void *arg) {
  (void)arg;
  while (!atomic_load_explicit(&g_q_stop, memory_order_acquire)) {
    if (q_drain()) continue;
    pthread_mutex_lock(&g_q_mtx);
    atomic_store_explicit(&g_q_sleeping, 1, memory_order_seq_cst);
    if (!q_drain()) {
      struct timespec ts;
      clock_gettime(CLOCK_REALTIME, &ts);
      ts.tv_nsec += 50000000;
      if (ts.tv_nsec >= 1000000000) {
        ts.tv_sec++;
        ts.tv_nsec -= 1000000000;
      }
      pthread_cond_timedwait(&g_q_cv, &g_q_mtx, &ts);
    }
    atomic_store_explicit(&g_q_sleeping, 0, memory_order_relaxed);
    pthread_mutex_unlock(&g_q_mtx);
  }
  q_drain();
  return NULL;
}

static void q_child_sync(void) { atomic_store_explicit(&g_q_on, 0, memory_order_relaxed); }

void log_async_stop(void) {
  if (!atomic_exchange_explicit(&g_q_on, 0, memory_order_acq_rel)) return;
  atomic_store_explicit(&g_q_stop, 1, memory_order_release);
  pthread_cond_signal(&g_q_cv);
  pthread_join(g_q_thread, NULL);
}

static void log_async_once(void) {
  pthread_atfork(NULL, NULL, q_child_sync);
  atexit(log_async_stop);
}

void log_async_start(void) {
  static pthread_once_t once = PTHREAD_ONCE_INIT;
  int err;
  if (atomic_load_explicit(&g_q_on, memory_order_acquire)) return;
  if (!g_q) {
    g_q = malloc(sizeof *g_q * LOG_QUEUE_SLOTS);
    if (!g_q) return;
    for (size_t i = 0; i < LOG_QUEUE_SLOTS; i++) atomic_init(&g_q[i].seq, i);
  }
  atomic_store_explicit(&g_q_stop, 0, memory_order_relaxed);
  err = pthread_create(&g_q_thread, NULL, q_writer, NULL);
  if (err) {
    log_code("LA500102", "pthread_create: %s", strerror(err));
    return;
  }
  pthread_once(&once, log_async_once);
  atomic_store_explicit(&g_q_on, 1, memory_order_release);
}

int log_stderr_is_tty(void) {
  static int tty = -1;
  if (tty < 0) tty = isatty(STDERR_FILENO);
  return tty;
}

/* color only if tty and not NO_COLOR / TERM=dumb */
static log_color_t color_mode = LOG_COLOR_AUTO;

void log_set_color(log_color_t mode) { color_mode = mode; }

log_color_t log_color_prescan(int argc, char **argv) {
  for (int i = 1; i < argc; i++) {
    const char *v = NULL;
    if (!strcmp(argv[i], "--color") && i + 1 < argc)    v = argv[i + 1];
    else if (!strncmp(argv[i], "--color=", 8)) v = argv[i] + 8;
    if (!v) continue;
    if (!strcmp(v, "always")) return LOG_COLOR_ALWAYS;
    if (!strcmp(v, "never")) return LOG_COLOR_NEVER;
  }
  return LOG_COLOR_AUTO;
}

/* let's have nice things */
int log_colors_enabled(void) {
  static int on = -1;
  if (color_mode == LOG_COLOR_ALWAYS) return 1;
  if (color_mode == LOG_COLOR_NEVER) return 0;
  if (on < 0) {
    const char *term = getenv("TERM");
    on = log_stderr_is_tty() && !getenv("NO_COLOR") && !(term && strcmp(term, "dumb") == 0);
  }
  return on;
}

/* skips a CSI escape sequence (ESC '[' params... final byte), *rp already past "ESC[" */
static void skip_csi_params(char **rp) {
  char *r = *rp;
  while (*r && (*r < 0x40 || *r > 0x7E)) r++; /* params */
  if (*r) r++; /* final */
  *rp = r;
}

/* strip ANSI escapes in place: CSI and two-byte */
static void strip_ansi(char *s) {
  char *r = s;
  char *w = s;
  while (*r) {
    if (*r == 0x1B) {
      r++;
      if (*r == '[') {
        r++;
        skip_csi_params(&r);
      } else if (*r) {
        r++;
      }
      continue;
    }
    *w++ = *r++;
  }
  *w = '\0';
}

static void emit(const char *code, const char *msg) {
  char line[LOG_LINE_MAX];
  size_t len = prefix(line);
  size_t n;
  if (code) {
    n = strlen(code);
    if (n > 16) n = 16;
    line[len++] = '[';
    memcpy(line + len, code, n);
    len += n;
    line[len++] = ']';
    line[len++] = ' ';
  }
  n = strlen(msg);
  if (n > sizeof line - len - 1) n = sizeof line - len - 1;
  memcpy(line + len, msg, n);
  len += n;
  line[len++] = '\n';
  if (atomic_load_explicit(&g_q_on, memory_order_acquire)) {
    if (q_push(line, len)) atomic_fetch_add_explicit(&g_q_dropped, 1, memory_order_relaxed);
    return;
  }
  write_all(line, len);
}

void log_line(const char *fmt, ...) {
  char msg[4096];
  va_list ap;
  va_start(ap, fmt);
  vsnprintf(msg, sizeof msg, fmt, ap);
  va_end(ap);
  emit(NULL, msg);
}

void log_code(const char *code, const char *fmt, ...) {
  char msg[4096];
  va_list ap;
  va_start(ap, fmt);
  vsnprintf(msg, sizeof msg, fmt, ap);
  va_end(ap);
  emit(code, msg);
}

void log_throttled(log_throttle_t *t, int window_s, const char *code, const char *fmt, ...) {
  long now = (long)(now_ms() / 1000);
  long next;
  long new_next;
  va_list ap;
  char msg[512];
  int n;
  unsigned long suppressed;
  next = atomic_load_explicit(&t->next_log_s, memory_order_relaxed);
  if (now < next) {
    atomic_fetch_add_explicit(&t->suppressed, 1, memory_order_relaxed);
    return;
  }
  new_next = now + window_s;
  if (!atomic_compare_exchange_strong_explicit(&t->next_log_s, &next, new_next, memory_order_relaxed, memory_order_relaxed)) {
    atomic_fetch_add_explicit(&t->suppressed, 1, memory_order_relaxed);
    return;
  }
  va_start(ap, fmt);
  n = vsnprintf(msg, sizeof msg, fmt, ap);
  va_end(ap);
  suppressed = atomic_exchange_explicit(&t->suppressed, 0, memory_order_relaxed);
  if (suppressed && n >= 0 && (size_t)n < sizeof msg) snprintf(msg + n, sizeof msg - (size_t)n, " (%lu more in %ds)", suppressed, window_s);
  emit(code, msg);
}

static void vemit_ansi(const char *code, const char *fmt, va_list ap) __attribute__((format(printf, 2, 0)));

static void vemit_ansi(const char *code, const char *fmt, va_list ap) {
  char msg[4096];
  vsnprintf(msg, sizeof msg, fmt, ap);
  if (!log_colors_enabled()) strip_ansi(msg);
  emit(code, msg);
}

void log_line_ansi(const char *fmt, ...) {
  va_list ap;
  va_start(ap, fmt);
  vemit_ansi(NULL, fmt, ap);
  va_end(ap);
}

void log_code_ansi(const char *code, const char *fmt, ...) {
  va_list ap;
  va_start(ap, fmt);
  vemit_ansi(code, fmt, ap);
  va_end(ap);
}
