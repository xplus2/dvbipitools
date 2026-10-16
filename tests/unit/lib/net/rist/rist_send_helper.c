/* Copyright 2026 dvbipitools authors. Licensed under GPL-3.0-or-later.
 * See NOTICE and LICENSE for details and authorship information. */

/* separate process from test_ristin.c's receiver:
   avoids two rist_ctx's in one process (librist evsocket_create race).
   usage: rist_send_helper <rist://host:port> <payload> */

#include <librist/librist.h>
#include <stdio.h>
#include <string.h>
#include <time.h>

int main(int argc, char **argv) {
  struct rist_ctx *ctx;
  struct rist_peer_config *pc = NULL;
  struct rist_peer *peer;
  struct rist_data_block db;
  int tries;
  int sent = 0;

  if (argc != 3) {
    fprintf(stderr, "usage: %s <rist://host:port> <payload>\n", argv[0]);
    return 2;
  }

  if (rist_sender_create(&ctx, RIST_PROFILE_SIMPLE, 0, NULL) != 0) {
    fprintf(stderr, "rist_sender_create failed\n");
    return 1;
  }
  if (rist_parse_address2(argv[1], &pc) != 0 || !pc) {
    fprintf(stderr, "rist_parse_address2 failed: %s\n", argv[1]);
    return 1;
  }
  pc->initiate_conn = 1;
  if (rist_peer_create(ctx, &peer, pc) != 0) {
    fprintf(stderr, "rist_peer_create failed\n");
    rist_peer_config_free2(&pc);
    return 1;
  }
  rist_peer_config_free2(&pc);
  if (rist_start(ctx) != 0) {
    fprintf(stderr, "rist_start failed\n");
    return 1;
  }

  memset(&db, 0, sizeof db);
  db.payload = argv[2];
  db.payload_len = strlen(argv[2]);
  /* receiver drops data until RTCP handshake completes: resend through the window */
  for (tries = 0; tries < 40; tries++) {
    struct timespec resend_wait = {0, 50000000L};

    if (rist_sender_data_write(ctx, &db) >= 0)
      sent++;
    nanosleep(&resend_wait, NULL);
  }
  if (sent == 0) {
    fprintf(stderr, "rist_sender_data_write never succeeded\n");
    rist_destroy(ctx);
    return 1;
  }

  rist_destroy(ctx);
  return 0;
}
