/* Copyright 2026 dvbipitools authors. Licensed under GPL-3.0-or-later.
 * See NOTICE and LICENSE for details and authorship information. */

#include <arpa/inet.h>
#include <stdarg.h>
#include <poll.h>
#include <pthread.h>
#include <stdatomic.h>
#include <stdlib.h>
#include <string.h>
#include <sys/socket.h>
#include <sys/time.h>
#include <time.h>

#include "format.h"
#include "lib/demux/psi/psi.h"
#include "lib/demux/rtp.h"
#include "lib/demux/tspack.h"
#include "lib/helper/describe.h"
#include "lib/sys/ioutil.h"
#include "lib/helper/log.h"
#include "lib/net/httpclient/httpclient.h"
#include "lib/net/multicast.h"
#include "lib/sys/signal.h"
#include "scan.h"
#include "version.h"

/* candidate group address at idx i in cfg->start, cfg->end */
void addr_at(const config_t *cfg, unsigned i, char *buf, size_t n) {
  unsigned char a[16];
  int alen = (cfg->family == AF_INET6) ? 16 : 4;
  unsigned delta = i - 1;
  memcpy(a, cfg->start, (size_t)alen);
  for (int p = alen - 1; p >= 0 && delta; p--) {
    unsigned sum = a[p] + (delta & 0xFF);
    a[p] = (unsigned char)sum;
    delta = (delta >> 8) + (sum >> 8);
  }
  inet_ntop(cfg->family, a, buf, (socklen_t)n);
}

/* -M: every PAT-listed program named. a program stuck at pmt-resolved-but-nameless still exits early once
   quiet/timeout deadline hits. this only short-circuits when everything's in. */
int multi_all_named(const psi_t *psi) {
  int count;
  const psi_multi_program_t *m;
  if (!psi_have_pat(psi)) return 0;
  m = psi_multi_programs(psi, &count);
  if (count == 0) return 0;
  for (int i = 0; i < count; i++) {
    if (!m[i].resolved || !m[i].service_name[0]) return 0;
  }
  return 1;
}

int probe_cb(void *v, const unsigned char *pkt) {
  probe_ctx_t *pc = v;
  pc->pkts++;
  psi_feed(pc->psi, pkt);
  if (pc->multi) return multi_all_named(pc->psi) ? 1 : 0;
  return (psi_have_pat(pc->psi) && psi_service_name(pc->psi)[0]) ? 1 : 0;
}

static ssize_t mcast_read_adapter(void *ctx, unsigned char *buf, size_t cap) { return mcast_recv((mcast_t *)ctx, buf, cap, NULL); }
static ssize_t http_read_adapter(void *ctx, unsigned char *buf, size_t cap) { return http_read((http_t *)ctx, buf, cap, NULL); }

/* first packet budget as percent of -t, dead addrs bail early */
#define PROBE_QUIET_PCT 30

void probe_common(psi_t *psi, chan_read_fn rf, void *rctx, int timeout_ms, int multi, int detect_rtp, probe_result_t *r) {
  unsigned char buf[65536];
  tspack_t pz;
  probe_ctx_t pc;
  double deadline, quiet_deadline;
  unsigned rtp_dgrams = 0, plain_dgrams = 0;

  memset(&pz, 0, sizeof pz);
  memset(r, 0, sizeof *r);
  r->rtp_wrapped = -1;
  psi_reset(psi);
  pc.psi = psi;
  pc.pkts = 0;
  pc.multi = multi;
  if (multi) psi_enable_multi_program(pc.psi);
  deadline = mono_seconds() + (double)timeout_ms / 1000.0;
  quiet_deadline = mono_seconds() + (double)timeout_ms * PROBE_QUIET_PCT / 100000.0;
  if (quiet_deadline > deadline) quiet_deadline = deadline;
  while (mono_seconds() < (pc.pkts ? deadline : quiet_deadline) && !signal_stop_requested()) {
    ssize_t n = rf(rctx, buf, sizeof buf);
    size_t off;
    if (n < 0) break;
    if (n == 0) continue;
    off = detect_rtp ? rtp_payload_offset(buf, (size_t)n) : 0;
    if (off) {
      rtp_dgrams++;
      memmove(buf, buf + off, (size_t)n - off);
      n -= (ssize_t)off;
    } else {
      plain_dgrams++;
    }
    if (tspack_feed(&pz, buf, (size_t)n, probe_cb, &pc)) break;
  }
  if (rtp_dgrams || plain_dgrams) r->rtp_wrapped = rtp_dgrams > plain_dgrams ? 1 : 0;
  r->pkts = pc.pkts;
  if (pc.pkts == 0) {
    r->kind = PROBE_NONE;
  } else if (multi) {
    int count;
    const psi_multi_program_t *m = psi_multi_programs(pc.psi, &count);
    r->tsid = psi_transport_stream_id(pc.psi);
    r->onid = psi_original_network_id(pc.psi);
    for (int i = 0; i < count && r->program_count < PSI_MAX_PROGRAMS; i++) {
      scan_program_t *p;
      if (!m[i].resolved) continue;
      p = &r->programs[r->program_count++];
      p->sid = m[i].program_number;
      bufcpy(p->name, sizeof p->name, m[i].service_name[0] ? m[i].service_name : "(no SDT)");
    }
    r->kind = r->program_count ? PROBE_NAMED : PROBE_UNNAMED;
  } else if (psi_have_pat(pc.psi) && psi_service_name(pc.psi)[0]) {
    r->kind = PROBE_NAMED;
    bufcpy(r->name, sizeof r->name, psi_service_name(pc.psi));
    r->tsid = psi_transport_stream_id(pc.psi);
    r->onid = psi_original_network_id(pc.psi);
    r->sid = psi_program_number(pc.psi);
  } else {
    r->kind = PROBE_UNNAMED;
    if (psi_have_pat(pc.psi)) {
      r->tsid = psi_transport_stream_id(pc.psi);
      r->sid = psi_program_number(pc.psi);
    }
  }
}

/* expands %g/%p/%%. template pre-validated (args.c). unknown esc stops expansion */
static void http_path_expand(const char *tmpl, const char *group, unsigned port, char *out, size_t outcap) {
  char portbuf[16];
  size_t o = 0;
  if (outcap == 0) return;
  uint_to_str(portbuf, port);
  for (; *tmpl && o + 1 < outcap; tmpl++) {
    const char *ins = NULL;
    if (*tmpl == '%') {
      tmpl++;
      if (*tmpl == 'g')       ins = group;
      else if (*tmpl == 'p')  ins = portbuf;
      else if (*tmpl == '%')  ins = "%";
      else break;
    }
    if (ins) {
      size_t l = strlen(ins);
      size_t space = outcap - 1 - o; /* o+1 < outcap above: never underflows */
      if (l > space) l = space;
      memcpy(out + o, ins, l);
      o += l;
    } else {
      out[o++] = *tmpl;
    }
  }
  out[o] = '\0';
}

#define PROBE_HTTP_READ_TIMEOUT_US 200000

/* connect + headers within budget_ms. NULL on timeout, error or stop request */
static http_t *http_get_budget(const http_url_t *url, int budget_ms) {
  http_async_t *a = http_async_start(url, TOOL_NAME "/" TOOL_VERSION, 0, NULL, NULL);
  double deadline = mono_seconds() + (double)budget_ms / 1000.0;
  if (!a) return NULL;
  for (;;) {
    struct pollfd pfd;
    double remain = deadline - mono_seconds();
    int wait_ms;
    http_async_state_t st = http_async_step(a, NULL);
    if (st == HTTP_ASYNC_DONE) return http_async_take(a);
    if (st == HTTP_ASYNC_ERROR || remain <= 0 || signal_stop_requested()) break;
    pfd.fd = http_async_poll_fd(a);
    pfd.events = http_async_poll_events(a);
    pfd.revents = 0;
    wait_ms = remain > 0.1 ? 100 : (int)(remain * 1000.0) + 1;
    poll(&pfd, 1, wait_ms);
  }
  http_async_free(a);
  return NULL;
}

static void probe_address(psi_t *psi, const config_t *cfg, const char *group, unsigned port, probe_result_t *r) {
  if (cfg->http_proxy) {
    http_url_t url;
    http_t *h;
    struct timeval tv;
    double t0 = mono_seconds();
    int left_ms;
    memset(&url, 0, sizeof url);
    bufcpy(url.host, sizeof url.host, cfg->http_proxy_host);
    url.port = cfg->http_proxy_port;
    http_path_expand(cfg->http_path_tmpl ? cfg->http_path_tmpl : "/udp/%g:%p/", group, port, url.path, sizeof url.path);
    h = http_get_budget(&url, cfg->timeout_ms);
    if (!h) {
      memset(r, 0, sizeof *r);
      r->kind = PROBE_NONE;
      return;
    }
    tv.tv_sec = 0;
    tv.tv_usec = PROBE_HTTP_READ_TIMEOUT_US;
    setsockopt(http_fd(h), SOL_SOCKET, SO_RCVTIMEO, &tv, sizeof tv);
    left_ms = cfg->timeout_ms - (int)((mono_seconds() - t0) * 1000.0);
    probe_common(psi, http_read_adapter, h, left_ms > 0 ? left_ms : 1, cfg->mpts, 0, r);
    http_close(h);
  } else {
    mcast_t *m = mcast_open(cfg->family, group, port, cfg->iface, 200);
    if (!m) {
      memset(r, 0, sizeof *r);
      r->kind = PROBE_NONE;
      return;
    }
    probe_common(psi, mcast_read_adapter, m, cfg->timeout_ms, cfg->mpts, 1, r);
    mcast_close(m);
  }
}

static void report_mpts_programs(const config_t *cfg, FILE *out, const probe_result_t *r, unsigned i, const char *uri, const char *group, unsigned port, unsigned *found) {
  for (int k = 0; k < r->program_count; k++) {
    (*found)++;
    if (cfg->verbose) log_line("%u/%u %-28s %-32s sid=%u [%u pkts]", i, cfg->total, uri, r->programs[k].name, r->programs[k].sid, r->pkts);
    else              log_line("%u/%u %-28s %s (sid=%u)", i, cfg->total, uri, r->programs[k].name, r->programs[k].sid);
    format_item(out, cfg->format, r->programs[k].name, uri, cfg->family, group, port, r->rtp_wrapped == 1, r->tsid, r->onid, r->programs[k].sid);
  }
  if (r->program_count == 0)
    log_code_ansi("SW400109", "%u/%u %-28s \e[0;33mstream present, no program resolved\e[0m", i, cfg->total, uri);
}

static void report_single_program(const config_t *cfg, FILE *out, const probe_result_t *r, unsigned i, const char *uri, const char *group, unsigned port, unsigned *found) {
  const char *name = (r->kind == PROBE_NAMED) ? r->name : "(no SDT)";
  (*found)++;
  if (cfg->verbose) log_line("%u/%u %-28s %-32s [%u pkts]", i, cfg->total, uri, name, r->pkts);
  else              log_line("%u/%u %-28s %s", i, cfg->total, uri, name);
  format_item(out, cfg->format, name, uri, cfg->family, group, port, r->rtp_wrapped == 1, r->tsid, r->onid, r->sid);
}

typedef struct {
  const config_t *cfg;
  FILE *out;
  atomic_uint next_claim;
  pthread_mutex_t mtx;
  pthread_cond_t cv;
  unsigned next_commit;
  unsigned total;
  unsigned found;
} scan_job_t;

static void *scan_worker(void *arg) {
  scan_job_t *job = arg;
  const config_t *cfg = job->cfg;
  unsigned nports = cfg->port_hi - cfg->port_lo + 1;
  probe_result_t *res = malloc(nports * sizeof *res);
  psi_t *psi = psi_new();

  if (!res || !psi) {
    log_code("SE500101", "out of memory");
    free(res);
    psi_free(psi);
    return NULL;
  }
  for (;;) {
    unsigned n;
    unsigned i = atomic_fetch_add_explicit(&job->next_claim, 1u, memory_order_relaxed) + 1u;
    char group[64];
    if (i > cfg->total) break;
    if (signal_stop_requested()) {
      pthread_mutex_lock(&job->mtx);
      while (job->next_commit != i) pthread_cond_wait(&job->cv, &job->mtx);
      job->next_commit = i + 1;
      pthread_cond_broadcast(&job->cv);
      pthread_mutex_unlock(&job->mtx);
      break;
    }

    addr_at(cfg, i, group, sizeof group);
    for (n = 0; n < nports; n++) {
      probe_address(psi, cfg, group, cfg->port_lo + n, &res[n]);
      if (signal_stop_requested()) {
        n++;
        break;
      }
    }

    pthread_mutex_lock(&job->mtx);
    while (job->next_commit != i) pthread_cond_wait(&job->cv, &job->mtx);
    pthread_mutex_unlock(&job->mtx);
    for (unsigned k = 0; k < n; k++) {
      const probe_result_t *r = &res[k];
      unsigned port = cfg->port_lo + k;
      const char *proto = (r->rtp_wrapped == 1) ? "rtp" : "udp";
      char uri[96];

      describe_mcast_uri(uri, sizeof uri, proto, cfg->family, group, port);
      job->total++;
      if (r->kind == PROBE_NONE)
        log_line_ansi("%u/%u %-28s \e[0;31mno stream\e[0m", i, cfg->total, uri);
      else if (cfg->mpts)
        report_mpts_programs(cfg, job->out, r, i, uri, group, port, &job->found);
      else
        report_single_program(cfg, job->out, r, i, uri, group, port, &job->found);
    }
    pthread_mutex_lock(&job->mtx);
    job->next_commit = i + 1;
    pthread_cond_broadcast(&job->cv);
    pthread_mutex_unlock(&job->mtx);
    if (signal_stop_requested()) break;
  }
  psi_free(psi);
  free(res);
  return NULL;
}

static void inv_add(char *buf, size_t n, const char *fmt, ...) {
  size_t len = strlen(buf);
  va_list ap;
  if (len >= n - 1) return;
  va_start(ap, fmt);
  vsnprintf(buf + len, n - len, fmt, ap);
  va_end(ap);
}

static void invocation_build(const config_t *cfg, const char *basestr, char *buf, size_t n) {
  static const char *const fmt_names[] = {"m3u", "csv", "xspf", "xml", "null"};
  buf[0] = '\0';
  inv_add(buf, n, "%s --mcast %s --port ", TOOL_NAME, basestr);
  if (cfg->port_lo == cfg->port_hi) inv_add(buf, n, "%u", cfg->port_lo);
  else                              inv_add(buf, n, "%u-%u", cfg->port_lo, cfg->port_hi);
  inv_add(buf, n, " --timeout %d", cfg->timeout_ms / 1000);
  inv_add(buf, n, " -f %s", fmt_names[cfg->format]);
  if (cfg->mpts) inv_add(buf, n, " -M");
  if (cfg->http_proxy) {
    if (strchr(cfg->http_proxy_host, ':')) inv_add(buf, n, " -u '[%s]:%u'", cfg->http_proxy_host, cfg->http_proxy_port);
    else                                   inv_add(buf, n, " -u '%s:%u'", cfg->http_proxy_host, cfg->http_proxy_port);
  }
  if (cfg->http_path_tmpl) inv_add(buf, n, " -x '%s'", cfg->http_path_tmpl);
  if (cfg->iface) inv_add(buf, n, " -I '%s'", cfg->iface);
}

int scan_run(const config_t *cfg, FILE *out) {
  char invocation[768], basestr[128], lo[64], hi[64];
  double start = mono_seconds();
  int interrupted;
  int af = cfg->family == AF_INET6 ? AF_INET6 : AF_INET;
  unsigned jets = cfg->jets ? cfg->jets : 1;
  if (jets > cfg->total) jets = cfg->total ? cfg->total : 1;
  pthread_t threads[DIPISCAN_MAX_JETS];
  int started[DIPISCAN_MAX_JETS] = {0};
  scan_job_t job = {.cfg = cfg, .out = out, .next_commit = 1, .total = 0, .found = 0};
  args_range_describe(cfg, basestr, sizeof basestr);
  inet_ntop(af, cfg->start, lo, sizeof lo);
  inet_ntop(af, cfg->end, hi, sizeof hi);
  log_line("%s - %s (%u address%s)", lo, hi, cfg->total, cfg->total == 1 ? "" : "es");
  invocation_build(cfg, basestr, invocation, sizeof invocation);

  format_init(out, cfg->format, invocation, cfg->provider);
  atomic_init(&job.next_claim, 0u);
  pthread_mutex_init(&job.mtx, NULL);
  pthread_cond_init(&job.cv, NULL);
  for (unsigned t = 1; t < jets; t++) {
    started[t] = pthread_create(&threads[t], NULL, scan_worker, &job) == 0;
  }
  scan_worker(&job);
  for (unsigned t = 1; t < jets; t++) {
    if (started[t]) pthread_join(threads[t], NULL);
  }
  pthread_cond_destroy(&job.cv);
  pthread_mutex_destroy(&job.mtx);
  format_close(out, cfg->format);
  interrupted = signal_stop_requested() ? 1 : 0;
  if (interrupted)
    log_line("interrupted: found %u station%s (of %u probed) in %.1fs", job.found, job.found == 1 ? "" : "s", job.total, mono_seconds() - start);
  else
    log_line("found %u station%s in %.1fs", job.found, job.found == 1 ? "" : "s", mono_seconds() - start);

  return interrupted;
}
