/* Copyright 2026 dvbipitools authors. Licensed under GPL-3.0-or-later.
 * See NOTICE and LICENSE for details and authorship information. */

#ifndef LIB_HELPER_PIPEREADER_H
#define LIB_HELPER_PIPEREADER_H

#include <pthread.h>
#include <stdatomic.h>

typedef struct {
  int pfd[2];
  pthread_t thread;
  atomic_int stop;
} pipereader_t;

/* 0 ok, -1 fail (pipe/fcntl/thread create), *p unchanged on fail */
int pipereader_start(pipereader_t *p, void *(*thread_fn)(void *), void *arg);

int pipereader_fd(const pipereader_t *p);

/* sets stop, joins thread, closes read end */
void pipereader_stop(pipereader_t *p);

#endif
