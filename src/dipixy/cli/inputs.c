/* Copyright 2026 dvbipitools authors. Licensed under GPL-3.0-or-later.
 * See NOTICE and LICENSE for details and authorship information. */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>

#include "lib/helper/argutil.h"
#include "lib/sys/ioutil.h"

#include "priv.h"
#include "../core/route.h"

/* case-insensitive suffix match against known playlist extensions */
static int playlist_kind_from_ext(const char *path, source_kind_t *out) {
  static const struct { const char *ext; source_kind_t kind; } map[] = {
    {".m3u", SRC_M3U}, {".m3u8", SRC_M3U}, {".xspf", SRC_XSPF}, {".csv", SRC_CSV}, {".xml", SRC_XML},
  };
  const char *dot = strrchr(path, '.');
  if (!dot) return -1;
  for (size_t i = 0; i < sizeof map / sizeof map[0]; i++) {
    if (!strcasecmp(dot, map[i].ext)) {
      *out = map[i].kind;
      return 0;
    }
  }
  return -1;
}

static int sources_append(config_t *cfg, source_kind_t kind, const char *value, int ordinal) {
  source_def_t *p = array_grow(cfg->sources, &cfg->sources_cap, cfg->n_sources + 1, sizeof *cfg->sources);
  if (!p) return -1;
  cfg->sources = p;
  memset(&cfg->sources[cfg->n_sources], 0, sizeof *cfg->sources);
  cfg->sources[cfg->n_sources].kind = kind;
  cfg->sources[cfg->n_sources].value = value;
  cfg->sources[cfg->n_sources].ordinal = ordinal;
  cfg->n_sources++;
  return 0;
}

static int name_in_use(const config_t *cfg, const char *name) {
  if (cfg->stdin_name && !strcmp(cfg->stdin_name, name)) return 1;
  if (cfg->rist_name && !strcmp(cfg->rist_name, name))   return 1;
  for (int i = 0; i < cfg->n_sources; i++) {
    if (cfg->sources[i].name && !strcmp(cfg->sources[i].name, name)) return 1;
  }
  return 0;
}

void args_free(config_t *cfg) {
  free(cfg->sources);
  cfg->sources = NULL;
  cfg->n_sources = 0;
  cfg->sources_cap = 0;
}

void dixy_cfg_reset_inputs(config_t *cfg) {
  args_free(cfg);
  cfg->stdin_path = NULL;
  cfg->stdin_name = NULL;
  cfg->stdin_ordinal = 0;
  cfg->stdin_media_type = MEDIA_TV;
  cfg->rist_uri = NULL;
  cfg->rist_name = NULL;
  cfg->rist_ordinal = 0;
  cfg->rist_media_type = MEDIA_TV;
  cfg->input_ordinal = 0;
  cfg->last_input = LAST_NONE;
  cfg->media_type_seen = 0;
}

int dixy_cfg_add_input(config_t *cfg, const char *val, char *err, size_t errsz) {
  source_kind_t kind;
  int ordinal = cfg->input_ordinal + 1;
  if (strcmp(val, "-") == 0) {
    cfg->stdin_path = val;
    cfg->stdin_ordinal = ordinal;
    cfg->last_input = LAST_STDIN;
  } else if (strncmp(val, "rist://", 7) == 0) {
    if (cfg->rist_uri) {
      bufcpy(err, errsz, "at most one rist:// input");
      return -1;
    }
    if (val[7] != '@') {
      snprintf(err, errsz, "invalid '%s' (rist:// needs rist://@host:port)", val);
      return -1;
    }
    cfg->rist_uri = val;
    cfg->rist_ordinal = ordinal;
    cfg->last_input = LAST_RIST;
  } else {
    const char *value = val;
    if (strncmp(val, "sds://", 6) == 0) {
      int family;
      char addr[64];
      unsigned port;
      value = val + 6;
      if (argutil_addrport_parse(value, &family, addr, sizeof addr, &port)) {
        snprintf(err, errsz, "invalid '%s' (sds:// needs sds://addr:port)", val);
        return -1;
      }
      kind = SRC_SDS;
    } else if (strncmp(val, "http://", 7) == 0 || strncmp(val, "https://", 8) == 0) {
      kind = SRC_HTTP;
    } else if (strncmp(val, "rtp://", 6) == 0 || strncmp(val, "udp://", 6) == 0) {
      const char *scheme = val[0] == 'r' ? "rtp" : "udp";
      int family;
      int rtp;
      char addr[64];
      unsigned port;
      if (route_resolve_channel_uri(val, &family, addr, sizeof addr, NULL, 0, &port, &rtp)) {
        snprintf(err, errsz, "invalid '%s' (%s:// needs %s://[src@]addr:port, multicast)", val, scheme, scheme);
        return -1;
      }
      kind = SRC_MCAST;
    } else if (playlist_kind_from_ext(val, &kind)) {
      snprintf(err, errsz, "unidentified '%s' (expected -, sds://, rist://, rtp://, udp://, http(s)://, or a .m3u/.xspf/.csv/.xml path)", val);
      return -1;
    }
    if (sources_append(cfg, kind, value, ordinal)) {
      bufcpy(err, errsz, "out of memory");
      return -1;
    }
    cfg->last_input = LAST_SOURCE;
  }
  cfg->input_ordinal = ordinal;
  cfg->media_type_seen = 0;
  return 0;
}

int dixy_cfg_set_name(config_t *cfg, const char *name, char *err, size_t errsz) {
  const char **slot;
  if (!route_name_valid(name)) {
    snprintf(err, errsz, "invalid name '%s' (no '/', not starting with '.', not a reserved word, max %d chars)", name, ROUTE_NAME_MAX);
    return -1;
  }
  if (name_in_use(cfg, name)) {
    snprintf(err, errsz, "duplicate name '%s'", name);
    return -1;
  }
  switch (cfg->last_input) {
    case LAST_STDIN:
      slot = &cfg->stdin_name;
      break;
    case LAST_RIST:
      slot = &cfg->rist_name;
      break;
    case LAST_SOURCE:
      slot = &cfg->sources[cfg->n_sources - 1].name;
      break;
    default:
      bufcpy(err, errsz, "name must directly follow the input it names");
      return -1;
  }
  if (*slot) {
    bufcpy(err, errsz, "name given twice for the same input");
    return -1;
  }
  *slot = name;
  return 0;
}
