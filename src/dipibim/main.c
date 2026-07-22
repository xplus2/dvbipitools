/* Copyright 2026 dvbipitools authors. Licensed under GPL-3.0-or-later.
 * See NOTICE and LICENSE for details and authorship information. */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "args.h"
#include "lib/bim/accessunit.h"
#include "lib/bim/bitreader.h"
#include "lib/bim/bitwriter.h"
#include "lib/bim/strrepo.h"
#include "lib/ioutil.h"
#include "lib/log.h"
#include "lib/tva/epg_doc.h"
#include "lib/tva/tva_xml.h"
#include "version.h"

static log_color_t color_prescan(int argc, char **argv) {
  int i;
  for (i = 1; i < argc; i++) {
    const char *v = NULL;
    if (!strcmp(argv[i], "--color") && i + 1 < argc)
      v = argv[i + 1];
    else if (!strncmp(argv[i], "--color=", 8))
      v = argv[i] + 8;
    if (!v)
      continue;
    if (!strcmp(v, "always"))
      return LOG_COLOR_ALWAYS;
    if (!strcmp(v, "never"))
      return LOG_COLOR_NEVER;
  }
  return LOG_COLOR_AUTO;
}

static FILE *open_input(const char *path) { return strcmp(path, "-") ? fopen(path, "r") : stdin; }
static FILE *open_output(const char *path) { return strcmp(path, "-") ? fopen(path, "w") : stdout; }

static int encode_xml_to_bim(FILE *in, FILE *out, int verbose) {
  epg_doc_t doc;
  bitwriter_t bw;
  strrepo_writer_t sw;
  int rc = 0, nfuu = 0;

  epg_doc_init(&doc);
  bitwriter_init(&bw);
  strrepo_writer_init(&sw);

  if (tva_xml_read(in, &doc)) {
    rc = -1;
  } else if (accessunit_encode(&doc, &bw, &sw, &nfuu)) {
    rc = -1;
  } else {
    size_t bits_len, strs_len;
    const unsigned char *bits = bitwriter_data(&bw, &bits_len);
    const unsigned char *strs = strrepo_writer_data(&sw, &strs_len);
    unsigned char lenbuf[4];
    lenbuf[0] = (unsigned char)(bits_len >> 24);
    lenbuf[1] = (unsigned char)(bits_len >> 16);
    lenbuf[2] = (unsigned char)(bits_len >> 8);
    lenbuf[3] = (unsigned char)bits_len;
    fwrite(lenbuf, 1, 4, out);
    fwrite(bits, 1, bits_len, out);
    fwrite(strs, 1, strs_len, out);
    if (verbose)
      log_line("%d channels, %d programmes, %d fragments -> %zu+%zu bytes", doc.channel_count, doc.programme_count, nfuu, bits_len, strs_len);
  }

  strrepo_writer_free(&sw);
  bitwriter_free(&bw);
  epg_doc_free(&doc);
  return rc;
}

static int decode_bim_to_xml(FILE *in, FILE *out, int verbose) {
  epg_doc_t doc;
  char *buf = NULL;
  size_t len;
  int rc = 0, nfuu = 0;

  epg_doc_init(&doc);
  if (read_all(in, &buf, &len) || len < 4) {
    rc = -1;
    goto done;
  }
  {
    const unsigned char *ubuf = (const unsigned char *)buf;
    size_t bits_len = ((size_t)ubuf[0] << 24) | ((size_t)ubuf[1] << 16) | ((size_t)ubuf[2] << 8) | (size_t)ubuf[3];
    bitreader_t br;
    strrepo_reader_t sr;

    if (bits_len > len - 4) {
      rc = -1;
      goto done;
    }
    bitreader_init(&br, ubuf + 4, bits_len);
    if (strrepo_reader_init(&sr, ubuf + 4 + bits_len, len - 4 - bits_len)) {
      rc = -1;
      goto done;
    }
    if (accessunit_decode(&br, &sr, &doc, &nfuu)) {
      rc = -1;
      goto done;
    }
    tva_xml_write(out, &doc);
    if (verbose)
      log_line("%d channels, %d programmes, %d fragments read", doc.channel_count, doc.programme_count, nfuu);
  }

done:
  free(buf);
  epg_doc_free(&doc);
  return rc;
}

int main(int argc, char **argv) {
  config_t cfg;
  args_status_t st;
  FILE *in, *out;
  int rc;

  log_set_color(color_prescan(argc, argv));
  log_line_ansi("\e[1m%s\e[0m \e[0;32mv%s\e[0m \e[0;37m%s\e[0m \e[0;37m%s\e[0m \e[0;34m%s\e[0m", TOOL_NAME, TOOL_VERSION, BUILD_ARCH, BUILD_TYPE, BUILD_LINK);
  st = args_parse(argc, argv, &cfg);
  if (st == ARGS_OK)
    log_set_color((log_color_t)cfg.color_mode);
  if (st == ARGS_HELP)
    return 0;
  if (st == ARGS_ERR) {
    fprintf(stderr, "try '%s --help' for usage\n", TOOL_NAME);
    return 2;
  }

  in = open_input(cfg.input_path);
  if (!in) {
    fprintf(stderr, TOOL_NAME ": cannot open %s\n", cfg.input_path);
    return 1;
  }
  out = open_output(cfg.output_path);
  if (!out) {
    fprintf(stderr, TOOL_NAME ": cannot open %s\n", cfg.output_path);
    if (in != stdin)
      fclose(in);
    return 1;
  }

  if (cfg.format == FMT_XML)
    rc = encode_xml_to_bim(in, out, cfg.verbose) ? 1 : 0;
  else
    rc = decode_bim_to_xml(in, out, cfg.verbose) ? 1 : 0;

  if (in != stdin)
    fclose(in);
  if (out != stdout)
    fclose(out);
  return rc;
}
