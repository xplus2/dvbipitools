/* Copyright 2026 dvbipitools authors. Licensed under GPL-3.0-or-later.
 * See NOTICE and LICENSE for details and authorship information. */

#define _GNU_SOURCE

#include <fcntl.h>
#include <unistd.h>

#include "pipereader.h"

#define PIPE_SIZE_BIG (4 * 1024 * 1024)
#define PIPE_SIZE_MID (1024 * 1024)

/* default 64 KiB is ~26 ms at 20 Mbit/s: bursts must survive a stalled consumer */
static void pipe_grow(int fd) {
#ifdef F_SETPIPE_SZ
  if (fcntl(fd, F_SETPIPE_SZ, PIPE_SIZE_BIG) < 0) fcntl(fd, F_SETPIPE_SZ, PIPE_SIZE_MID);
#else
  (void)fd;
#endif
}

int pipereader_start(pipereader_t *p, void *(*thread_fn)(void *), void *arg) {
  if (pipe(p->pfd) < 0) return -1;
  pipe_grow(p->pfd[1]);
  if (fcntl(p->pfd[1], F_SETFL, O_NONBLOCK) < 0) {
    close(p->pfd[0]);
    close(p->pfd[1]);
    return -1;
  }
  atomic_init(&p->stop, 0);
  if (pthread_create(&p->thread, NULL, thread_fn, arg) != 0) {
    close(p->pfd[0]);
    close(p->pfd[1]);
    return -1;
  }
  return 0;
}

int pipereader_fd(const pipereader_t *p) { return p->pfd[0]; }

void pipereader_stop(pipereader_t *p) {
  atomic_store_explicit(&p->stop, 1, memory_order_relaxed);
  pthread_join(p->thread, NULL);
  close(p->pfd[0]); /* write end already closed by thread_fn on exit */
}
