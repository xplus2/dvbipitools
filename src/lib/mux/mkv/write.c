/* Copyright 2026 dvbipitools authors. Licensed under GPL-3.0-or-later.
 * See NOTICE and LICENSE for details and authorship information. */

#include <errno.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <unistd.h>

#include "lib/helper/log.h"
#include "lib/sys/ioutil.h"
#include "priv.h"

void wfd(mkv_t *m, const void *p, size_t n) {
  const unsigned char *b = p;

  while (n && !m->err) {
    ssize_t w = write(m->fd, b, n);
    if (w < 0) {
      if (errno == EINTR)
        continue;
      log_line("mkv write: %s", strerror(errno));
      m->err = 1;
      return;
    }
    b += w;
    n -= (size_t)w;
    *m->bytes += (unsigned long long)w;
  }
}

track_t *find_track(mkv_t *m, unsigned pid) {
  if (m->last_trk_idx >= 0 && m->last_trk_idx < m->ntrk && m->trk[m->last_trk_idx].pid == pid)
    return &m->trk[m->last_trk_idx];
  for (int i = 0; i < m->ntrk; i++) {
    if (m->trk[i].pid == pid) {
      m->last_trk_idx = i;
      return &m->trk[i];
    }
  }
  return NULL;
}

void cluster_flush(mkv_t *m) {
  unsigned char buf[16]; /* eb_id max 4 bytes + eb_size max 8 bytes, always fits */
  ebuf_t hdr;
  if (!m->cl_open) return;
  if (m->cl_cue && m->seekable && !m->err) {
    if (growbuf_reserve((void **)&m->cue, &m->cuecap, sizeof *m->cue, m->ncue + 1, 64) < 0) {
      m->err = 1;
      return;
    }
    m->cue[m->ncue].time = m->cl_base;
    m->cue[m->ncue].pos = *m->bytes - m->seg_data;
    m->ncue++;
  }
  hdr.p = buf;
  hdr.len = 0;
  hdr.cap = sizeof buf;
  hdr.err = 0;
  eb_id(&hdr, 0x1F43B675);
  eb_size(&hdr, m->cl.len);
  wfd(m, hdr.p, hdr.len);
  wfd(m, m->cl.p, m->cl.len); /* straight to output, no throwaway copy of whole cluster */
  m->cl.len = 0;              /* keep m->cl's allocation, next cluster reuses it */
  m->cl_open = 0;
}

void put_block(mkv_t *m, int num, int64_t rel, const unsigned char *d, size_t n, int key, int64_t dur) {
  unsigned char hdr[4];
  int16_t tc;
  int cut_key = key && (!m->cut_num || num == m->cut_num);

  if (!m->cl_open || (rel - m->cl_base >= CLUSTER_MS && cut_key) || rel - m->cl_base >= CLUSTER_MAX_MS || rel - m->cl_base < -CLUSTER_MAX_MS) {
    cluster_flush(m);
    if (rel < 0)
      rel = 0;
    eb_uint(&m->cl, 0xE7, (uint64_t)rel);
    m->cl_base = rel;
    m->cl_open = 1;
    m->cl_cue = cut_key;
  }
  if (rel + (dur > 0 ? dur : 0) > m->dur_ms)
    m->dur_ms = rel + (dur > 0 ? dur : 0);
  tc = (int16_t)(rel - m->cl_base);
  hdr[0] = (unsigned char)(0x80 | num);
  hdr[1] = (unsigned char)(tc >> 8);
  hdr[2] = (unsigned char)tc;
  hdr[3] = key ? 0x80 : 0x00;
  if (dur > 0) { /* explicit duration */
    ebuf_t bg;
    memset(&bg, 0, sizeof bg);
    hdr[3] = 0x00; /* Block: no keyframe flag */
    eb_id(&bg, 0xA1);
    eb_size(&bg, sizeof hdr + n);
    eb_bytes(&bg, hdr, sizeof hdr);
    eb_bytes(&bg, d, n);
    eb_uint(&bg, 0x9B, (uint64_t)dur); /* BlockDuration */
    eb_master(&m->cl, 0xA0, &bg);      /* BlockGroup */
    return;
  }
  eb_id(&m->cl, 0xA3);
  eb_size(&m->cl, sizeof hdr + n);
  eb_bytes(&m->cl, hdr, sizeof hdr);
  eb_bytes(&m->cl, d, n);
}

static void simpletag(ebuf_t *tag, const char *name, const char *val) {
  ebuf_t st;
  if (!val || !*val)
    return;
  memset(&st, 0, sizeof st);
  eb_str(&st, 0x45A3, name);
  eb_str(&st, 0x447A, "und");
  eb_str(&st, 0x4487, val);
  eb_master(tag, 0x67C8, &st);
}

static void write_head(mkv_t *m) {
  static const unsigned char seg[] = {0x18, 0x53, 0x80, 0x67, 0x01, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF};
  const char *app = m->opts->app_name;
  const char *srcuri = m->opts->source_desc ? m->opts->source_desc : "";
  char date[32];
  ebuf_t b, e;
  time_t now = time(NULL);
  struct tm tm;
  ebuf_t tag;
  ebuf_t tgt;

  gmtime_r(&now, &tm);
  strftime(date, sizeof date, "%Y-%m-%d %H:%M:%S", &tm);
  memset(&b, 0, sizeof b);
  memset(&e, 0, sizeof e);
  eb_uint(&e, 0x4286, 1);
  eb_uint(&e, 0x42F7, 1);
  eb_uint(&e, 0x42F2, 4);
  eb_uint(&e, 0x42F3, 8);
  eb_str(&e, 0x4282, "matroska");
  eb_uint(&e, 0x4287, 4);
  eb_uint(&e, 0x4285, 2);
  eb_master(&b, 0x1A45DFA3, &e);
  wfd(m, b.p, b.len);
  ebuf_free(&b);

  m->seekable = lseek(m->fd, 0, SEEK_CUR) >= 0;
  m->seg_size_pos = *m->bytes + 4;
  wfd(m, seg, sizeof seg);
  m->seg_data = *m->bytes;
  if (m->seekable) {
    unsigned char sh[MKV_SEEKHEAD_RESERVE];
    memset(sh, 0, sizeof sh);
    sh[0] = 0xEC; /* Void, SeekHead is patched in at close */
    sh[1] = 0x80 | (MKV_SEEKHEAD_RESERVE - 2);
    wfd(m, sh, sizeof sh);
  }
  memset(&b, 0, sizeof b);
  memset(&e, 0, sizeof e);
  eb_uint(&e, 0x2AD7B1, 1000000);
  eb_str(&e, 0x4D80, app);
  eb_str(&e, 0x5741, app);
  eb_uint(&e, 0x4461, (uint64_t)((int64_t)(now - 978307200) * 1000000000LL));
  if (*psi_service_name(m->psi[0]))
    eb_str(&e, 0x7BA9, psi_service_name(m->psi[0])); /* first program if -p all */
  if (m->seekable)
    eb_float(&e, 0x4489, 0.0); /* Duration, last: payload is the final 8 bytes */
  eb_master(&b, 0x1549A966, &e);
  m->pos_info = *m->bytes - m->seg_data;
  wfd(m, b.p, b.len);
  m->dur_pos = *m->bytes - 8;
  ebuf_free(&b);

  memset(&b, 0, sizeof b);
  memset(&e, 0, sizeof e);
  for (int i = 0; i < m->ntrk; i++) {
    track_t *t = &m->trk[i];
    ebuf_t te, sub;
    int video = (t->cls == PID_VIDEO);
    memset(&te, 0, sizeof te);
    memset(&sub, 0, sizeof sub);
    eb_uint(&te, 0xD7, (uint64_t)t->num);
    eb_uint(&te, 0x73C5, (uint64_t)t->num);
    eb_uint(&te, 0x83, (t->cls == PID_TELETEXT) ? 17 : (video ? 1 : 2));
    eb_uint(&te, 0x9C, 0);
    eb_str(&te, 0x22B59C, t->lang[0] ? t->lang : "und");
    if (m->npsi > 1) {
      const char *name = psi_service_name(m->psi[t->psi_idx]);
      if (name[0])
        eb_str(&te, 0x536E, name);
    }
    eb_str(&te, 0x86, t->codecid);
    if (t->es.cpriv_len)
      eb_bin(&te, 0x63A2, t->es.cpriv, t->es.cpriv_len);
    if (t->cls == PID_TELETEXT) {
      ebuf_free(&sub); /* no settings element */
    } else if (video) {
      eb_uint(&sub, 0xB0, t->width ? t->width : 720);
      eb_uint(&sub, 0xBA, t->height ? t->height : 576);
      eb_master(&te, 0xE0, &sub);
    } else {
      eb_float(&sub, 0xB5, (double)(t->es.rate ? t->es.rate : 48000));
      eb_uint(&sub, 0x9F, t->es.channels ? t->es.channels : 2);
      eb_master(&te, 0xE1, &sub);
    }
    eb_master(&e, 0xAE, &te);
  }
  eb_master(&b, 0x1654AE6B, &e);
  m->pos_tracks = *m->bytes - m->seg_data;
  wfd(m, b.p, b.len);
  ebuf_free(&b);
  memset(&b, 0, sizeof b);
  memset(&e, 0, sizeof e);
  memset(&tag, 0, sizeof tag);
  memset(&tgt, 0, sizeof tgt);
  eb_uint(&tgt, 0x68CA, 50);
  eb_master(&tag, 0x63C0, &tgt);
  simpletag(&tag, "TITLE", psi_service_name(m->psi[0]));
  simpletag(&tag, "NETWORK", psi_network_name(m->psi[0]));
  simpletag(&tag, "PROVIDER", psi_provider_name(m->psi[0]));
  simpletag(&tag, "SOURCE", srcuri);
  simpletag(&tag, "DATE_RECORDED", date);
  eb_master(&e, 0x7373, &tag);
  eb_master(&b, 0x1254C367, &e);
  m->pos_tags = *m->bytes - m->seg_data;
  wfd(m, b.p, b.len);
  ebuf_free(&b);
}

static void patch(mkv_t *m, const void *p, size_t n, uint64_t pos) {
  if (!m->err && pwrite(m->fd, p, n, (off_t)pos) != (ssize_t)n) {
    log_line("mkv patch: %s", strerror(errno));
    m->err = 1;
  }
}

static void seek_entry(ebuf_t *sh, uint32_t id, uint64_t pos) {
  unsigned char idb[4] = {(unsigned char)(id >> 24), (unsigned char)(id >> 16), (unsigned char)(id >> 8), (unsigned char)id};
  ebuf_t se;
  memset(&se, 0, sizeof se);
  eb_bin(&se, 0x53AB, idb, sizeof idb);
  eb_uint(&se, 0x53AC, pos);
  eb_master(sh, 0x4DBB, &se);
}

static void write_cues(mkv_t *m) {
  ebuf_t cues;
  ebuf_t out;
  int track = m->cut_num ? m->cut_num : m->trk[0].num;

  memset(&cues, 0, sizeof cues);
  for (size_t i = 0; i < m->ncue; i++) {
    ebuf_t cp, tp;
    memset(&cp, 0, sizeof cp);
    memset(&tp, 0, sizeof tp);
    eb_uint(&cp, 0xB3, (uint64_t)m->cue[i].time);
    eb_uint(&tp, 0xF7, (uint64_t)track);
    eb_uint(&tp, 0xF1, m->cue[i].pos);
    eb_master(&cp, 0xB7, &tp);
    eb_master(&cues, 0xBB, &cp);
  }
  memset(&out, 0, sizeof out);
  eb_master(&out, 0x1C53BB6B, &cues);
  wfd(m, out.p, out.len);
  ebuf_free(&out);
}

void seg_finish(mkv_t *m) {
  ebuf_t sh, out;
  unsigned char sz[8];
  uint64_t pos_cues = 0;
  uint64_t end;
  double dur = (double)m->dur_ms;
  union {
    double d;
    uint64_t u;
  } c;

  if (!m->started || !m->seekable || m->err) return;
  if (m->ncue) {
    pos_cues = *m->bytes - m->seg_data;
    write_cues(m);
  }
  end = *m->bytes;
  c.d = dur;
  for (int i = 0; i < 8; i++)
    sz[i] = (unsigned char)(c.u >> (56 - 8 * i));
  patch(m, sz, sizeof sz, m->dur_pos);
  sz[0] = 0x01; /* 8 byte size vint */
  for (int i = 1; i < 8; i++)
    sz[i] = (unsigned char)((end - m->seg_data) >> (56 - 8 * i));
  patch(m, sz, sizeof sz, m->seg_size_pos);
  memset(&sh, 0, sizeof sh);
  seek_entry(&sh, 0x1549A966, m->pos_info);
  seek_entry(&sh, 0x1654AE6B, m->pos_tracks);
  seek_entry(&sh, 0x1254C367, m->pos_tags);
  if (m->ncue)
    seek_entry(&sh, 0x1C53BB6B, pos_cues);
  memset(&out, 0, sizeof out);
  eb_master(&out, 0x114D9B74, &sh);
  if (!out.err && out.len + 2 <= MKV_SEEKHEAD_RESERVE) {
    size_t rest = MKV_SEEKHEAD_RESERVE - out.len;
    eb_id(&out, 0xEC);
    eb_size(&out, rest - 2);
    for (size_t i = 0; i < rest - 2; i++)
      eb_bytes(&out, "", 1);
    patch(m, out.p, out.len, m->seg_data);
  }
  ebuf_free(&out);
}

void start(mkv_t *m) {
  if (m->started || !m->ntrk)
    return;
  m->t0 = 0;
  m->cut_num = 0;
  if (m->npend) {
    int vnum = 0, found = 0;
    int64_t vt = 0;
    for (int i = 0; i < m->ntrk; i++)
      if (m->trk[i].cls == PID_VIDEO) {
        vnum = m->trk[i].num;
        break;
      }
    m->t0 = m->pend[0].ts;
    for (int i = 1; i < m->npend; i++)
      if (m->pend[i].ts < m->t0)
        m->t0 = m->pend[i].ts;
    /* with video: start at first keyframe */
    for (int i = 0; vnum && i < m->npend; i++)
      if (m->pend[i].num == vnum && (!found || m->pend[i].ts < vt)) {
        vt = m->pend[i].ts;
        found = 1;
      }
    if (found) {
      m->t0 = vt;
      for (int i = 0; i < m->npend; i++) {
        pid_class_t cls = PID_VIDEO;
        for (int k = 0; k < m->ntrk; k++)
          if (m->trk[k].num == m->pend[i].num)
            cls = m->trk[k].cls;
        if (cls == PID_VIDEO || cls == PID_TELETEXT)
          continue;
        if (m->pend[i].ts < m->t0 && vt - m->pend[i].ts <= PTS_LEAD_KEEP_MS)
          m->t0 = m->pend[i].ts;
      }
    }
  }
  for (int i = 0; i < m->ntrk; i++)
    if (m->trk[i].cls == PID_VIDEO) {
      m->cut_num = m->trk[i].num;
      break;
    }
  write_head(m);
  m->started = 1;
  for (int i = 0; i < m->npend; i++)
    if (m->pend[i].ts >= m->t0)
      put_block(m, m->pend[i].num, m->pend[i].ts - m->t0, m->pend[i].data, m->pend[i].len, m->pend[i].key, m->pend[i].dur);
  m->npend = 0;
  m->pend_bytes = 0;
}

void pend_add(mkv_t *m, int num, int64_t ts, const unsigned char *d, size_t n, int key, int64_t dur) {
  pend_t *p;

  if (m->npend >= MKV_PEND_MAX || m->pend_bytes + n > MKV_PEND_BYTES) {
    start(m); /* give up on silent track */
    if (m->started)
      put_block(m, num, ts - m->t0, d, n, key, dur);
    return;
  }
  p = &m->pend[m->npend];
  p->data = m->pend_arena + m->pend_bytes;
  memcpy(p->data, d, n);
  p->num = num;
  p->ts = ts;
  p->len = n;
  p->key = key;
  p->dur = dur;
  m->npend++;
  m->pend_bytes += n;
}

void emit(mkv_t *m, track_t *t, const unsigned char *d, size_t n, int key) {
  if (!m->started) {
    pend_add(m, t->num, t->ts_ms, d, n, key, 0);
    return;
  }
  if (t->ts_ms < m->t0) /* before start point */
    return;
  put_block(m, t->num, t->ts_ms - m->t0, d, n, key, 0);
}
