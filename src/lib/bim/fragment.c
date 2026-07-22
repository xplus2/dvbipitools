/* Copyright 2026 dvbipitools authors. Licensed under GPL-3.0-or-later.
 * See NOTICE and LICENSE for details and authorship information. */

#include <stdio.h>
#include <string.h>

#include "codec.h"
#include "fragment.h"
#include "lib/tva/tva_xml.h"

static int put_bit(bitwriter_t *bw, int v) { return bitwriter_put(bw, v ? 1 : 0, 1); }

static int get_bit(bitreader_t *br) {
  uint64_t v;
  if (bitreader_get(br, 1, &v))
    return -1;
  return (int)v;
}

int fragment_encode_program_information(const epg_programme_t *pr, bitwriter_t *bw, strrepo_writer_t *sw) {
  char crid[EPG_ID_LEN * 3 + 64];
  tva_build_crid(pr->channel_id, pr->start, crid, sizeof crid);
  if (dvb_locator_encode(bw, sw, crid))
    return -1;
  if (put_bit(bw, pr->title[0] != 0))
    return -1;
  if (pr->title[0] && dvb_string_encode(sw, pr->title))
    return -1;
  if (put_bit(bw, pr->desc[0] != 0))
    return -1;
  if (pr->desc[0] && dvb_string_encode(sw, pr->desc))
    return -1;
  if (put_bit(bw, pr->category[0] != 0))
    return -1;
  if (pr->category[0]) {
    if (dvb_controlledterm_encode(bw, sw, "urn:tva:metadata:cs:ContentCS:2011:3.0"))
      return -1;
    if (dvb_string_encode(sw, pr->category))
      return -1;
  }
  return 0;
}

int fragment_decode_program_information(bitreader_t *br, strrepo_reader_t *sr, char *crid_out, size_t crid_cap, epg_programme_t *pr_out) {
  int present;
  memset(pr_out, 0, sizeof *pr_out);
  if (dvb_locator_decode(br, sr, crid_out, crid_cap))
    return -1;
  if ((present = get_bit(br)) < 0)
    return -1;
  if (present && dvb_string_decode(sr, pr_out->title, sizeof pr_out->title))
    return -1;
  if ((present = get_bit(br)) < 0)
    return -1;
  if (present && dvb_string_decode(sr, pr_out->desc, sizeof pr_out->desc))
    return -1;
  if ((present = get_bit(br)) < 0)
    return -1;
  if (present) {
    char href[256];
    if (dvb_controlledterm_decode(br, sr, href, sizeof href))
      return -1;
    if (dvb_string_decode(sr, pr_out->category, sizeof pr_out->category))
      return -1;
  }
  return 0;
}

int fragment_encode_schedule(const char *channel_id, const epg_programme_t *programmes, int count, bitwriter_t *bw, strrepo_writer_t *sw) {
  int j;
  if (dvb_string_encode(sw, channel_id))
    return -1;
  for (j = 0; j < count; j++) {
    const epg_programme_t *pr = &programmes[j];
    char crid[EPG_ID_LEN * 3 + 64];
    if (strcmp(pr->channel_id, channel_id))
      continue;
    if (put_bit(bw, 1))
      return -1;
    tva_build_crid(pr->channel_id, pr->start, crid, sizeof crid);
    if (dvb_locator_encode(bw, sw, crid))
      return -1;
    if (dvb_datetime_encode(bw, pr->start))
      return -1;
    if (put_bit(bw, pr->stop[0] != 0))
      return -1;
    if (pr->stop[0] && dvb_datetime_encode(bw, pr->stop))
      return -1;
  }
  return put_bit(bw, 0);
}

int fragment_decode_schedule(bitreader_t *br, strrepo_reader_t *sr, epg_doc_t *doc, fragment_text_lookup_fn lookup, void *ctx) {
  char channel[EPG_ID_LEN];
  if (dvb_string_decode(sr, channel, sizeof channel))
    return -1;
  for (;;) {
    int more = get_bit(br);
    char crid[EPG_ID_LEN * 3 + 64];
    epg_programme_t *pr;
    int present;
    if (more < 0)
      return -1;
    if (!more)
      return 0;
    if (dvb_locator_decode(br, sr, crid, sizeof crid))
      return -1;
    pr = epg_add_programme(doc);
    if (!pr)
      return -1;
    snprintf(pr->channel_id, sizeof pr->channel_id, "%s", channel);
    if (dvb_datetime_decode(br, pr->start, sizeof pr->start))
      return -1;
    if ((present = get_bit(br)) < 0)
      return -1;
    if (present) {
      if (dvb_datetime_decode(br, pr->stop, sizeof pr->stop))
        return -1;
    } else {
      pr->stop[0] = '\0';
    }
    if (lookup)
      lookup(ctx, crid, pr);
  }
}

int fragment_encode_service_information(const epg_channel_t *c, bitwriter_t *bw, strrepo_writer_t *sw) {
  int j;
  char dtt[64];
  if (dvb_string_encode(sw, c->id))
    return -1;
  for (j = 0; j < c->name_count; j++) {
    if (put_bit(bw, 1))
      return -1;
    if (dvb_string_encode(sw, c->names[j]))
      return -1;
  }
  if (put_bit(bw, 0))
    return -1;
  if (dvb_locator_encode(bw, sw, c->uri))
    return -1;
  snprintf(dtt, sizeof dtt, "dvb://%u.%u.%u", c->onid, c->tsid, c->sid);
  return dvb_locator_encode(bw, sw, dtt);
}

int fragment_decode_service_information(bitreader_t *br, strrepo_reader_t *sr, epg_channel_t *c_out) {
  char dtt[64];
  unsigned onid, tsid, sid;
  memset(c_out, 0, sizeof *c_out);
  if (dvb_string_decode(sr, c_out->id, sizeof c_out->id))
    return -1;
  for (;;) {
    int more = get_bit(br);
    char name[EPG_ID_LEN];
    if (more < 0)
      return -1;
    if (!more)
      break;
    if (dvb_string_decode(sr, name, sizeof name))
      return -1;
    epg_channel_add_name(c_out, name);
  }
  if (dvb_locator_decode(br, sr, c_out->uri, sizeof c_out->uri))
    return -1;
  if (dvb_locator_decode(br, sr, dtt, sizeof dtt))
    return -1;
  if (sscanf(dtt, "dvb://%u.%u.%u", &onid, &tsid, &sid) == 3) {
    c_out->onid = onid;
    c_out->tsid = tsid;
    c_out->sid = sid;
  }
  return 0;
}
