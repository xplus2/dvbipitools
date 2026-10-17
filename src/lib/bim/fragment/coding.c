/* Copyright 2026 dvbipitools authors. Licensed under GPL-3.0-or-later.
 * See NOTICE and LICENSE for details and authorship information. */

#include <stdio.h>
#include <string.h>
#include "lib/bim/codec.h"
#include "lib/bim/fragment.h"
#include "lib/sys/ioutil.h"
#include "lib/tva/tva_xml.h"

static bim_node_t *add_leaf(bim_node_t *parent, const char *name, const char *text) { return bim_node_add_new(parent, name, text); }

static int encode_tree(const bim_type_t *t, bim_node_t *root, bitwriter_t *bw, strrepo_writer_t *sw) {
  int rc = root ? bim_encode_payload(t, root, bw, sw) : -1;
  bim_node_free(root);
  return rc;
}

int fragment_encode_program_information(const bcg_programme_t *pr, bitwriter_t *bw, strrepo_writer_t *sw) {
  char buf[TVA_CRID_BUF];
  bim_node_t *root = bim_node_new("ProgramInformation");
  bim_node_t *bd;
  if (!root || bim_node_set_attr(root, "programId", tva_programme_crid(pr, buf, sizeof buf))) goto fail;
  bd = add_leaf(root, "BasicDescription", NULL);
  if (!bd) goto fail;
  if (pr->title && !add_leaf(bd, "Title", pr->title)) goto fail;
  if (pr->desc && !add_leaf(bd, "Synopsis", pr->desc)) goto fail;
  if (pr->category) {
    bim_node_t *g = add_leaf(bd, "Genre", NULL);
    if (!g || bim_node_set_attr(g, "href", tva_genre_href(pr->category)) || !add_leaf(g, "Name", pr->category)) goto fail;
  }
  return encode_tree(tva_type_program_information, root, bw, sw);
fail:
  bim_node_free(root);
  return -1;
}

int fragment_decode_program_information(bitreader_t *br, strrepo_reader_t *sr, char *crid_out, size_t crid_cap, bcg_programme_t *pr_out) {
  bim_node_t *root;
  const bim_node_t *bd;
  const char *id;
  memset(pr_out, 0, sizeof *pr_out);
  if (bim_decode_payload(tva_type_program_information, "ProgramInformation", br, sr, &root)) return -1;
  id = bim_node_attr(root, "programId");
  bd = bim_node_child(root, "BasicDescription");
  if (!id || !bd || strlen(id) >= crid_cap) goto fail;
  memcpy(crid_out, id, strlen(id) + 1);
  for (int i = 0; i < bd->nkids; i++) {
    const bim_node_t *k = bd->kids[i];
    if (!strcmp(k->name, "Title") && !pr_out->title) {
      if (bcg_set_text(&pr_out->title, k->text)) goto fail;
    } else if (!strcmp(k->name, "Synopsis") && !pr_out->desc) {
      if (bcg_set_text(&pr_out->desc, k->text)) goto fail;
    } else if (!strcmp(k->name, "Genre") && !pr_out->category) {
      const bim_node_t *name = bim_node_child(k, "Name");
      if (name && bcg_set_text(&pr_out->category, name->text)) goto fail;
    }
  }
  bim_node_free(root);
  return 0;
fail:
  bcg_programme_free_text(pr_out);
  bim_node_free(root);
  return -1;
}

int fragment_encode_schedule(const char *channel_id, const bcg_programme_t *programmes, int first, const int *next, bitwriter_t *bw, strrepo_writer_t *sw) {
  bim_node_t *root = bim_node_new("Schedule");
  if (!root || bim_node_set_attr(root, "serviceIDRef", channel_id)) goto fail;
  for (int j = first; j != -1; j = next[j]) {
    const bcg_programme_t *pr = &programmes[j];
    char buf[TVA_CRID_BUF];
    bim_node_t *ev = add_leaf(root, "ScheduleEvent", NULL);
    bim_node_t *p = ev ? add_leaf(ev, "Program", NULL) : NULL;
    if (!p || bim_node_set_attr(p, "crid", tva_programme_crid(pr, buf, sizeof buf))) goto fail;
    if (!add_leaf(ev, "PublishedStartTime", pr->start)) goto fail;
    if (pr->stop[0] && !add_leaf(ev, "PublishedEndTime", pr->stop)) goto fail;
  }
  return encode_tree(tva_type_schedule, root, bw, sw);
fail:
  bim_node_free(root);
  return -1;
}

int fragment_decode_schedule(bitreader_t *br, strrepo_reader_t *sr, bcg_doc_t *doc, fragment_text_lookup_fn lookup, void *ctx) {
  bim_node_t *root;
  const char *channel;
  if (bim_decode_payload(tva_type_schedule, "Schedule", br, sr, &root)) return -1;
  channel = bim_node_attr(root, "serviceIDRef");
  if (!channel || strlen(channel) >= BCG_ID_LEN) goto fail;
  for (int i = 0; i < root->nkids; i++) {
    const bim_node_t *ev = root->kids[i];
    const bim_node_t *prog = bim_node_child(ev, "Program");
    const bim_node_t *start = bim_node_child(ev, "PublishedStartTime");
    const bim_node_t *stop = bim_node_child(ev, "PublishedEndTime");
    const char *crid = prog ? bim_node_attr(prog, "crid") : NULL;
    bcg_programme_t *pr;
    if (!crid || !start || !start->text || strlen(start->text) >= BCG_TIME_LEN) continue;
    pr = bcg_add_programme(doc);
    if (!pr) goto fail;
    bufcpy(pr->channel_id, sizeof pr->channel_id, channel);
    bufcpy(pr->start, sizeof pr->start, start->text);
    if (stop && stop->text && strlen(stop->text) < BCG_TIME_LEN) bufcpy(pr->stop, sizeof pr->stop, stop->text);
    if (lookup && lookup(ctx, crid, pr) == -2) goto fail;
  }
  bim_node_free(root);
  return 0;
fail:
  bim_node_free(root);
  return -1;
}

/* two strings after the fragment's own: dvb:// triplet ("" if none), "1" if Name was synthesized.
   receivers find strings via the fragment reference and never see them */
int fragment_encode_service_information(const bcg_channel_t *c, bitwriter_t *bw, strrepo_writer_t *sw) {
  char dtt[64];
  sbuf_t db;
  bim_node_t *root = bim_node_new("ServiceInformation");
  if (!root || bim_node_set_attr(root, "serviceId", c->id)) goto fail;
  if (c->name_count == 0 && !add_leaf(root, "Name", c->id)) goto fail;
  for (int j = 0; j < c->name_count; j++)
    if (!add_leaf(root, "Name", c->names[j])) goto fail;
  if (!add_leaf(root, "ServiceURL", c->uri)) goto fail;
  if (encode_tree(tva_type_service_information, root, bw, sw)) return -1;
  sbuf_init(&db, dtt, sizeof dtt);
  if (c->onid || c->tsid || c->sid) {
    sbuf_add(&db, "dvb://");
    sbuf_add_hex(&db, c->onid);
    sbuf_add(&db, ".");
    sbuf_add_hex(&db, c->tsid);
    sbuf_add(&db, ".");
    sbuf_add_hex(&db, c->sid);
  }
  if (dvb_string_encode(sw, dtt)) return -1;
  return dvb_string_encode(sw, c->name_count == 0 ? "1" : "0");
fail:
  bim_node_free(root);
  return -1;
}

int fragment_decode_service_information(bitreader_t *br, strrepo_reader_t *sr, bcg_channel_t *c_out) {
  bim_node_t *root;
  const char *id;
  const bim_node_t *url;
  char dtt[64];
  char synth[8];
  unsigned onid;
  unsigned tsid;
  unsigned sid;
  int no_names = 0;
  int dropped = 0;
  size_t tail_pos;
  memset(c_out, 0, sizeof *c_out);
  if (bim_decode_payload(tva_type_service_information, "ServiceInformation", br, sr, &root)) return -1;
  id = bim_node_attr(root, "serviceId");
  if (!id || strlen(id) >= sizeof c_out->id) goto fail;
  memcpy(c_out->id, id, strlen(id) + 1);
  url = bim_node_child(root, "ServiceURL");
  if (url && url->text && strlen(url->text) < sizeof c_out->uri) memcpy(c_out->uri, url->text, strlen(url->text) + 1);
  tail_pos = sr->pos;
  if (!dvb_string_decode(sr, dtt, sizeof dtt) && !dvb_string_decode(sr, synth, sizeof synth) && (!strcmp(synth, "0") || !strcmp(synth, "1")) &&
      (!dtt[0] || !strncmp(dtt, "dvb://", 6))) {
    no_names = synth[0] == '1';
    if (sscanf(dtt, "dvb://%x.%x.%x", &onid, &tsid, &sid) == 3) {
      c_out->onid = onid;
      c_out->tsid = tsid;
      c_out->sid = sid;
    }
  } else {
    sr->pos = tail_pos; /* foreign: strings belong to next fragment */
  }
  for (int i = 0; !no_names && i < root->nkids; i++) {
    if (strcmp(root->kids[i]->name, "Name") || !root->kids[i]->text) continue;
    if (strlen(root->kids[i]->text) >= BCG_ID_LEN) {
      dropped++;
    } else if (bcg_channel_add_name(c_out, root->kids[i]->text)) {
      dropped++;
    }
  }
  if (dropped) log_code("LW400234", "bcg: channel %s: %d <Name> dropped (max %d names, %d bytes each)", c_out->id, dropped, BCG_MAX_NAMES, BCG_ID_LEN - 1);
  bim_node_free(root);
  return 0;
fail:
  bim_node_free(root);
  return -1;
}
