/* Copyright 2026 dvbipitools authors. Licensed under GPL-3.0-or-later.
 * See NOTICE and LICENSE for details and authorship information. */

#include <stdlib.h>
#include <string.h>

#include "crc32.h"
#include "psi.h"

typedef struct {
  int active;
  size_t len;
  size_t expect; /* total section length, 0 until known */
  unsigned char buf[4096];
} sect_asm_t;

struct psi {
  sect_asm_t pat, pmt, sdt, nit;
  int have_pat, have_pmt, have_sdt, have_nit;
  unsigned program_number, pmt_pid, pcr_pid, nit_pid;
  unsigned tsid, onid;
  psi_es_t es[PSI_MAX_ES];
  int es_count, audio_count;
  unsigned ecm[PSI_MAX_ES];
  int ecm_count;
  char service_name[PSI_NAME], provider_name[PSI_NAME], network_name[PSI_NAME];
};

static void parse_pat(psi_t *c);
static void parse_pmt(psi_t *c);
static void parse_sdt(psi_t *c);
static void parse_nit(psi_t *c);

/* collect section; 1 when complete */
static int asm_feed(sect_asm_t *a, const unsigned char *pl, size_t plen, int pusi) {
  size_t i = 0;

  if (pusi) {
    unsigned ptr;
    if (plen < 1)
      return 0;
    ptr = pl[0];
    i = 1 + (size_t)ptr;
    if (i > plen) {
      a->active = 0;
      return 0;
    }
    a->len = 0;
    a->expect = 0;
    a->active = 1;
  } else if (!a->active) {
    return 0;
  }
  for (; i < plen; i++) {
    if (a->len < sizeof a->buf)
      a->buf[a->len++] = pl[i];
    if (a->expect == 0 && a->len >= 3) {
      unsigned sl = (((unsigned)a->buf[1] & 0x0F) << 8) | a->buf[2];
      a->expect = (size_t)sl + 3;
      if (a->expect > sizeof a->buf) {
        a->active = 0;
        return 0;
      }
    }
    if (a->expect != 0 && a->len >= a->expect) {
      a->active = 0;
      return 1;
    }
  }
  return 0;
}

/* find descriptor by tag; returns data */
static const unsigned char *find_desc(const unsigned char *d, size_t len, unsigned tag, size_t *dlen) {
  size_t i = 0;
  while (i + 2 <= len) {
    unsigned t = d[i], l = d[i + 1];
    if (i + 2 + l > len)
      break;
    if (t == tag) {
      *dlen = l;
      return d + i + 2;
    }
    i += 2 + l;
  }
  return NULL;
}

/* DVB text: skip charset prefix, controls -> space. not ISO 6937 */
static void copy_name(char *dst, size_t dstsz, const unsigned char *src, size_t len) {
  size_t i = 0, o = 0;
  if (len && src[0] < 0x20) {
    if (src[0] == 0x10 && len >= 3)
      i = 3;
    else if (src[0] == 0x1F && len >= 2)
      i = 2;
    else
      i = 1;
  }
  for (; i < len && o + 1 < dstsz; i++)
    dst[o++] = (src[i] < 0x20) ? ' ' : (char)src[i];
  dst[o] = '\0';
}

static void add_ecm(psi_t *c, unsigned pid) {
  int k;
  if (pid == 0 || pid == 0x1FFF)
    return;
  for (k = 0; k < c->ecm_count; k++)
    if (c->ecm[k] == pid)
      return;
  if (c->ecm_count < PSI_MAX_ES)
    c->ecm[c->ecm_count++] = pid;
}

static void classify(psi_es_t *e, const unsigned char *desc, size_t dlen) {
  size_t l;
  const unsigned char *ld;

  e->codec = CODEC_NONE;
  e->cls = PID_DATA;
  e->lang[0] = '\0';
  switch (e->stream_type) {
    case 0x01:
    case 0x02:
      e->cls = PID_VIDEO;
      e->codec = CODEC_MPEG2V;
      break;
    case 0x1B:
      e->cls = PID_VIDEO;
      e->codec = CODEC_H264;
      break;
    case 0x24:
      e->cls = PID_VIDEO;
      e->codec = CODEC_HEVC;
      break;
    case 0x03:
    case 0x04:
      e->cls = PID_AUDIO;
      e->codec = CODEC_MP2A;
      break;
    case 0x0F:
      e->cls = PID_AUDIO;
      e->codec = CODEC_AAC;
      break;
    case 0x11:
      e->cls = PID_AUDIO;
      e->codec = CODEC_AAC_LATM;
      break;
    case 0x81:
      e->cls = PID_AUDIO;
      e->codec = CODEC_AC3;
      break;
    case 0x87:
      e->cls = PID_AUDIO;
      e->codec = CODEC_EAC3;
      break;
    case 0x06:
      if ((ld = find_desc(desc, dlen, 0x56, &l)) != NULL) {
        size_t x;
        e->cls = PID_TELETEXT;
        /* 5-byte entries; prefer subtitle (type 2/5) */
        for (x = 0; x + 5 <= l; x += 5) {
          int ty = ld[x + 3] >> 3;
          unsigned mag = ld[x + 3] & 0x07;
          unsigned pg = ld[x + 4];
          unsigned page = (mag ? mag : 8) * 100 + ((pg >> 4) & 0x0F) * 10 + (pg & 0x0F);
          if (!e->ttx_page || ty == 2 || ty == 5) {
            e->ttx_page = page;
            e->ttx_type = ty;
            memcpy(e->ttx_lang, ld + x, 3);
            e->ttx_lang[3] = '\0';
          }
          if (ty == 2 || ty == 5)
            break;
        }
      } else if (find_desc(desc, dlen, 0x59, &l))
        e->cls = PID_SUBTITLE;
      else if (find_desc(desc, dlen, 0x6A, &l)) {
        e->cls = PID_AUDIO;
        e->codec = CODEC_AC3;
      } else if (find_desc(desc, dlen, 0x7A, &l)) {
        e->cls = PID_AUDIO;
        e->codec = CODEC_EAC3;
      }
      break;
    case 0x05:
      if (find_desc(desc, dlen, 0x6F, &l))
        e->cls = PID_AIT;
      break;
    default:
      break;
  }

  ld = find_desc(desc, dlen, 0x0A, &l);
  if (ld && l >= 3) {
    e->lang[0] = (char)ld[0];
    e->lang[1] = (char)ld[1];
    e->lang[2] = (char)ld[2];
    e->lang[3] = '\0';
  }
}

static void parse_pat(psi_t *c) {
  const unsigned char *b = c->pat.buf;
  size_t n = c->pat.expect, i, end;
  if (n < 12 || b[0] != 0x00 || crc32_mpeg(b, n) != 0)
    return;
  c->pmt_pid = 0;
  c->nit_pid = 0;
  c->tsid = ((unsigned)b[3] << 8) | b[4];
  end = n - 4;
  for (i = 8; i + 4 <= end; i += 4) {
    unsigned prog = ((unsigned)b[i] << 8) | b[i + 1];
    unsigned pid = (((unsigned)b[i + 2] & 0x1F) << 8) | b[i + 3];
    if (prog == 0)
      c->nit_pid = pid;
    else if (!c->pmt_pid) {
      c->pmt_pid = pid;
      c->program_number = prog;
    }
  }
  c->have_pat = 1;
}

static void parse_pmt(psi_t *c) {
  const unsigned char *b = c->pmt.buf;
  size_t n = c->pmt.expect, i, end, pil, l;
  unsigned prog;
  const unsigned char *ca;
  int k;
  if (n < 16 || b[0] != 0x02 || crc32_mpeg(b, n) != 0)
    return;
  prog = ((unsigned)b[3] << 8) | b[4];
  if (prog != c->program_number)
    return;

  c->pcr_pid = (((unsigned)b[8] & 0x1F) << 8) | b[9];
  pil = (((size_t)b[10] & 0x0F) << 8) | b[11];
  c->es_count = 0;
  c->audio_count = 0;
  c->ecm_count = 0;
  if (12 + pil <= n) {
    ca = find_desc(b + 12, pil, 0x09, &l);
    if (ca && l >= 4)
      add_ecm(c, (((unsigned)ca[2] & 0x1F) << 8) | ca[3]);
  }

  end = n - 4;
  i = 12 + pil;
  while (i + 5 <= end && c->es_count < PSI_MAX_ES) {
    psi_es_t *e = &c->es[c->es_count];
    size_t esil = (((size_t)b[i + 3] & 0x0F) << 8) | b[i + 4];
    const unsigned char *desc = b + i + 5;
    if (i + 5 + esil > end)
      break;
    memset(e, 0, sizeof *e);
    e->stream_type = b[i];
    e->pid = (((unsigned)b[i + 1] & 0x1F) << 8) | b[i + 2];
    classify(e, desc, esil);
    ca = find_desc(desc, esil, 0x09, &l);
    if (ca && l >= 4) {
      e->ca_pid = (((unsigned)ca[2] & 0x1F) << 8) | ca[3];
      add_ecm(c, e->ca_pid);
    }
    c->es_count++;
    i += 5 + esil;
  }
  for (k = 0; k < c->es_count; k++)
    if (c->es[k].cls == PID_AUDIO)
      c->es[k].audio_index = ++c->audio_count;
  c->have_pmt = 1;
}

static void parse_sdt(psi_t *c) {
  const unsigned char *b = c->sdt.buf;
  size_t n = c->sdt.expect, i, end, dll, l;

  if (n < 12 || b[0] != 0x42 || crc32_mpeg(b, n) != 0)
    return;
  c->onid = ((unsigned)b[8] << 8) | b[9];
  end = n - 4;
  i = 11;
  while (i + 5 <= end) {
    unsigned sid = ((unsigned)b[i] << 8) | b[i + 1];
    const unsigned char *d = b + i + 5;
    dll = (((size_t)b[i + 3] & 0x0F) << 8) | b[i + 4];
    if (i + 5 + dll > end)
      break;
    if (sid == c->program_number) {
      const unsigned char *sd = find_desc(d, dll, 0x48, &l);
      if (sd && l >= 2) {
        size_t pnl = sd[1];
        if (2 + pnl <= l) {
          copy_name(c->provider_name, sizeof c->provider_name, sd + 2, pnl);
          if (2 + pnl < l) {
            size_t snl = sd[2 + pnl];
            if (3 + pnl + snl <= l)
              copy_name(c->service_name, sizeof c->service_name, sd + 3 + pnl, snl);
          }
        }
      }
    }
    i += 5 + dll;
  }
  c->have_sdt = 1;
}

static void parse_nit(psi_t *c) {
  const unsigned char *b = c->nit.buf;
  size_t n = c->nit.expect, ndl, l;
  const unsigned char *nn;

  if (n < 12 || b[0] != 0x40 || crc32_mpeg(b, n) != 0)
    return;
  ndl = (((size_t)b[8] & 0x0F) << 8) | b[9];
  if (10 + ndl > n)
    return;
  nn = find_desc(b + 10, ndl, 0x40, &l);
  if (nn)
    copy_name(c->network_name, sizeof c->network_name, nn, l);
  c->have_nit = 1;
}

psi_t *psi_new(void) { return calloc(1, sizeof(psi_t)); }

void psi_free(psi_t *c) { free(c); }

void psi_feed(psi_t *c, const unsigned char *pkt) {
  unsigned pid, afc;
  int pusi;
  size_t off, plen;
  const unsigned char *pl;

  if (pkt[0] != 0x47)
    return;
  pid = (((unsigned)pkt[1] & 0x1F) << 8) | pkt[2];
  pusi = pkt[1] & 0x40;
  afc = (pkt[3] >> 4) & 0x3;
  if (afc == 0 || afc == 2) /* no payload */
    return;
  off = 4;
  if (afc == 3) {
    off = 5 + (size_t)pkt[4];
    if (off >= 188)
      return;
  }
  pl = pkt + off;
  plen = 188 - off;

  if (pid == 0x0000) {
    if (asm_feed(&c->pat, pl, plen, pusi))
      parse_pat(c);
  } else if (pid == 0x0010) {
    if (asm_feed(&c->nit, pl, plen, pusi))
      parse_nit(c);
  } else if (pid == 0x0011) {
    if (asm_feed(&c->sdt, pl, plen, pusi))
      parse_sdt(c);
  } else if (c->have_pat && pid == c->pmt_pid) {
    if (asm_feed(&c->pmt, pl, plen, pusi))
      parse_pmt(c);
  }
}

int psi_have_pat(const psi_t *c) { return c->have_pat; }
int psi_have_pmt(const psi_t *c) { return c->have_pmt; }
int psi_have_sdt(const psi_t *c) { return c->have_sdt; }
int psi_ready(const psi_t *c) { return c->have_pat && c->have_pmt; }

unsigned psi_program_number(const psi_t *c) { return c->program_number; }
unsigned psi_pmt_pid(const psi_t *c) { return c->pmt_pid; }
unsigned psi_pcr_pid(const psi_t *c) { return c->pcr_pid; }
unsigned psi_nit_pid(const psi_t *c) { return c->nit_pid; }
unsigned psi_transport_stream_id(const psi_t *c) { return c->tsid; }
unsigned psi_original_network_id(const psi_t *c) { return c->onid; }

const psi_es_t *psi_es(const psi_t *c, int *count) {
  if (count)
    *count = c->es_count;
  return c->es;
}

int psi_audio_count(const psi_t *c) { return c->audio_count; }

const char *psi_service_name(const psi_t *c) { return c->service_name; }
const char *psi_provider_name(const psi_t *c) { return c->provider_name; }
const char *psi_network_name(const psi_t *c) { return c->network_name; }

pid_class_t psi_classify(const psi_t *c, unsigned pid) {
  int k;

  if (pid == 0x0000)
    return PID_PAT;
  if (pid == 0x0001)
    return PID_CAT;
  if (pid == 0x0010)
    return PID_NIT;
  if (pid == 0x0011)
    return PID_SDT;
  if (pid == 0x0012)
    return PID_EIT;
  if (pid == 0x0013 || pid == 0x0014)
    return PID_OTHER_SI;
  if (pid == 0x1FFF)
    return PID_NULL;
  if (c->have_pat && c->nit_pid && pid == c->nit_pid)
    return PID_NIT;
  if (c->have_pmt) {
    if (pid == c->pmt_pid)
      return PID_PMT;
    for (k = 0; k < c->es_count; k++)
      if (c->es[k].pid == pid)
        return c->es[k].cls;
    for (k = 0; k < c->ecm_count; k++)
      if (c->ecm[k] == pid)
        return PID_ECM;
    if (pid == c->pcr_pid)
      return PID_PCR;
  } else if (c->have_pat && pid == c->pmt_pid) {
    return PID_PMT;
  }
  return PID_UNKNOWN;
}

const unsigned char *psi_pat_section(const psi_t *c, size_t *len) {
  if (!c->have_pat) {
    if (len)
      *len = 0;
    return NULL;
  }
  if (len)
    *len = c->pat.expect;
  return c->pat.buf;
}

const unsigned char *psi_pmt_section(const psi_t *c, size_t *len) {
  if (!c->have_pmt) {
    if (len)
      *len = 0;
    return NULL;
  }
  if (len)
    *len = c->pmt.expect;
  return c->pmt.buf;
}

const char *pid_class_name(pid_class_t k) {
  switch (k) {
    case PID_PAT:       return "PAT";
    case PID_CAT:       return "CAT";
    case PID_PMT:       return "PMT";
    case PID_NIT:       return "NIT";
    case PID_SDT:       return "SDT";
    case PID_EIT:       return "EIT";
    case PID_OTHER_SI:  return "SI";
    case PID_NULL:      return "null";
    case PID_PCR:       return "PCR";
    case PID_VIDEO:     return "video";
    case PID_AUDIO:     return "audio";
    case PID_TELETEXT:  return "teletext";
    case PID_SUBTITLE:  return "subtitle";
    case PID_AIT:       return "AIT";
    case PID_ECM:       return "ECM";
    case PID_DATA:      return "data";
    case PID_UNKNOWN:   return "unknown";
  }
  return "unknown";
}

const char *codec_name(codec_t k) {
  switch (k) {
    case CODEC_MPEG2V:    return "mpeg2video";
    case CODEC_H264:      return "h264";
    case CODEC_HEVC:      return "hevc";
    case CODEC_MP2A:      return "mp2";
    case CODEC_AAC:       return "aac";
    case CODEC_AAC_LATM:  return "aac_latm";
    case CODEC_AC3:       return "ac3";
    case CODEC_EAC3:      return "eac3";
    case CODEC_NONE:      return "none";
  }
  return "none";
}
