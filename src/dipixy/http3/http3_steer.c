/* Copyright 2026 dvbipitools authors. Licensed under GPL-3.0-or-later.
 * See NOTICE and LICENSE for details and authorship information. */

#ifdef HAVE_HTTP3

#include "http3_steer.h"

#include <linux/filter.h>
#include <pthread.h>
#include <sys/socket.h>

#ifndef SO_ATTACH_REUSEPORT_CBPF
#define SO_ATTACH_REUSEPORT_CBPF 51
#endif

static pthread_mutex_t g_bind_lock = PTHREAD_MUTEX_INITIALIZER;
static unsigned g_next_tag;
static _Thread_local uint16_t t_tag;

void h3_steer_begin(void) {
  pthread_mutex_lock(&g_bind_lock);
}

int h3_steer_end(int fd4, int fd6) {
  int rc = 0;
  if (fd4 >= 0 || fd6 >= 0) t_tag = (uint16_t)g_next_tag++;
  if (fd4 >= 0 && h3_steer_attach(fd4) != 0) rc = -1;
  if (fd6 >= 0 && h3_steer_attach(fd6) != 0) rc = -1;
  pthread_mutex_unlock(&g_bind_lock);
  return rc;
}

/* payload start at UDP data.
   long header: DCID at 6, short: at 1. oor index fallback to hash */
int h3_steer_attach(int fd) {
  struct sock_filter prog[] = {
      BPF_STMT(BPF_LD | BPF_B | BPF_ABS, 0),
      BPF_STMT(BPF_ALU | BPF_AND | BPF_K, 0x80),
      BPF_JUMP(BPF_JMP | BPF_JEQ | BPF_K, 0, 2, 0),
      BPF_STMT(BPF_LD | BPF_H | BPF_ABS, 6),
      BPF_STMT(BPF_RET | BPF_A, 0),
      BPF_STMT(BPF_LD | BPF_H | BPF_ABS, 1),
      BPF_STMT(BPF_RET | BPF_A, 0),
  };
  struct sock_fprog fprog = {.len = sizeof prog / sizeof prog[0], .filter = prog};
  return setsockopt(fd, SOL_SOCKET, SO_ATTACH_REUSEPORT_CBPF, &fprog, sizeof fprog);
}

void h3_steer_tag_cid(uint8_t *cid) {
  cid[0] = (uint8_t)(t_tag >> 8);
  cid[1] = (uint8_t)(t_tag & 0xff);
}

#endif /* HAVE_HTTP3 */
