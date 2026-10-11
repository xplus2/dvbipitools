/* Copyright 2026 dvbipitools authors. Licensed under GPL-3.0-or-later.
 * See NOTICE and LICENSE for details and authorship information. */

#include <stdio.h>
#include <stdlib.h>

#include "cli/args.h"
#include "lib/bim/accessunit.h"
#include "lib/bim/bimreader.h"
#include "lib/bim/bitwriter.h"
#include "lib/bim/strrepo.h"
#include "lib/helper/fileutil.h"
#include "lib/sys/ioutil.h"
#include "lib/helper/log.h"
#include "lib/helper/toolmain.h"
#include "lib/tva/bcg_doc.h"
#include "lib/tva/tva_xml.h"
#include "version.h"

static int encode_xml_to_bim(FILE *in, FILE *out, int verbose) {
  bcg_doc_t doc;
  bitwriter_t bw;
  strrepo_writer_t sw;
  accessunit_scratch_t sc;
  int rc = 0, nfuu = 0;

  bcg_doc_init(&doc);
  bitwriter_init(&bw);
  strrepo_writer_init(&sw);
  accessunit_scratch_init(&sc);
  if (tva_xml_read(in, &doc)) {
    log_code("BE400101", "cannot parse input XML");
    rc = -1;
  } else if (accessunit_encode(&sc, &doc, &bw, &sw, &nfuu)) {
    log_code("BE400102", "BiM encoding failed");
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
    if (fwrite(lenbuf, 1, 4, out) != 4 || fwrite(bits, 1, bits_len, out) != bits_len || fwrite(strs, 1, strs_len, out) != strs_len) {
      log_code("BE100103", "write failed");
      rc = -1;
    } else if (verbose) {
      log_code("BD700101", "%d channels, %d programmes, %d fragments -> %zu+%zu bytes", doc.channel_count, doc.programme_count, nfuu, bits_len, strs_len);
    }
  }
  accessunit_scratch_free(&sc);
  strrepo_writer_free(&sw);
  bitwriter_free(&bw);
  bcg_doc_free(&doc);
  return rc;
}

static int decode_bim_to_xml(FILE *in, FILE *out, int verbose) {
  bcg_doc_t doc;
  accessunit_scratch_t sc;
  char *buf = NULL;
  size_t len;
  int rc = 0, nfuu = 0;
  const unsigned char *ubuf;
  size_t bits_len;
  bitreader_t br;
  strrepo_reader_t sr;

  bcg_doc_init(&doc);
  accessunit_scratch_init(&sc);
  if (read_all(in, &buf, &len)) {
    log_code("BE000123", "read failed");
    rc = -1;
    goto done;
  }
  if (len < 4) {
    log_code("BE400103", "input truncated: %zu bytes, need 4 byte length header", len);
    rc = -1;
    goto done;
  }
  ubuf = (const unsigned char *)buf;
  bits_len = ((size_t)ubuf[0] << 24) | ((size_t)ubuf[1] << 16) | ((size_t)ubuf[2] << 8) | (size_t)ubuf[3];
  if (bits_len > len - 4) {
    log_code("BE400104", "input truncated: bitstream needs %zu bytes, %zu present", bits_len, len - 4);
    rc = -1;
    goto done;
  }
  bitreader_init(&br, ubuf + 4, bits_len);
  if (strrepo_reader_init(&sr, ubuf + 4 + bits_len, len - 4 - bits_len)) {
    log_code("BE400105", "string repository missing or unsupported encoding");
    rc = -1;
    goto done;
  }
  if (accessunit_decode(&sc, &br, &sr, &doc, &nfuu)) {
    log_code("BE400106", "malformed or truncated BiM stream");
    rc = -1;
    goto done;
  }
  tva_xml_write(out, &doc);
  if (verbose) log_code("BD700102", "%d channels, %d programmes, %d fragments read", doc.channel_count, doc.programme_count, nfuu);

done:
  accessunit_scratch_free(&sc);
  free(buf);
  bcg_doc_free(&doc);
  return rc;
}

int main(int argc, char **argv) {
  config_t cfg;
  FILE *in, *out;
  int rc;

  TOOLMAIN_STARTUP(argc, argv, &cfg, args_parse);
  in = fileutil_open_std(cfg.input_path, "r");
  if (!in) {
    log_code("BS000100", TOOL_NAME ": cannot open %s", cfg.input_path);
    return 1;
  }
  out = fileutil_open_std(cfg.output_path, "w");
  if (!out) {
    log_code("BS100104", TOOL_NAME ": cannot open %s", cfg.output_path);
    if (in != stdin) fclose(in);
    return 1;
  }
  if (cfg.format == FMT_XML) rc = encode_xml_to_bim(in, out, cfg.verbose) ? 1 : 0;
  else rc = decode_bim_to_xml(in, out, cfg.verbose) ? 1 : 0;

  if (in != stdin) fclose(in);
  if (out != stdout && fclose(out) && rc == 0) {
    log_code("BE100102", TOOL_NAME ": error writing %s", cfg.output_path);
    rc = 1;
  }
  return rc;
}
