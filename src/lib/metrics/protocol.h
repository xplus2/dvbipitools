/* Copyright 2026 dvbipitools authors. Licensed under GPL-3.0-or-later.
 * See NOTICE and LICENSE for details and authorship information. */

#ifndef DVBIPITOOLS_LIB_METRICS_PROTOCOL_H
#define DVBIPITOOLS_LIB_METRICS_PROTOCOL_H

#include <stddef.h>
#include <stdint.h>

/* one snapshot/datagram, capped at METRICS_MAX_SNAPSHOT_BYTES.
   entry = metric_id(2 BE) + label_len(1) + label + value(8 BE); unknown
   ids skip by length, so appends stay wire-compatible */

#define METRICS_PROTO_VERSION 1
#define METRICS_MAX_SNAPSHOT_BYTES 4096
#define METRICS_ID_MAX 32   /* incl NUL */
#define METRICS_LABEL_MAX 63
#define METRICS_HDR_LEN (4 + METRICS_ID_MAX + 8 + 8 + 8)

/* rendezvous path shared by every exporter and dipimetrics itself */
#define METRICS_DEFAULT_SOCK_PATH "/run/dvbipitools/metrics.sock"

/* wire values: append only, never renumber/reuse */
typedef enum {
  METRICS_COMPONENT_TVHEAD = 1,
  METRICS_COMPONENT_RADIOHEAD = 2,
  METRICS_COMPONENT_SDS = 3,
  METRICS_COMPONENT_BCG = 4,
  METRICS_COMPONENT_RIST = 5,
  METRICS_COMPONENT_REC = 6,
  METRICS_COMPONENT_DESCRAMBLE = 7,
  METRICS_COMPONENT_CAM378 = 8,
  METRICS_COMPONENT_FCCRET = 9
} metrics_component_t;

/* wire values: append only, never renumber/reuse */
typedef enum {
  METRICS_ID_HEADEND_INFO = 1,                    /* label = toolkit version, value = 1 */
  METRICS_ID_METRICS_SNAPSHOTS_DROPPED_TOTAL = 2,
  METRICS_ID_ERRORS_TOTAL = 3,                    /* label = reason */

  METRICS_ID_OUTPUT_PACKETS_TOTAL = 10,
  METRICS_ID_OUTPUT_BYTES_TOTAL = 11,
  METRICS_ID_OUTPUT_ERRORS_TOTAL = 12,
  METRICS_ID_CONFIGURED_SERVICES = 13,
  METRICS_ID_ACTIVE_SERVICES = 14,

  METRICS_ID_INPUT_UP = 20,                       /* label = input */
  METRICS_ID_INPUT_BYTES_TOTAL = 21,              /* label = input */
  METRICS_ID_INPUT_RECONNECTS_TOTAL = 22,         /* label = input */
  METRICS_ID_INPUT_ERRORS_TOTAL = 23,             /* label = input METRICS_LABEL_SEP reason */
  METRICS_ID_INPUT_LAST_DATA_TIME_SECONDS = 24,   /* label = input */

  METRICS_ID_TS_PACKETS_TOTAL = 30,
  METRICS_ID_TS_CONTINUITY_ERRORS_TOTAL = 31,
  METRICS_ID_TS_DISCONTINUITIES_TOTAL = 32,
  METRICS_ID_TS_SYNC_ERRORS_TOTAL = 33,
  METRICS_ID_PCR_DISCONTINUITIES_TOTAL = 34,

  METRICS_ID_PSI_SECTIONS_TOTAL = 40,             /* label = table */
  METRICS_ID_PSI_ERRORS_TOTAL = 41,               /* label = table */

  METRICS_ID_CAS_ECMG_CONNECTED = 50,                  /* label = cas (super_cas_id, hex) */
  METRICS_ID_CAS_EMMG_CLIENTS = 51,                    /* label = cas */
  METRICS_ID_CAS_CRYPTOPERIOD_TRANSITIONS_TOTAL = 52,  /* label = cas */
  METRICS_ID_CAS_ECM_TOTAL = 53,                       /* label = cas */
  METRICS_ID_CAS_ECM_ERRORS_TOTAL = 54,                /* label = cas */
  METRICS_ID_CAS_EMM_TOTAL = 55,                       /* label = cas */
  METRICS_ID_CAS_SCRAMBLED_PACKETS_TOTAL = 56,         /* shared scramble engine, one value across all cas */
  METRICS_ID_CAS_UNEXPECTED_CLEAR_PACKETS_TOTAL = 57,  /* shared scramble engine, one value across all cas */
  METRICS_ID_CAS_EMM_DROPPED_TOTAL = 58,               /* label = cas; oversized or evicted from a full send queue */

  METRICS_ID_RADIO_AUDIO_FRAMES_TOTAL = 60,       /* label = codec */
  METRICS_ID_RADIO_AUDIO_FRAMING_ERRORS_TOTAL = 61,
  METRICS_ID_RADIO_METADATA_UPDATES_TOTAL = 62,
  METRICS_ID_RADIO_METADATA_ERRORS_TOTAL = 63,

  METRICS_ID_TV_SOURCE_PROGRAM_UP = 70,
  METRICS_ID_TV_SOURCE_PMT_UPDATES_TOTAL = 71,
  METRICS_ID_TV_SOURCE_PID_CHANGES_TOTAL = 72,
  METRICS_ID_TV_REMUX_PACKETS_TOTAL = 73,
  METRICS_ID_TV_REMUX_DROPPED_PACKETS_TOTAL = 74,
  METRICS_ID_TV_AIT_SECTIONS_TOTAL = 75,
  METRICS_ID_TV_AIT_ERRORS_TOTAL = 76,
  METRICS_ID_TV_EIT_QUEUE_DROPS_TOTAL = 77,

  METRICS_ID_SDS_SERVICE_PROVIDERS = 80,
  METRICS_ID_SDS_SERVICES = 81,
  METRICS_ID_SDS_DOCUMENTS_GENERATED_TOTAL = 82,
  METRICS_ID_SDS_DOCUMENT_ERRORS_TOTAL = 83,
  METRICS_ID_SDS_ANNOUNCEMENTS_TOTAL = 84,        /* label = transport */
  METRICS_ID_SDS_ANNOUNCEMENT_ERRORS_TOTAL = 85,
  METRICS_ID_SDS_LAST_SUCCESS_TIME_SECONDS = 86,

  METRICS_ID_BCG_SOURCES_CONFIGURED = 90,
  METRICS_ID_BCG_SOURCES_UP = 91,
  METRICS_ID_BCG_SOURCE_ERRORS_TOTAL = 92,        /* label = reason */
  METRICS_ID_BCG_SERVICES = 93,
  METRICS_ID_BCG_SERVICES_WITH_EVENTS = 94,
  METRICS_ID_BCG_EVENTS = 95,
  METRICS_ID_BCG_DOCUMENTS_GENERATED_TOTAL = 96,
  METRICS_ID_BCG_DOCUMENT_ERRORS_TOTAL = 97,
  METRICS_ID_BCG_PUBLICATIONS_TOTAL = 98,
  METRICS_ID_BCG_PUBLICATION_ERRORS_TOTAL = 99,
  METRICS_ID_BCG_LAST_SUCCESS_TIME_SECONDS = 100,
  METRICS_ID_BCG_SCHEDULE_START_TIME_SECONDS = 101,
  METRICS_ID_BCG_SCHEDULE_END_TIME_SECONDS = 102,

  METRICS_ID_RIST_SENDER_SENT_TOTAL = 110,          /* label = peer; sender only */
  METRICS_ID_RIST_SENDER_RETRANSMITTED_TOTAL = 111, /* label = peer; sender only */
  METRICS_ID_RIST_SENDER_RTT_MILLISECONDS = 112,    /* label = peer; sender only */
  METRICS_ID_RIST_RECEIVER_RECEIVED_TOTAL = 120,    /* receiver only, flow aggregate */
  METRICS_ID_RIST_RECEIVER_MISSING_TOTAL = 121,     /* receiver only */
  METRICS_ID_RIST_RECEIVER_RECOVERED_TOTAL = 122,   /* receiver only */
  METRICS_ID_RIST_RECEIVER_LOST_TOTAL = 123,        /* receiver only */
  METRICS_ID_RIST_RECEIVER_RTT_MILLISECONDS = 124,  /* receiver only */
  METRICS_ID_RIST_RECEIVER_BUFFER_MILLISECONDS = 125, /* receiver only */

  METRICS_ID_REC_BYTES_TOTAL = 130,
  METRICS_ID_REC_OUTPUT_UP = 131,                     /* label = output */
  METRICS_ID_REC_OUTPUT_ERRORS_TOTAL = 132,           /* label = output */
  METRICS_ID_REC_ELAPSED_SECONDS = 133,
  METRICS_ID_REC_DURATION_LIMIT_SECONDS = 134,        /* 0 = unlimited */

  METRICS_ID_DESCRAMBLE_MODE = 140,                   /* label = mode, value = 1 */
  METRICS_ID_DESCRAMBLE_KEY_LOAD_ERRORS_TOTAL = 141,
  METRICS_ID_DESCRAMBLE_OUTPUT_ERRORS_TOTAL = 142,

  METRICS_ID_CAM_CONNECTIONS_ACTIVE = 150,
  METRICS_ID_CAM_CONNECTIONS_TOTAL = 151,
  METRICS_ID_CAM_AUTH_ERRORS_TOTAL = 152,             /* label = reason */
  METRICS_ID_CAM_SERVICES_ACTIVE = 153,

  METRICS_ID_FCC_CHANNELS_ACTIVE = 160,
  METRICS_ID_FCC_BURSTS_ACTIVE = 161,
  METRICS_ID_FCC_RET_CLIENTS_ACTIVE = 162,
  METRICS_ID_FCC_BYTES_RETRANSMITTED_TOTAL = 163,
  METRICS_ID_FCC_NACKS_TOTAL = 164,
  METRICS_ID_FCC_CONGESTION_ADAPTATIONS_TOTAL = 165
} metrics_id_t;

/* joins input+reason into METRICS_ID_INPUT_ERRORS_TOTAL's one label field */
#define METRICS_LABEL_SEP '\x1f'

typedef struct {
  uint8_t proto_version;
  metrics_component_t component;
  char metrics_id[METRICS_ID_MAX]; /* NUL-terminated */
  uint64_t process_start_time;     /* unix seconds, restart detection */
  uint64_t sequence;               /* per-process, monotonically increasing */
  uint64_t snapshot_time;          /* unix seconds */
} metrics_hdr_t;

typedef struct {
  unsigned char buf[METRICS_MAX_SNAPSHOT_BYTES];
  size_t len; /* 0 = unusable (begin failed, or a put overflowed) */
} metrics_writer_t;

/* 0 ok, -1 bad proto_version/empty metrics_id */
int metrics_writer_begin(metrics_writer_t *w, const metrics_hdr_t *hdr);
/* label may be NULL (unlabeled series); truncated to METRICS_LABEL_MAX.
   -1 on overflow, writer becomes unusable (finish returns 0 afterward) */
int metrics_writer_put(metrics_writer_t *w, metrics_id_t id, const char *label, uint64_t value);
/* total encoded bytes, ready to send; 0 if begin/put ever failed */
size_t metrics_writer_finish(metrics_writer_t *w);

typedef struct {
  const unsigned char *buf;
  size_t len;
  size_t pos;
} metrics_reader_t;

/* -1 on short/malformed header, unsupported version, unknown component,
   or empty metrics_id */
int metrics_reader_init(metrics_reader_t *r, const unsigned char *buf, size_t len, metrics_hdr_t *hdr);
/* 1 = entry, 0 = clean end, -1 = truncated entry.
   label_out may be NULL to skip the copy (label_cap ignored then) */
int metrics_reader_next(metrics_reader_t *r, metrics_id_t *id, char *label_out, size_t label_cap, uint64_t *value);

#endif
