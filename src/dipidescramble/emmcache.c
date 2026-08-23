/* Copyright 2026 dvbipitools authors. Licensed under GPL-3.0-or-later.
 * See NOTICE and LICENSE for details and authorship information. */

#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "lib/cas/device_state_core.h"
#include "lib/demux/psi/section_asm.h"
#include "lib/demux/tspack.h"
#include "lib/log.h"

#include "crypto.h"
#include "emmcache.h"
#include "version.h"

#define EMMCACHE_MAX_SERVICES_CEILING DEVICE_MAX_SERVICES_CEILING /* array size, matches device_core_t exactly */
#define EMMCACHE_MAX_FILE (1 << 20) /* generous cap, real cache is a few KB */
#define EMM_G_PAYLOAD_LEN (4 + CRYPTO_EMM_G_LEN) /* mirrors device.c's own classification */

typedef struct {
  unsigned char raw[PSI_SECTION_ASM_BUF_LEN];
  size_t len;
} raw_sec_t;

typedef struct {
  unsigned service_id;
  raw_sec_t sec;
} emm_g_slot_t;

struct emmcache {
  raw_sec_t emm_u;
  int have_emm_u;
  emm_g_slot_t services[EMMCACHE_MAX_SERVICES_CEILING];
  size_t service_count;
  size_t max_services;
  unsigned long long dropped_total;
};

emmcache_t *emmcache_new(size_t max_services) {
  struct emmcache *c = calloc(1, sizeof(struct emmcache));
  if (!c)
    return NULL;
  c->max_services = max_services ? max_services : 32;
  if (c->max_services > EMMCACHE_MAX_SERVICES_CEILING)
    c->max_services = EMMCACHE_MAX_SERVICES_CEILING;
  return c;
}

void emmcache_free(emmcache_t *c) { free(c); }

static emm_g_slot_t *slot_for(struct emmcache *c, unsigned service_id) {
  for (size_t i = 0; i < c->service_count; i++)
    if (c->services[i].service_id == service_id)
      return &c->services[i];
  if (c->service_count >= c->max_services)
    return NULL;
  c->services[c->service_count].service_id = service_id;
  return &c->services[c->service_count++];
}

int emmcache_feed(emmcache_t *c, device_state_t *d, const unsigned char *emm, size_t emm_len) {
  size_t payload_len;

  if (emm_len < 3 || emm_len > PSI_SECTION_ASM_BUF_LEN)
    return 0;
  if (!device_on_emm(d, emm, emm_len))
    return 0;

  payload_len = emm_len - 3;
  if (payload_len == EMM_G_PAYLOAD_LEN) {
    unsigned service_id = ((unsigned)emm[3] << 8) | emm[4];
    emm_g_slot_t *sl = slot_for(c, service_id);
    if (!sl) {
      log_line(TOOL_NAME ": emm cache full (%zu services), dropping EMM-G for service_id 0x%04x", c->max_services, service_id);
      c->dropped_total++;
      return 0;
    }
    if (sl->sec.len == emm_len && memcmp(sl->sec.raw, emm, emm_len) == 0)
      return 0; /* unchanged repeat */
    memcpy(sl->sec.raw, emm, emm_len);
    sl->sec.len = emm_len;
    return 1;
  }

  if (c->have_emm_u && c->emm_u.len == emm_len && memcmp(c->emm_u.raw, emm, emm_len) == 0)
    return 0; /* unchanged repeat */
  memcpy(c->emm_u.raw, emm, emm_len);
  c->emm_u.len = emm_len;
  c->have_emm_u = 1;
  return 1;
}

int emmcache_load(emmcache_t *c, device_state_t *d, const char *path) {
  FILE *f = fopen(path, "rb");
  unsigned char *buf;
  size_t len = 0, cap = 65536, off;
  int rc = 0;

  if (!f)
    return 0; /* absent: first run, not an error */

  buf = malloc(cap);
  if (!buf) {
    fclose(f);
    return -1;
  }
  for (;;) {
    size_t n;
    if (len == cap) {
      size_t ncap = cap * 2;
      unsigned char *nb;
      if (ncap > EMMCACHE_MAX_FILE) {
        log_line(TOOL_NAME ": emm cache file too large, ignoring: %s", path);
        rc = -1;
        break;
      }
      nb = realloc(buf, ncap);
      if (!nb) {
        rc = -1;
        break;
      }
      buf = nb;
      cap = ncap;
    }
    n = fread(buf + len, 1, cap - len, f);
    if (n == 0)
      break;
    len += n;
  }
  fclose(f);
  if (rc != 0) {
    free(buf);
    return -1;
  }

  off = 0;
  while (off + 3 <= len) {
    size_t total = tspack_length12(buf + off + 1) + 3;
    if (off + total > len)
      break;
    emmcache_feed(c, d, buf + off, total);
    off += total;
  }
  free(buf);
  return 0;
}

int emmcache_save(const emmcache_t *c, const char *path) {
  unsigned char *buf = malloc((EMMCACHE_MAX_SERVICES_CEILING + 1) * PSI_SECTION_ASM_BUF_LEN);
  size_t off = 0;
  FILE *f;
  int rc = 0;

  if (!buf)
    return -1;

  if (c->have_emm_u) {
    memcpy(buf + off, c->emm_u.raw, c->emm_u.len);
    off += c->emm_u.len;
  }
  for (size_t i = 0; i < c->service_count; i++) {
    memcpy(buf + off, c->services[i].sec.raw, c->services[i].sec.len);
    off += c->services[i].sec.len;
  }

  f = fopen(path, "wb");
  if (!f) {
    log_line(TOOL_NAME ": cannot write emm cache %s: %s", path, strerror(errno));
    free(buf);
    return -1;
  }
  if (off && fwrite(buf, 1, off, f) != off) {
    log_line(TOOL_NAME ": short write to emm cache %s", path);
    rc = -1;
  }
  fclose(f);
  free(buf);
  return rc;
}

unsigned long long emmcache_dropped_total(const emmcache_t *c) {
  return c->dropped_total;
}
