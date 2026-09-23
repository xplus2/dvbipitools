/* Copyright 2026 dvbipitools authors. Licensed under GPL-3.0-or-later.
 * See NOTICE and LICENSE for details and authorship information. */

#ifdef HAVE_HTTP3

#include "../http3.h"
#include "../http3_int.h"

#include "lib/helper/log.h"

#include <arpa/inet.h>
#include <string.h>
#include <unistd.h>

SSL_CTX *g_h3_ssl_ctx = NULL;

_Thread_local struct sockaddr_storage t_h3_udp4_local;
_Thread_local socklen_t t_h3_udp4_local_len;
_Thread_local struct sockaddr_storage t_h3_udp6_local;
_Thread_local socklen_t t_h3_udp6_local_len;

static int h3_alpn_select_cb(SSL *ssl, const unsigned char **out, unsigned char *outlen, const unsigned char *in, unsigned int inlen, void *arg) {
  (void)ssl;
  (void)arg;
  static const unsigned char alpn[] = "\x02h3";
  if (SSL_select_next_proto((unsigned char **)out, outlen, alpn, sizeof alpn - 1, in, inlen) == OPENSSL_NPN_NEGOTIATED)
    return SSL_TLSEXT_ERR_OK;
  return SSL_TLSEXT_ERR_NOACK;
}

void h3_init(const char *cert_path, const char *key_path) {
  ngtcp2_crypto_ossl_init();
  if (h3_stateless_init() != 0) return;
  g_h3_ssl_ctx = SSL_CTX_new(TLS_server_method());
  if (!g_h3_ssl_ctx) return;
  SSL_CTX_set_min_proto_version(g_h3_ssl_ctx, TLS1_3_VERSION);
  SSL_CTX_set_max_proto_version(g_h3_ssl_ctx, TLS1_3_VERSION);
  SSL_CTX_set_mode(g_h3_ssl_ctx, SSL_MODE_RELEASE_BUFFERS);
  SSL_CTX_set_alpn_select_cb(g_h3_ssl_ctx, h3_alpn_select_cb, NULL);
  if (SSL_CTX_use_certificate_chain_file(g_h3_ssl_ctx, cert_path) != 1 || SSL_CTX_use_PrivateKey_file(g_h3_ssl_ctx, key_path, SSL_FILETYPE_PEM) != 1) {
    SSL_CTX_free(g_h3_ssl_ctx);
    g_h3_ssl_ctx = NULL;
    return;
  }
  log_line("http3: quic context ready");
}

int h3_ready(void) {
  return g_h3_ssl_ctx != NULL;
}

void h3_cleanup(void) {
  if (g_h3_ssl_ctx) {
    SSL_CTX_free(g_h3_ssl_ctx);
    g_h3_ssl_ctx = NULL;
  }
}

/* caller (reactor.c) owns epoll registration, its dispatch loop keys off
   reactor_listener pointers not raw fds */
int h3_create_udp_sock(int port, const char *host) {
  if (!g_h3_ssl_ctx) return -1;
  int sock = socket(AF_INET, SOCK_DGRAM | SOCK_NONBLOCK, 0);
  if (sock < 0) return -1;
  int opt = 1;
  setsockopt(sock, SOL_SOCKET, SO_REUSEADDR, &opt, sizeof opt);
  setsockopt(sock, SOL_SOCKET, SO_REUSEPORT, &opt, sizeof opt);
  /* set DF, no IP fragmentation: QUIC datagrams must drop not fragment, for path-MTU discovery (RFC 9000 14) */
  int mtud = IP_PMTUDISC_PROBE;
  setsockopt(sock, IPPROTO_IP, IP_MTU_DISCOVER, &mtud, sizeof mtud);

  struct sockaddr_in saddr = {0};
  saddr.sin_family = AF_INET;
  saddr.sin_port = htons((uint16_t)port);
  inet_pton(AF_INET, host, &saddr.sin_addr);

  if (bind(sock, (struct sockaddr *)&saddr, sizeof saddr) < 0) {
    close(sock);
    return -1;
  }
  h3_udp_gso_probe(sock);
  memset(&t_h3_udp4_local, 0, sizeof t_h3_udp4_local);
  memcpy(&t_h3_udp4_local, &saddr, sizeof saddr);
  t_h3_udp4_local_len = sizeof saddr;
  return sock;
}

int h3_create_udp_sock6(int port, const char *host6) {
  if (!g_h3_ssl_ctx) return -1;
  int sock = socket(AF_INET6, SOCK_DGRAM | SOCK_NONBLOCK, 0);
  if (sock < 0) return -1;
  int opt = 1;
  setsockopt(sock, SOL_SOCKET, SO_REUSEADDR, &opt, sizeof opt);
  setsockopt(sock, SOL_SOCKET, SO_REUSEPORT, &opt, sizeof opt);
  setsockopt(sock, IPPROTO_IPV6, IPV6_V6ONLY, &opt, sizeof opt);
  int mtud6 = IPV6_PMTUDISC_PROBE;
  setsockopt(sock, IPPROTO_IPV6, IPV6_MTU_DISCOVER, &mtud6, sizeof mtud6);

  struct sockaddr_in6 saddr6 = {0};
  saddr6.sin6_family = AF_INET6;
  saddr6.sin6_port = htons((uint16_t)port);
  if (!host6 || !host6[0] || strcmp(host6, "::") == 0) saddr6.sin6_addr = in6addr_any;
  else inet_pton(AF_INET6, host6, &saddr6.sin6_addr);

  if (bind(sock, (struct sockaddr *)&saddr6, sizeof saddr6) < 0) {
    close(sock);
    return -1;
  }
  h3_udp_gso_probe(sock);
  memset(&t_h3_udp6_local, 0, sizeof t_h3_udp6_local);
  memcpy(&t_h3_udp6_local, &saddr6, sizeof saddr6);
  t_h3_udp6_local_len = sizeof saddr6;
  return sock;
}

#endif /* HAVE_HTTP3 */
