/* Copyright 2026 dvbipitools authors. Licensed under GPL-3.0-or-later.
 * See NOTICE and LICENSE for details and authorship information. */

#ifndef DIPIXY_CLI_PRIV_H
#define DIPIXY_CLI_PRIV_H

#include "lib/helper/argutil.h"

#include "args.h"
#include "../config.h"
#include "../version.h"

#define OPT_TLS_CERT 1001
#define OPT_TLS_KEY 1002
#define OPT_MAX_CLIENTS 1057
#define OPT_MAX_CHANNELS 1051
#define OPT_IDLE_TIMEOUT 1052
#define OPT_CAPTURE_RING_SIZE 1048
#define OPT_SDS_TIMEOUT 1044
#define OPT_SDS_REFRESH_INTERVAL 1045
#define OPT_SEGMENT_SIZE 1008
#define OPT_SEGMENT_COUNT 1009
#define OPT_HLS_PART_SIZE 1010
#define OPT_DASH_PART_SIZE 1049
#define OPT_DASH_UTC_URL 1050
#define OPT_HLS_SEG_POOL 1039
#define OPT_METRICS 1012
#define OPT_METRICS_ID 1013
#define OPT_METRICS_INTERVAL 1014
#define OPT_METRICS_INSPECT_TS 1068
#define OPT_TS_STARTUP_TIMEOUT 1069
#define OPT_CPU_AFFINITY 1070
#define OPT_METRICS_HTTP 1015
#define OPT_METRICS_AUTH 1056
#define OPT_NO_URL_RTP 1020
#define OPT_NO_URL_UDP 1021
#define OPT_NO_URL_SRT 1026
#define OPT_NO_PID_FILTERS 1022
#define OPT_NO_LCEVC 1055
#define OPT_NO_HTTP2 1040
#define OPT_NO_HTTP3 1041
#define OPT_H3_ALTSVC_PORT 1060
#define OPT_H3_MAX_STREAMS 1061
#define OPT_H3_MAX_CONNS 1062
#define OPT_H3_IDLE_TIMEOUT 1063
#define OPT_H3_RETRY 1064
#define OPT_H3_MAX_UDP_PAYLOAD 1065
#define OPT_H3_WINDOW 1066
#define OPT_H3_CC 1067
#define OPT_NO_FCC 1042
#define OPT_NO_RET 1043
#define OPT_AL_FEC 1053
#define OPT_NO_AL_FEC 1054
#define OPT_NO_STATUS 1023
#define OPT_STATUS_TPL 1027
#define OPT_AUTH 1037
#define OPT_CORS_ORIGIN 1036
#define OPT_SSDP_TTL 1029
#define OPT_SSDP_IFACE 1030
#define OPT_SSDP_INTERVAL 1046
#define OPT_SSDP_MAX_AGE 1047
#define OPT_ENABLE_DLNA 1031
#define OPT_DLNA_HOST 1032
#define OPT_DLNA_NAME 1033
#define OPT_DLNA_KEEP_MULTICAST 1038
#define OPT_MEDIA_TYPE 1034
#define OPT_COLOR 1007
#define OPT_CONFIG_STRICT 1059
#define OPT_CONFIGTEST 1058

#define argerr(...) argutil_err(TOOL_NAME, __VA_ARGS__)

#define OPT_UNHANDLED 4

typedef struct {
  config_t *cfg;
  int cli_input;
} dixy_opt_t;

/* ARGS_OK: handled. OPT_UNHANDLED: not this group */
args_status_t dixy_opt_server(dixy_opt_t *p, int c);
args_status_t dixy_opt_dlna(dixy_opt_t *p, int c);
args_status_t dixy_opt_stream(dixy_opt_t *p, int c);
args_status_t dixy_opt_general(dixy_opt_t *p, int c);
args_status_t dixy_opt_http(dixy_opt_t *p, int c);

void dixy_print_help(void);
args_status_t dixy_cli_check(config_t *cfg);
int dixy_basic_auth_parse(const char *flag, const char *val, char *out, size_t outsz);

#endif
