/* Copyright 2026 dvbipitools authors. Licensed under GPL-3.0-or-later.
 * See NOTICE and LICENSE for details and authorship information. */

#include <stdint.h>
#include <stdio.h>
#include <string.h>

#include "lib/helper/log.h"

#include "cw_encryption.h"

void cwenc_des56_expand(const unsigned char in7[7], unsigned char out8[CWENC_DES_KEY_LEN]) {
  uint64_t bits = 0;
  for (int i = 0; i < 7; i++) bits = (bits << 8) | in7[i];
  for (int g = 0; g < 8; g++) {
    int shift = 56 - 7 * (g + 1);
    unsigned char byte = (unsigned char)(((bits >> shift) & 0x7F) << 1);
    int ones = 0;
    for (int b = 0; b < 8; b++) if (byte & (1 << b)) ones++;
    out8[g] = (ones % 2 == 0) ? (byte | 1) : byte;
  }
}

static int cwenc_hex_nibble(char c) {
  if (c >= '0' && c <= '9') return c - '0';
  if (c >= 'a' && c <= 'f') return c - 'a' + 10;
  if (c >= 'A' && c <= 'F') return c - 'A' + 10;
  return -1;
}

static int cwenc_hex_decode(const char *hex, unsigned char *out, size_t out_len) {
  if (strlen(hex) != out_len * 2) return -1;
  for (size_t i = 0; i < out_len; i++) {
    int hi = cwenc_hex_nibble(hex[2 * i]);
    int lo = cwenc_hex_nibble(hex[2 * i + 1]);
    if (hi < 0 || lo < 0) return -1;
    out[i] = (unsigned char)((hi << 4) | lo);
  }
  return 0;
}

static int cwenc_load_key_list_file(const char *path, unsigned char *out) {
  FILE *f = fopen(path, "rb");
  size_t n;
  int extra;
  if (!f) {
    log_line("cw_encryption: cannot open key list '%s'", path);
    return -1;
  }
  n = fread(out, 1, CWENC_KEY_LIST_LEN, f);
  extra = fgetc(f);
  fclose(f);
  if (n != CWENC_KEY_LIST_LEN || extra != EOF) {
    log_line("cw_encryption: key list '%s' must be exactly %d bytes", path, CWENC_KEY_LIST_LEN);
    return -1;
  }
  return 0;
}

int cwenc_config_init(cwenc_config_t *cfg, const char *algorithm, const char *aes_mode, const char *fixed_key_hex, const char *key_list_a_path, const char *key_list_b_path) {
  size_t want_len;
  static const struct { const char *name; cwenc_algo_t algo; size_t want_len; } algo_map[] = {
    {"des56", CWENC_ALGO_DES56, 7}, {"aes128", CWENC_ALGO_AES128, 16}, {"aes256", CWENC_ALGO_AES256, 32},
  };
  size_t i;

  memset(cfg, 0, sizeof *cfg);
  if (!algorithm || !algorithm[0]) {
    cfg->algo = CWENC_ALGO_OFF;
    return 0;
  }

  for (i = 0; i < sizeof algo_map / sizeof algo_map[0]; i++) if (!strcmp(algorithm, algo_map[i].name)) break;
  if (i == sizeof algo_map / sizeof algo_map[0]) {
    log_line("cw_encryption: invalid algorithm '%s' (des56|aes128|aes256)", algorithm);
    return -1;
  }
  cfg->algo = algo_map[i].algo;
  want_len = algo_map[i].want_len;

  if (!aes_mode || !aes_mode[0]) {
    cfg->aes_mode = CWENC_AES_MODE_STREAM;
  } else {
    static const struct { const char *name; cwenc_aes_mode_t mode; } mode_map[] = {
      {"stream", CWENC_AES_MODE_STREAM}, {"ecb", CWENC_AES_MODE_ECB},
    };
    for (i = 0; i < sizeof mode_map / sizeof mode_map[0]; i++) if (!strcmp(aes_mode, mode_map[i].name)) break;
    if (i == sizeof mode_map / sizeof mode_map[0]) {
      log_line("cw_encryption: invalid aes_mode '%s' (stream|ecb)", aes_mode);
      return -1;
    }
    cfg->aes_mode = mode_map[i].mode;
  }

  if (fixed_key_hex && fixed_key_hex[0]) {
    if (cwenc_hex_decode(fixed_key_hex, cfg->fixed_key, want_len) != 0) {
      log_line("cw_encryption: fixed_key must be %zu hex chars for %s", want_len * 2, algorithm);
      return -1;
    }
    cfg->fixed_key_len = want_len;
  } else if (cfg->algo == CWENC_ALGO_DES56) {
    static const unsigned char annex_d_fixed_key[7] = {0x4D, 0xA1, 0x9F, 0xF0, 0xAF, 0x6B, 0x8F};
    memcpy(cfg->fixed_key, annex_d_fixed_key, 7);
    cfg->fixed_key_len = 7;
  }

  if (key_list_a_path && key_list_a_path[0]) {
    if (cwenc_load_key_list_file(key_list_a_path, cfg->key_list_a) != 0) return -1;
    cfg->key_list_a_loaded = 1;
  }
  if (key_list_b_path && key_list_b_path[0]) {
    if (cwenc_load_key_list_file(key_list_b_path, cfg->key_list_b) != 0) return -1;
    cfg->key_list_b_loaded = 1;
  }
  return 0;
}

void cwenc_ctx_init(cwenc_ctx_t *ctx, const cwenc_config_t *cfg) {
  ctx->cfg = *cfg;
  ctx->next_ptr = 0;
  ctx->next_list_sel = 0;
}

int cwenc_select_next(cwenc_ctx_t *ctx, cwenc_selection_t *out) {
  const cwenc_config_t *cfg = &ctx->cfg;
  switch (cfg->algo) {
    case CWENC_ALGO_OFF: return -1;
    case CWENC_ALGO_DES56: out->algorithm_type = 0; break;
    case CWENC_ALGO_AES128: out->algorithm_type = 1; break;
    case CWENC_ALGO_AES256: out->algorithm_type = 2; break;
  }
  if (cfg->key_list_a_loaded || cfg->key_list_b_loaded) {
    int use_b = cfg->key_list_b_loaded && (ctx->next_list_sel || !cfg->key_list_a_loaded);
    const unsigned char *list = use_b ? cfg->key_list_b : cfg->key_list_a;
    out->fixed_key_mode = 1;
    out->key_list_sel = use_b ? 1 : 0;
    out->cw_key_ptr = (int)ctx->next_ptr;
    memcpy(out->key_material, list + ctx->next_ptr, 7);
    out->key_material_len = 7;

    ctx->next_ptr += CWENC_PTR_STEP;
    if (ctx->next_ptr > CWENC_PTR_MAX) {
      ctx->next_ptr = 0;
      if (cfg->key_list_a_loaded && cfg->key_list_b_loaded)
        ctx->next_list_sel = !ctx->next_list_sel;
    }
    return 0;
  }
  if (cfg->fixed_key_len == 0) return -1;
  out->fixed_key_mode = 0;
  out->key_list_sel = 0;
  out->cw_key_ptr = 0;
  memcpy(out->key_material, cfg->fixed_key, cfg->fixed_key_len);
  out->key_material_len = cfg->fixed_key_len;
  return 0;
}

unsigned short cwenc_pack_param(const cwenc_selection_t *sel) {
  unsigned short v = 0;
  v |= (unsigned short)((sel->key_list_sel & 1) << 15);
  v |= (unsigned short)((sel->fixed_key_mode & 1) << 14);
  v |= (unsigned short)((sel->algorithm_type & 0x7) << 11);
  v |= (unsigned short)(sel->cw_key_ptr & 0x7FF);
  return v;
}
