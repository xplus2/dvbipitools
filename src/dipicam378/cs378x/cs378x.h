/* Copyright 2026 dvbipitools authors. Licensed under GPL-3.0-or-later.
 * See NOTICE and LICENSE for details and authorship information. */

#ifndef DIPICAM378_CS378X_H
#define DIPICAM378_CS378X_H

#include <stddef.h>
#include <stdint.h>

typedef struct cs378x_server cs378x_server_t;

typedef struct {
  unsigned port;
  const char *password;  /* must match reader's "password ="; MD5 is AES-128 key */
  const char *username;  /* must match reader's "user ="; empty/NULL = no check, accept any */
  int verbose;
} cs378x_cfg_t;

/* append-only: index also selects auth_errors_total[] slot, metrics wire label */
typedef enum { CAM_AUTH_USER = 0, CAM_AUTH_CONNID, CAM_AUTH_CHECKSUM, CAM_AUTH_OVERSIZED, CAM_AUTH_REASON_COUNT } cam_auth_reason_t;

const char *cs378x_auth_reason_name(cam_auth_reason_t r);

typedef struct {
  unsigned connections_active;
  unsigned long long connections_total;
  unsigned long long auth_errors_total[CAM_AUTH_REASON_COUNT];
  unsigned long long ecm_total;
  unsigned long long ecm_errors_total;
  unsigned long long emm_total;
} cs378x_metrics_t;

void cs378x_server_get_metrics(cs378x_server_t *s, cs378x_metrics_t *out);

/* CW for one ECM. 0 ok. -1 transient, no reply (OSCcam retries, e.g. no EMM yet).
   -2 permanent, CAID unsupported (CMD08 "shut up") */
typedef int (*cs378x_ecm_cb)(const unsigned char *ecm, size_t ecm_len, unsigned srvid, unsigned caid, unsigned prid, unsigned char cw_out[16], void *user);

/* one incoming EMM, no reply ever sent */
typedef void (*cs378x_emm_cb)(const unsigned char *emm, size_t emm_len, unsigned caid, unsigned provid, void *user);

cs378x_server_t *cs378x_server_start(const cs378x_cfg_t *cfg, cs378x_ecm_cb ecm_cb, cs378x_emm_cb emm_cb, void *user);
void cs378x_server_stop(cs378x_server_t *s);

/* pure wire-format/crypto helpers, exposed for unit tests with synthetic buffers */
void cs378x_crc32_init_table(void);
uint32_t cs378x_crc32(const unsigned char *buf, size_t len);
int cs378x_md5(const unsigned char *data, size_t len, unsigned char out[16]);
int cs378x_aes128_ecb(const unsigned char key[16], unsigned char *buf, size_t len, int encrypt);
size_t cs378x_frame_boundary(size_t n);

#endif
