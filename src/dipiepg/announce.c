/* Copyright 2026 dvbipitools authors. Licensed under GPL-3.0-or-later.
 * See NOTICE and LICENSE for details and authorship information. */

#include <ctype.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

#include "announce.h"
#include "container.h"
#include "lib/bim/accessunit.h"
#include "lib/bim/bitwriter.h"
#include "lib/bim/strrepo.h"
#include "lib/log.h"
#include "lib/net/dvbstp.h"
#include "lib/net/multicast.h"
#include "lib/signal.h"
#include "lib/tva/epg_doc.h"
#include "lib/tva/mapping.h"
#include "lib/tva/xmltv.h"

static double mono(void) {
  struct timespec t;
  clock_gettime(CLOCK_MONOTONIC, &t);
  return (double)t.tv_sec + (double)t.tv_nsec / 1e9;
}

static void sleep_interruptible(double secs) {
  double deadline = mono() + secs;
  while (mono() < deadline && !signal_stop_requested()) {
    struct timespec req = {0, 100000000L};
    nanosleep(&req, NULL);
  }
}

/* EN 300 468 annex C, same formula as lib/bim/codec.c's dvbDateTimeCodec */
static long date_to_mjd(int y, int mo, int d) {
  int yy = mo <= 2 ? y - 1 : y;
  int mm = mo <= 2 ? mo + 12 : mo;
  return 14956L + d + (long)((yy - 1900) * 365.25) + (long)((mm + 1) * 30.6001);
}

static int all_digits(const char *s, int n) {
  int i;
  for (i = 0; i < n; i++)
    if (!isdigit((unsigned char)s[i]))
      return 0;
  return 1;
}

/* "YYYY-MM-DDTHH:MM:SS[Z|+HH:MM|-HH:MM]" -> minutes since the MJD epoch, UTC-normalized */
static int iso8601_to_minutes(const char *in, long *out) {
  size_t l = strlen(in);
  int y, mo, d, h, mi, off_min = 0;
  long mjd, total;

  if (l < 19 || in[4] != '-' || in[7] != '-' || in[10] != 'T' || in[13] != ':' || in[16] != ':')
    return -1;
  if (!all_digits(in, 4) || !all_digits(in + 5, 2) || !all_digits(in + 8, 2) ||
      !all_digits(in + 11, 2) || !all_digits(in + 14, 2))
    return -1;
  y = (in[0] - '0') * 1000 + (in[1] - '0') * 100 + (in[2] - '0') * 10 + (in[3] - '0');
  mo = (in[5] - '0') * 10 + (in[6] - '0');
  d = (in[8] - '0') * 10 + (in[9] - '0');
  h = (in[11] - '0') * 10 + (in[12] - '0');
  mi = (in[14] - '0') * 10 + (in[15] - '0');

  if (l > 19) {
    const char *tail = in + 19;
    if (*tail == '+' || *tail == '-') {
      if (strlen(tail) >= 6 && tail[3] == ':' && all_digits(tail + 1, 2) && all_digits(tail + 4, 2)) {
        int oh = (tail[1] - '0') * 10 + (tail[2] - '0');
        int om = (tail[4] - '0') * 10 + (tail[5] - '0');
        off_min = (oh * 60 + om) * (tail[0] == '-' ? -1 : 1);
      } else {
        return -1;
      }
    }
  }

  mjd = date_to_mjd(y, mo, d);
  total = mjd * 1440L + h * 60L + mi - off_min;
  *out = total;
  return 0;
}

static long now_minutes(void) {
  time_t t = time(NULL);
  struct tm tm;
  gmtime_r(&t, &tm);
  return date_to_mjd(tm.tm_year + 1900, tm.tm_mon + 1, tm.tm_mday) * 1440L + tm.tm_hour * 60L + tm.tm_min;
}

static int build_windowed_doc(const epg_doc_t *src, epg_doc_t *dst, long now, long window_min) {
  int i;
  epg_doc_init(dst);
  for (i = 0; i < src->channel_count; i++) {
    epg_channel_t *c = epg_add_channel(dst);
    if (!c)
      return -1;
    *c = src->channels[i];
  }
  for (i = 0; i < src->programme_count; i++) {
    const epg_programme_t *pr = &src->programmes[i];
    epg_programme_t *out;
    long start_min, end_min;
    if (iso8601_to_minutes(pr->start, &start_min))
      continue;
    end_min = start_min;
    if (pr->stop[0] && !iso8601_to_minutes(pr->stop, &end_min)) {
      /* end_min set */
    }
    if (end_min < now)
      continue;
    if (start_min > now + window_min)
      continue;
    out = epg_add_programme(dst);
    if (!out)
      return -1;
    *out = *pr;
  }
  return 0;
}

int announce_run(const config_t *cfg) {
  epg_doc_t doc;
  mapping_t map;
  mcast_t *m;
  FILE *in;
  unsigned cycles = 0;
  int rc = 0;
  char mcast_txt[80];

  in = strcmp(cfg->input_path, "-") ? fopen(cfg->input_path, "r") : stdin;
  if (!in) {
    log_line("cannot open %s", cfg->input_path);
    return 1;
  }
  epg_doc_init(&doc);
  if (xmltv_read(in, &doc)) {
    if (in != stdin)
      fclose(in);
    epg_doc_free(&doc);
    return 1;
  }
  if (in != stdin)
    fclose(in);

  if (mapping_load(cfg->map_path, &map)) {
    epg_doc_free(&doc);
    return 1;
  }
  {
    int i;
    for (i = 0; i < doc.channel_count; i++) {
      epg_channel_t *c = &doc.channels[i];
      char uri[EPG_ID_LEN];
      unsigned tsid, onid, sid;
      if (!mapping_lookup(&map, c->id, uri, sizeof uri, &tsid, &onid, &sid)) {
        snprintf(c->uri, sizeof c->uri, "%s", uri);
        c->tsid = tsid;
        c->onid = onid;
        c->sid = sid;
      }
    }
  }
  mapping_free(&map);

  m = mcast_open_send(cfg->family, cfg->mcast_group, cfg->mcast_port, cfg->iface, 0);
  if (!m) {
    log_line("cannot open %s:%u for sending", cfg->mcast_group, cfg->mcast_port);
    epg_doc_free(&doc);
    return 1;
  }

  mcast_describe(cfg, mcast_txt, sizeof mcast_txt);
  log_line("announcing %d channels, %d programmes (window %ldh) on %s every %lds", doc.channel_count, doc.programme_count, cfg->window_hours, mcast_txt, cfg->interval_s);

  while (!signal_stop_requested()) {
    epg_doc_t windowed;
    bitwriter_t bw;
    strrepo_writer_t sw;
    int nfuu = 0;

    if (build_windowed_doc(&doc, &windowed, now_minutes(), cfg->window_hours * 60)) {
      epg_doc_free(&windowed);
      rc = 1;
      break;
    }

    bitwriter_init(&bw);
    strrepo_writer_init(&sw);
    if (accessunit_encode(&windowed, &bw, &sw, &nfuu)) {
      bitwriter_free(&bw);
      strrepo_writer_free(&sw);
      epg_doc_free(&windowed);
      rc = 1;
      break;
    }
    {
      size_t bits_len, strs_len, cont_len;
      const unsigned char *bits = bitwriter_data(&bw, &bits_len);
      const unsigned char *strs = strrepo_writer_data(&sw, &strs_len);
      unsigned char *cont;
      if (container_build(bits, bits_len, strs, strs_len, &cont, &cont_len) == 0) {
        dvbstp_send_segment(m, DVBSTP_PAYLOAD_BCG_DATA_CONTAINER, 1, (unsigned)(cycles % 256), 0, 0, 1, cont, cont_len);
        free(cont);
      }
    }
    bitwriter_free(&bw);
    strrepo_writer_free(&sw);
    epg_doc_free(&windowed);

    cycles++;
    if (cfg->verbose)
      log_line("cycle %u sent, %d fragments", cycles, nfuu);
    sleep_interruptible((double)cfg->interval_s);
  }

  mcast_close(m);
  epg_doc_free(&doc);
  log_line("stopped after %u cycle%s", cycles, cycles == 1 ? "" : "s");
  return rc;
}
