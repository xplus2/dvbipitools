/* Copyright 2026 dvbipitools authors. Licensed under GPL-3.0-or-later.
 * See NOTICE and LICENSE for details and authorship information. */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "lib/sys/ioutil.h"
#include "render.h"

typedef enum { M_GAUGE, M_COUNTER, M_INFO, M_GAUGE_SIGNED } metric_kind_t;

static const char *metric_kind_name(metric_kind_t k) {
  switch (k) {
    case M_COUNTER: return "counter";
    case M_INFO:    return "info";
    case M_GAUGE:
    case M_GAUGE_SIGNED: return "gauge";
  }
  return "gauge";
}

typedef enum { TS_LABEL_NONE, TS_LABEL_STREAM, TS_LABEL_TABLE, TS_LABEL_PID, TS_LABEL_SERVICE } ts_label_kind_t;

typedef struct {
  metrics_id_t id;
  const char *name;
  metric_kind_t kind;
  const char *help;
  const char *label_name;     /* NULL = no metric-specific label */
  int composite_input_reason; /* label on wire is "input" SEP "reason" */
  ts_label_kind_t ts_label;
} metric_def_t;

/* order matches protocol.h metric_id_t order, also render order */
static const metric_def_t DEFS[] = {
    {.id = METRICS_ID_HEADEND_INFO, .name = "dvbipi_headend_info", .kind = M_INFO, .help = "toolkit build info", .label_name = "version", .composite_input_reason = 0, .ts_label = 0},
    {.id = METRICS_ID_METRICS_SNAPSHOTS_DROPPED_TOTAL, .name = "dvbipi_metrics_snapshots_dropped_total", .kind = M_COUNTER, .help = "snapshots this exporter failed to send", .label_name = NULL, .composite_input_reason = 0, .ts_label = 0},
    {.id = METRICS_ID_METRICS_PARTS_DROPPED_TOTAL, .name = "dvbipi_metrics_parts_dropped_total", .kind = M_COUNTER, .help = "snapshot pieces this exporter failed to send", .label_name = NULL, .composite_input_reason = 0, .ts_label = 0},
    {.id = METRICS_ID_ERRORS_TOTAL, .name = "dvbipi_errors_total", .kind = M_COUNTER, .help = "network errors by reason", .label_name = "reason", .composite_input_reason = 0, .ts_label = 0},
    {.id = METRICS_ID_OUTPUT_PACKETS_TOTAL, .name = "dvbipi_output_packets_total", .kind = M_COUNTER, .help = "packets sent on the output", .label_name = NULL, .composite_input_reason = 0, .ts_label = 0},
    {.id = METRICS_ID_OUTPUT_BYTES_TOTAL, .name = "dvbipi_output_bytes_total", .kind = M_COUNTER, .help = "bytes sent on the output", .label_name = NULL, .composite_input_reason = 0, .ts_label = 0},
    {.id = METRICS_ID_OUTPUT_ERRORS_TOTAL, .name = "dvbipi_output_errors_total", .kind = M_COUNTER, .help = "output send errors", .label_name = NULL, .composite_input_reason = 0, .ts_label = 0},
    {.id = METRICS_ID_CONFIGURED_SERVICES, .name = "dvbipi_configured_services", .kind = M_GAUGE, .help = "services configured", .label_name = NULL, .composite_input_reason = 0, .ts_label = 0},
    {.id = METRICS_ID_ACTIVE_SERVICES, .name = "dvbipi_active_services", .kind = M_GAUGE, .help = "services currently active", .label_name = NULL, .composite_input_reason = 0, .ts_label = 0},
    {.id = METRICS_ID_INPUT_UP, .name = "dvbipi_input_up", .kind = M_GAUGE, .help = "1 if the input is currently connected", .label_name = "input", .composite_input_reason = 0, .ts_label = 0},
    {.id = METRICS_ID_INPUT_BYTES_TOTAL, .name = "dvbipi_input_bytes_total", .kind = M_COUNTER, .help = "bytes read from the input", .label_name = "input", .composite_input_reason = 0, .ts_label = 0},
    {.id = METRICS_ID_INPUT_RECONNECTS_TOTAL, .name = "dvbipi_input_reconnects_total", .kind = M_COUNTER, .help = "input reconnect count", .label_name = "input", .composite_input_reason = 0, .ts_label = 0},
    {.id = METRICS_ID_INPUT_ERRORS_TOTAL, .name = "dvbipi_input_errors_total", .kind = M_COUNTER, .help = "input errors by reason", .label_name = NULL, .composite_input_reason = 1, .ts_label = 0},
    {.id = METRICS_ID_INPUT_LAST_DATA_TIME_SECONDS, .name = "dvbipi_input_last_data_time_seconds", .kind = M_GAUGE, .help = "unix time of the last data received", .label_name = "input", .composite_input_reason = 0, .ts_label = 0},
    {.id = METRICS_ID_TS_PACKETS_TOTAL, .name = "dvbipi_ts_packets_total", .kind = M_COUNTER, .help = "transport stream packets processed", .label_name = NULL, .composite_input_reason = 0, .ts_label = 1},
    {.id = METRICS_ID_TS_CONTINUITY_ERRORS_TOTAL, .name = "dvbipi_ts_continuity_errors_total", .kind = M_COUNTER, .help = "continuity counter errors", .label_name = NULL, .composite_input_reason = 0, .ts_label = 1},
    {.id = METRICS_ID_TS_DISCONTINUITIES_TOTAL, .name = "dvbipi_ts_discontinuities_total", .kind = M_COUNTER, .help = "signaled discontinuities", .label_name = NULL, .composite_input_reason = 0, .ts_label = 1},
    {.id = METRICS_ID_TS_SYNC_ERRORS_TOTAL, .name = "dvbipi_ts_sync_errors_total", .kind = M_COUNTER, .help = "TS sync byte errors", .label_name = NULL, .composite_input_reason = 0, .ts_label = 1},
    {.id = METRICS_ID_PCR_DISCONTINUITIES_TOTAL, .name = "dvbipi_pcr_discontinuities_total", .kind = M_COUNTER, .help = "implausible PCR jumps", .label_name = NULL, .composite_input_reason = 0, .ts_label = 1},
    {.id = METRICS_ID_PSI_SECTIONS_TOTAL, .name = "dvbipi_psi_sections_total", .kind = M_COUNTER, .help = "PSI/SI sections generated by table", .label_name = "table", .composite_input_reason = 0, .ts_label = 0},
    {.id = METRICS_ID_PSI_ERRORS_TOTAL, .name = "dvbipi_psi_errors_total", .kind = M_COUNTER, .help = "PSI/SI section build errors by table", .label_name = "table", .composite_input_reason = 0, .ts_label = 0},
    {.id = METRICS_ID_CAS_ECMG_CONNECTED, .name = "dvbipi_cas_ecmg_connected", .kind = M_GAUGE, .help = "1 if connected to the ECMG", .label_name = "cas", .composite_input_reason = 0, .ts_label = 0},
    {.id = METRICS_ID_CAS_EMMG_CLIENTS, .name = "dvbipi_cas_emmg_clients", .kind = M_GAUGE, .help = "connected EMMG clients", .label_name = "cas", .composite_input_reason = 0, .ts_label = 0},
    {.id = METRICS_ID_CAS_CRYPTOPERIOD_TRANSITIONS_TOTAL, .name = "dvbipi_cas_cryptoperiod_transitions_total", .kind = M_COUNTER, .help = "cryptoperiod transitions", .label_name = "cas", .composite_input_reason = 0, .ts_label = 0},
    {.id = METRICS_ID_CAS_ECM_TOTAL, .name = "dvbipi_cas_ecm_total", .kind = M_COUNTER, .help = "ECMs generated", .label_name = "cas", .composite_input_reason = 0, .ts_label = 0},
    {.id = METRICS_ID_CAS_ECM_ERRORS_TOTAL, .name = "dvbipi_cas_ecm_errors_total", .kind = M_COUNTER, .help = "ECM generation errors", .label_name = "cas", .composite_input_reason = 0, .ts_label = 0},
    {.id = METRICS_ID_CAS_EMM_TOTAL, .name = "dvbipi_cas_emm_total", .kind = M_COUNTER, .help = "EMMs published", .label_name = "cas", .composite_input_reason = 0, .ts_label = 0},
    {.id = METRICS_ID_CAS_EMM_DROPPED_TOTAL, .name = "dvbipi_cas_emm_dropped_total", .kind = M_COUNTER, .help = "EMMs dropped, oversized or send queue full", .label_name = "cas", .composite_input_reason = 0, .ts_label = 0},
    {.id = METRICS_ID_CAS_SCRAMBLED_PACKETS_TOTAL, .name = "dvbipi_cas_scrambled_packets_total", .kind = M_COUNTER, .help = "packets scrambled", .label_name = NULL, .composite_input_reason = 0, .ts_label = 0},
    {.id = METRICS_ID_CAS_UNEXPECTED_CLEAR_PACKETS_TOTAL, .name = "dvbipi_cas_unexpected_clear_packets_total", .kind = M_COUNTER, .help = "clear packets on a managed pid before the ECMG was up", .label_name = NULL, .composite_input_reason = 0, .ts_label = 0},
    {.id = METRICS_ID_RADIO_AUDIO_FRAMES_TOTAL, .name = "dvbipi_radio_audio_frames_total", .kind = M_COUNTER, .help = "audio frames processed by codec", .label_name = "codec", .composite_input_reason = 0, .ts_label = 0},
    {.id = METRICS_ID_RADIO_AUDIO_FRAMING_ERRORS_TOTAL, .name = "dvbipi_radio_audio_framing_errors_total", .kind = M_COUNTER, .help = "audio framing/parse errors", .label_name = NULL, .composite_input_reason = 0, .ts_label = 0},
    {.id = METRICS_ID_RADIO_METADATA_UPDATES_TOTAL, .name = "dvbipi_radio_metadata_updates_total", .kind = M_COUNTER, .help = "metadata updates parsed", .label_name = NULL, .composite_input_reason = 0, .ts_label = 0},
    {.id = METRICS_ID_RADIO_METADATA_ERRORS_TOTAL, .name = "dvbipi_radio_metadata_errors_total", .kind = M_COUNTER, .help = "metadata parse errors", .label_name = NULL, .composite_input_reason = 0, .ts_label = 0},
    {.id = METRICS_ID_TV_SOURCE_PROGRAM_UP, .name = "dvbipi_tv_source_program_up", .kind = M_GAUGE, .help = "1 if the program has at least one active service", .label_name = NULL, .composite_input_reason = 0, .ts_label = 0},
    {.id = METRICS_ID_TV_SOURCE_PMT_UPDATES_TOTAL, .name = "dvbipi_tv_source_pmt_updates_total", .kind = M_COUNTER, .help = "PMT content changes seen", .label_name = NULL, .composite_input_reason = 0, .ts_label = 0},
    {.id = METRICS_ID_TV_SOURCE_PID_CHANGES_TOTAL, .name = "dvbipi_tv_source_pid_changes_total", .kind = M_COUNTER, .help = "elementary stream pid changes seen", .label_name = NULL, .composite_input_reason = 0, .ts_label = 0},
    {.id = METRICS_ID_TV_REMUX_PACKETS_TOTAL, .name = "dvbipi_tv_remux_packets_total", .kind = M_COUNTER, .help = "packets passed through the remux", .label_name = NULL, .composite_input_reason = 0, .ts_label = 0},
    {.id = METRICS_ID_TV_REMUX_DROPPED_PACKETS_TOTAL, .name = "dvbipi_tv_remux_dropped_packets_total", .kind = M_COUNTER, .help = "packets dropped by the remux", .label_name = NULL, .composite_input_reason = 0, .ts_label = 0},
    {.id = METRICS_ID_TV_AIT_SECTIONS_TOTAL, .name = "dvbipi_tv_ait_sections_total", .kind = M_COUNTER, .help = "AIT sections sent", .label_name = NULL, .composite_input_reason = 0, .ts_label = 0},
    {.id = METRICS_ID_TV_AIT_ERRORS_TOTAL, .name = "dvbipi_tv_ait_errors_total", .kind = M_COUNTER, .help = "AIT build errors", .label_name = NULL, .composite_input_reason = 0, .ts_label = 0},
    {.id = METRICS_ID_TV_EIT_QUEUE_DROPS_TOTAL, .name = "dvbipi_tv_eit_queue_drops_total", .kind = M_COUNTER, .help = "EIT sections discarded, reassembly queue full", .label_name = NULL, .composite_input_reason = 0, .ts_label = 0},
    {.id = METRICS_ID_TV_PCR_REWRITTEN_TOTAL, .name = "dvbipi_tv_pcr_rewritten_total", .kind = M_COUNTER, .help = "PCR fields rewritten (--pcr-mode rebase or regenerate)", .label_name = NULL, .composite_input_reason = 0, .ts_label = 0},
    {.id = METRICS_ID_TV_PCR_INJECTED_TOTAL, .name = "dvbipi_tv_pcr_injected_total", .kind = M_COUNTER, .help = "PCR-only packets inserted (--pcr-mode regenerate)", .label_name = NULL, .composite_input_reason = 0, .ts_label = 0},
    {.id = METRICS_ID_SDS_SERVICE_PROVIDERS, .name = "dvbipi_sds_service_providers", .kind = M_GAUGE, .help = "service providers announced", .label_name = NULL, .composite_input_reason = 0, .ts_label = 0},
    {.id = METRICS_ID_SDS_SERVICES, .name = "dvbipi_sds_services", .kind = M_GAUGE, .help = "services announced", .label_name = NULL, .composite_input_reason = 0, .ts_label = 0},
    {.id = METRICS_ID_SDS_DOCUMENTS_GENERATED_TOTAL, .name = "dvbipi_sds_documents_generated_total", .kind = M_COUNTER, .help = "SD&S documents (re)generated", .label_name = NULL, .composite_input_reason = 0, .ts_label = 0},
    {.id = METRICS_ID_SDS_DOCUMENT_ERRORS_TOTAL, .name = "dvbipi_sds_document_errors_total", .kind = M_COUNTER, .help = "SD&S document generation errors", .label_name = NULL, .composite_input_reason = 0, .ts_label = 0},
    {.id = METRICS_ID_SDS_ANNOUNCEMENTS_TOTAL, .name = "dvbipi_sds_announcements_total", .kind = M_COUNTER, .help = "announcement cycles sent by transport", .label_name = "transport", .composite_input_reason = 0, .ts_label = 0},
    {.id = METRICS_ID_SDS_ANNOUNCEMENT_ERRORS_TOTAL, .name = "dvbipi_sds_announcement_errors_total", .kind = M_COUNTER, .help = "announcement send errors", .label_name = NULL, .composite_input_reason = 0, .ts_label = 0},
    {.id = METRICS_ID_SDS_LAST_SUCCESS_TIME_SECONDS, .name = "dvbipi_sds_last_success_time_seconds", .kind = M_GAUGE, .help = "unix time of the last successful announcement", .label_name = NULL, .composite_input_reason = 0, .ts_label = 0},
    {.id = METRICS_ID_BCG_SOURCES_CONFIGURED, .name = "dvbipi_bcg_sources_configured", .kind = M_GAUGE, .help = "guide sources configured", .label_name = NULL, .composite_input_reason = 0, .ts_label = 0},
    {.id = METRICS_ID_BCG_SOURCES_UP, .name = "dvbipi_bcg_sources_up", .kind = M_GAUGE, .help = "guide sources currently loaded", .label_name = NULL, .composite_input_reason = 0, .ts_label = 0},
    {.id = METRICS_ID_BCG_SOURCE_ERRORS_TOTAL, .name = "dvbipi_bcg_source_errors_total", .kind = M_COUNTER, .help = "guide source load/parse errors by reason", .label_name = "reason", .composite_input_reason = 0, .ts_label = 0},
    {.id = METRICS_ID_BCG_SERVICES, .name = "dvbipi_bcg_services", .kind = M_GAUGE, .help = "channels in the loaded guide", .label_name = NULL, .composite_input_reason = 0, .ts_label = 0},
    {.id = METRICS_ID_BCG_SERVICES_WITH_EVENTS, .name = "dvbipi_bcg_services_with_events", .kind = M_GAUGE, .help = "channels with an event in the current window", .label_name = NULL, .composite_input_reason = 0, .ts_label = 0},
    {.id = METRICS_ID_BCG_EVENTS, .name = "dvbipi_bcg_events", .kind = M_GAUGE, .help = "events in the current window", .label_name = NULL, .composite_input_reason = 0, .ts_label = 0},
    {.id = METRICS_ID_BCG_DOCUMENTS_GENERATED_TOTAL, .name = "dvbipi_bcg_documents_generated_total", .kind = M_COUNTER, .help = "BCG documents generated", .label_name = NULL, .composite_input_reason = 0, .ts_label = 0},
    {.id = METRICS_ID_BCG_DOCUMENT_ERRORS_TOTAL, .name = "dvbipi_bcg_document_errors_total", .kind = M_COUNTER, .help = "BCG document generation errors", .label_name = NULL, .composite_input_reason = 0, .ts_label = 0},
    {.id = METRICS_ID_BCG_PUBLICATIONS_TOTAL, .name = "dvbipi_bcg_publications_total", .kind = M_COUNTER, .help = "BCG documents published", .label_name = NULL, .composite_input_reason = 0, .ts_label = 0},
    {.id = METRICS_ID_BCG_PUBLICATION_ERRORS_TOTAL, .name = "dvbipi_bcg_publication_errors_total", .kind = M_COUNTER, .help = "BCG publication send errors", .label_name = NULL, .composite_input_reason = 0, .ts_label = 0},
    {.id = METRICS_ID_BCG_LAST_SUCCESS_TIME_SECONDS, .name = "dvbipi_bcg_last_success_time_seconds", .kind = M_GAUGE, .help = "unix time of the last successful publication", .label_name = NULL, .composite_input_reason = 0, .ts_label = 0},
    {.id = METRICS_ID_BCG_SCHEDULE_START_TIME_SECONDS, .name = "dvbipi_bcg_schedule_start_time_seconds", .kind = M_GAUGE, .help = "earliest event start in the current window", .label_name = NULL, .composite_input_reason = 0, .ts_label = 0},
    {.id = METRICS_ID_BCG_SCHEDULE_END_TIME_SECONDS, .name = "dvbipi_bcg_schedule_end_time_seconds", .kind = M_GAUGE, .help = "latest event end in the current window", .label_name = NULL, .composite_input_reason = 0, .ts_label = 0},
    {.id = METRICS_ID_RIST_SENDER_SENT_TOTAL, .name = "dvbipi_rist_sender_sent_total", .kind = M_COUNTER, .help = "packets sent to this RIST peer", .label_name = "peer", .composite_input_reason = 0, .ts_label = 0},
    {.id = METRICS_ID_RIST_SENDER_RETRANSMITTED_TOTAL, .name = "dvbipi_rist_sender_retransmitted_total", .kind = M_COUNTER, .help = "packets retransmitted to this RIST peer", .label_name = "peer", .composite_input_reason = 0, .ts_label = 0},
    {.id = METRICS_ID_RIST_SENDER_RTT_MILLISECONDS, .name = "dvbipi_rist_sender_rtt_milliseconds", .kind = M_GAUGE, .help = "current RTT to this RIST peer", .label_name = "peer", .composite_input_reason = 0, .ts_label = 0},
    {.id = METRICS_ID_RIST_RECEIVER_RECEIVED_TOTAL, .name = "dvbipi_rist_receiver_received_total", .kind = M_COUNTER, .help = "packets received on the RIST flow", .label_name = NULL, .composite_input_reason = 0, .ts_label = 0},
    {.id = METRICS_ID_RIST_RECEIVER_MISSING_TOTAL, .name = "dvbipi_rist_receiver_missing_total", .kind = M_COUNTER, .help = "packets missing on the RIST flow, incl reordered", .label_name = NULL, .composite_input_reason = 0, .ts_label = 0},
    {.id = METRICS_ID_RIST_RECEIVER_RECOVERED_TOTAL, .name = "dvbipi_rist_receiver_recovered_total", .kind = M_COUNTER, .help = "packets recovered via retransmit", .label_name = NULL, .composite_input_reason = 0, .ts_label = 0},
    {.id = METRICS_ID_RIST_RECEIVER_LOST_TOTAL, .name = "dvbipi_rist_receiver_lost_total", .kind = M_COUNTER, .help = "packets never recovered", .label_name = NULL, .composite_input_reason = 0, .ts_label = 0},
    {.id = METRICS_ID_RIST_RECEIVER_RTT_MILLISECONDS, .name = "dvbipi_rist_receiver_rtt_milliseconds", .kind = M_GAUGE, .help = "average RTT across the flow's peers", .label_name = NULL, .composite_input_reason = 0, .ts_label = 0},
    {.id = METRICS_ID_RIST_RECEIVER_BUFFER_MILLISECONDS, .name = "dvbipi_rist_receiver_buffer_milliseconds", .kind = M_GAUGE, .help = "current recovery buffer fill", .label_name = NULL, .composite_input_reason = 0, .ts_label = 0},
    {.id = METRICS_ID_SRT_SENDER_SENT_TOTAL, .name = "dvbipi_srt_sender_sent_total", .kind = M_COUNTER, .help = "packets sent to this SRT peer, incl retransmissions", .label_name = "peer", .composite_input_reason = 0, .ts_label = 0},
    {.id = METRICS_ID_SRT_SENDER_RETRANSMITTED_TOTAL, .name = "dvbipi_srt_sender_retransmitted_total", .kind = M_COUNTER, .help = "packets retransmitted to this SRT peer", .label_name = "peer", .composite_input_reason = 0, .ts_label = 0},
    {.id = METRICS_ID_SRT_SENDER_RTT_MILLISECONDS, .name = "dvbipi_srt_sender_rtt_milliseconds", .kind = M_GAUGE, .help = "current RTT to this SRT peer", .label_name = "peer", .composite_input_reason = 0, .ts_label = 0},
    {.id = METRICS_ID_SRT_SENDER_LOST_TOTAL, .name = "dvbipi_srt_sender_lost_total", .kind = M_COUNTER, .help = "packets the peer reported lost", .label_name = "peer", .composite_input_reason = 0, .ts_label = 0},
    {.id = METRICS_ID_SRT_SENDER_DROPPED_TOTAL, .name = "dvbipi_srt_sender_dropped_total", .kind = M_COUNTER, .help = "packets dropped, too late to send", .label_name = "peer", .composite_input_reason = 0, .ts_label = 0},
    {.id = METRICS_ID_SRT_RECEIVER_RECEIVED_TOTAL, .name = "dvbipi_srt_receiver_received_total", .kind = M_COUNTER, .help = "packets received on the SRT flow", .label_name = NULL, .composite_input_reason = 0, .ts_label = 0},
    {.id = METRICS_ID_SRT_RECEIVER_LOST_TOTAL, .name = "dvbipi_srt_receiver_lost_total", .kind = M_COUNTER, .help = "packets lost, incl later recovered", .label_name = NULL, .composite_input_reason = 0, .ts_label = 0},
    {.id = METRICS_ID_SRT_RECEIVER_DROPPED_TOTAL, .name = "dvbipi_srt_receiver_dropped_total", .kind = M_COUNTER, .help = "packets dropped, too late to play", .label_name = NULL, .composite_input_reason = 0, .ts_label = 0},
    {.id = METRICS_ID_SRT_RECEIVER_RTT_MILLISECONDS, .name = "dvbipi_srt_receiver_rtt_milliseconds", .kind = M_GAUGE, .help = "current RTT on the SRT flow", .label_name = NULL, .composite_input_reason = 0, .ts_label = 0},
    {.id = METRICS_ID_SRT_RECEIVER_BUFFER_MILLISECONDS, .name = "dvbipi_srt_receiver_buffer_milliseconds", .kind = M_GAUGE, .help = "receive buffer delay (TSBPD)", .label_name = NULL, .composite_input_reason = 0, .ts_label = 0},
    {.id = METRICS_ID_REC_BYTES_TOTAL, .name = "dvbipi_rec_bytes_total", .kind = M_COUNTER, .help = "bytes written across all outputs", .label_name = NULL, .composite_input_reason = 0, .ts_label = 0},
    {.id = METRICS_ID_REC_OUTPUT_UP, .name = "dvbipi_rec_output_up", .kind = M_GAUGE, .help = "1 if this output's last write succeeded", .label_name = "output", .composite_input_reason = 0, .ts_label = 0},
    {.id = METRICS_ID_REC_OUTPUT_ERRORS_TOTAL, .name = "dvbipi_rec_output_errors_total", .kind = M_COUNTER, .help = "output write failures", .label_name = "output", .composite_input_reason = 0, .ts_label = 0},
    {.id = METRICS_ID_REC_ELAPSED_SECONDS, .name = "dvbipi_rec_elapsed_seconds", .kind = M_GAUGE, .help = "seconds since recording started", .label_name = NULL, .composite_input_reason = 0, .ts_label = 0},
    {.id = METRICS_ID_REC_DURATION_LIMIT_SECONDS, .name = "dvbipi_rec_duration_limit_seconds", .kind = M_GAUGE, .help = "configured recording duration, 0 if unlimited", .label_name = NULL, .composite_input_reason = 0, .ts_label = 0},
    {.id = METRICS_ID_DESCRAMBLE_MODE, .name = "dvbipi_descramble_mode", .kind = M_INFO, .help = "detected CAS scheme", .label_name = "mode", .composite_input_reason = 0, .ts_label = 0},
    {.id = METRICS_ID_DESCRAMBLE_KEY_LOAD_ERRORS_TOTAL, .name = "dvbipi_descramble_key_load_errors_total", .kind = M_COUNTER, .help = "RSA/device key load failures", .label_name = NULL, .composite_input_reason = 0, .ts_label = 0},
    {.id = METRICS_ID_DESCRAMBLE_OUTPUT_ERRORS_TOTAL, .name = "dvbipi_descramble_output_errors_total", .kind = M_COUNTER, .help = "output emit failures", .label_name = NULL, .composite_input_reason = 0, .ts_label = 0},
    {.id = METRICS_ID_CAM_CONNECTIONS_ACTIVE, .name = "dvbipi_cam_connections_active", .kind = M_GAUGE, .help = "connected cs378x clients", .label_name = NULL, .composite_input_reason = 0, .ts_label = 0},
    {.id = METRICS_ID_CAM_CONNECTIONS_TOTAL, .name = "dvbipi_cam_connections_total", .kind = M_COUNTER, .help = "cs378x client connections accepted", .label_name = NULL, .composite_input_reason = 0, .ts_label = 0},
    {.id = METRICS_ID_CAM_AUTH_ERRORS_TOTAL, .name = "dvbipi_cam_auth_errors_total", .kind = M_COUNTER, .help = "cs378x auth/protocol errors by reason", .label_name = "reason", .composite_input_reason = 0, .ts_label = 0},
    {.id = METRICS_ID_CAM_SERVICES_ACTIVE, .name = "dvbipi_cam_services_active", .kind = M_GAUGE, .help = "services with a live session key", .label_name = NULL, .composite_input_reason = 0, .ts_label = 0},
    {.id = METRICS_ID_FCC_CHANNELS_ACTIVE, .name = "dvbipi_fcc_channels_active", .kind = M_GAUGE, .help = "channels currently tracked", .label_name = NULL, .composite_input_reason = 0, .ts_label = 0},
    {.id = METRICS_ID_FCC_BURSTS_ACTIVE, .name = "dvbipi_fcc_bursts_active", .kind = M_GAUGE, .help = "concurrent FCC burst sessions", .label_name = NULL, .composite_input_reason = 0, .ts_label = 0},
    {.id = METRICS_ID_FCC_RET_CLIENTS_ACTIVE, .name = "dvbipi_fcc_ret_clients_active", .kind = M_GAUGE, .help = "active unicast RET sessions", .label_name = NULL, .composite_input_reason = 0, .ts_label = 0},
    {.id = METRICS_ID_FCC_BYTES_RETRANSMITTED_TOTAL, .name = "dvbipi_fcc_bytes_retransmitted_total", .kind = M_COUNTER, .help = "bytes retransmitted via FCC bursts", .label_name = NULL, .composite_input_reason = 0, .ts_label = 0},
    {.id = METRICS_ID_FCC_NACKS_TOTAL, .name = "dvbipi_fcc_nacks_total", .kind = M_COUNTER, .help = "NACKs handled", .label_name = NULL, .composite_input_reason = 0, .ts_label = 0},
    {.id = METRICS_ID_FCC_CONGESTION_ADAPTATIONS_TOTAL, .name = "dvbipi_fcc_congestion_adaptations_total", .kind = M_COUNTER, .help = "burst rate reduced due to congestion", .label_name = NULL, .composite_input_reason = 0, .ts_label = 0},
    {.id = METRICS_ID_XY_CONNECTIONS_TOTAL, .name = "dvbipi_xy_connections_total", .kind = M_COUNTER, .help = "connections accepted, every protocol", .label_name = NULL, .composite_input_reason = 0, .ts_label = 0},
    {.id = METRICS_ID_XY_CONNECTIONS_ACTIVE, .name = "dvbipi_xy_connections_active", .kind = M_GAUGE, .help = "connections currently open", .label_name = NULL, .composite_input_reason = 0, .ts_label = 0},
    {.id = METRICS_ID_XY_REQUESTS_TOTAL, .name = "dvbipi_xy_requests_total", .kind = M_COUNTER, .help = "HTTP requests dispatched", .label_name = NULL, .composite_input_reason = 0, .ts_label = 0},
    {.id = METRICS_ID_XY_HTTP_ERRORS_TOTAL, .name = "dvbipi_xy_http_errors_total", .kind = M_COUNTER, .help = "HTTP responses with a 4xx/5xx status", .label_name = NULL, .composite_input_reason = 0, .ts_label = 0},
    {.id = METRICS_ID_XY_BYTES_SERVED_TOTAL, .name = "dvbipi_xy_bytes_served_total", .kind = M_COUNTER, .help = "wire bytes queued to clients, headers and body", .label_name = NULL, .composite_input_reason = 0, .ts_label = 0},
    {.id = METRICS_ID_XY_SOURCES_ACTIVE, .name = "dvbipi_xy_sources_active", .kind = M_GAUGE, .help = "distinct multicast joins currently open", .label_name = NULL, .composite_input_reason = 0, .ts_label = 0},
    {.id = METRICS_ID_XY_TSPUSH_SUBS_ACTIVE, .name = "dvbipi_xy_tspush_subscribers_active", .kind = M_GAUGE, .help = "raw TS push clients currently attached", .label_name = NULL, .composite_input_reason = 0, .ts_label = 0},
    {.id = METRICS_ID_TS_BYTES_TOTAL, .name = "dvbipi_ts_bytes_total", .kind = M_COUNTER, .help = "transport stream bytes", .label_name = NULL, .composite_input_reason = 0, .ts_label = 1},
    {.id = METRICS_ID_TS_NULL_PACKETS_TOTAL, .name = "dvbipi_ts_null_packets_total", .kind = M_COUNTER, .help = "null packets", .label_name = NULL, .composite_input_reason = 0, .ts_label = 1},
    {.id = METRICS_ID_TS_SCRAMBLED_PACKETS_TOTAL, .name = "dvbipi_ts_scrambled_packets_total", .kind = M_COUNTER, .help = "scrambled packets", .label_name = NULL, .composite_input_reason = 0, .ts_label = 1},
    {.id = METRICS_ID_TS_CLEAR_PACKETS_TOTAL, .name = "dvbipi_ts_clear_packets_total", .kind = M_COUNTER, .help = "unscrambled non-null packets", .label_name = NULL, .composite_input_reason = 0, .ts_label = 1},
    {.id = METRICS_ID_TS_SYNC_LOSS_TOTAL, .name = "dvbipi_ts_sync_loss_total", .kind = M_COUNTER, .help = "TS sync losses", .label_name = NULL, .composite_input_reason = 0, .ts_label = 1},
    {.id = METRICS_ID_TS_TRANSPORT_ERROR_PACKETS_TOTAL, .name = "dvbipi_ts_transport_error_packets_total", .kind = M_COUNTER, .help = "packets with the transport error indicator set", .label_name = NULL, .composite_input_reason = 0, .ts_label = 1},
    {.id = METRICS_ID_TS_DUPLICATE_PACKETS_TOTAL, .name = "dvbipi_ts_duplicate_packets_total", .kind = M_COUNTER, .help = "allowed duplicate packets", .label_name = NULL, .composite_input_reason = 0, .ts_label = 1},
    {.id = METRICS_ID_TS_PAT_ERRORS_TOTAL, .name = "dvbipi_ts_pat_errors_total", .kind = M_COUNTER, .help = "PAT missing or repeated too late", .label_name = NULL, .composite_input_reason = 0, .ts_label = 1},
    {.id = METRICS_ID_TS_PMT_ERRORS_TOTAL, .name = "dvbipi_ts_pmt_errors_total", .kind = M_COUNTER, .help = "PMT missing or repeated too late", .label_name = NULL, .composite_input_reason = 0, .ts_label = 1},
    {.id = METRICS_ID_TS_CAT_ERRORS_TOTAL, .name = "dvbipi_ts_cat_errors_total", .kind = M_COUNTER, .help = "scrambled packets without a CAT", .label_name = NULL, .composite_input_reason = 0, .ts_label = 1},
    {.id = METRICS_ID_TS_SDT_ERRORS_TOTAL, .name = "dvbipi_ts_sdt_errors_total", .kind = M_COUNTER, .help = "SDT actual missing or repeated too late", .label_name = NULL, .composite_input_reason = 0, .ts_label = 1},
    {.id = METRICS_ID_TS_NIT_ERRORS_TOTAL, .name = "dvbipi_ts_nit_errors_total", .kind = M_COUNTER, .help = "NIT actual missing or repeated too late", .label_name = NULL, .composite_input_reason = 0, .ts_label = 1},
    {.id = METRICS_ID_TS_SDT_OTHER_ERRORS_TOTAL, .name = "dvbipi_ts_sdt_other_errors_total", .kind = M_COUNTER, .help = "SDT other repeated too late", .label_name = NULL, .composite_input_reason = 0, .ts_label = 1},
    {.id = METRICS_ID_TS_NIT_OTHER_ERRORS_TOTAL, .name = "dvbipi_ts_nit_other_errors_total", .kind = M_COUNTER, .help = "NIT other repeated too late", .label_name = NULL, .composite_input_reason = 0, .ts_label = 1},
    {.id = METRICS_ID_TS_EIT_ERRORS_TOTAL, .name = "dvbipi_ts_eit_errors_total", .kind = M_COUNTER, .help = "EIT present/following actual missing or repeated too late", .label_name = NULL, .composite_input_reason = 0, .ts_label = 1},
    {.id = METRICS_ID_TS_EIT_OTHER_ERRORS_TOTAL, .name = "dvbipi_ts_eit_other_errors_total", .kind = M_COUNTER, .help = "EIT other repeated too late", .label_name = NULL, .composite_input_reason = 0, .ts_label = 1},
    {.id = METRICS_ID_TS_RST_ERRORS_TOTAL, .name = "dvbipi_ts_rst_errors_total", .kind = M_COUNTER, .help = "sections on the RST pid with another table_id", .label_name = NULL, .composite_input_reason = 0, .ts_label = 1},
    {.id = METRICS_ID_TS_TDT_ERRORS_TOTAL, .name = "dvbipi_ts_tdt_errors_total", .kind = M_COUNTER, .help = "TDT/TOT missing or wrong table_id on its pid", .label_name = NULL, .composite_input_reason = 0, .ts_label = 1},
    {.id = METRICS_ID_TS_REFERENCED_PID_MISSING_TOTAL, .name = "dvbipi_ts_referenced_pid_missing_total", .kind = M_COUNTER, .help = "PMT-referenced pids absent for 5 s", .label_name = NULL, .composite_input_reason = 0, .ts_label = 1},
    {.id = METRICS_ID_TS_PCR_REPETITION_ERRORS_TOTAL, .name = "dvbipi_ts_pcr_repetition_errors_total", .kind = M_COUNTER, .help = "PCR intervals above 100 ms", .label_name = NULL, .composite_input_reason = 0, .ts_label = 1},
    {.id = METRICS_ID_TS_PTS_ERRORS_TOTAL, .name = "dvbipi_ts_pts_errors_total", .kind = M_COUNTER, .help = "video or audio pids without PTS for 700 ms", .label_name = NULL, .composite_input_reason = 0, .ts_label = 1},
    {.id = METRICS_ID_TS_PID_ADDED_TOTAL, .name = "dvbipi_ts_pid_added_total", .kind = M_COUNTER, .help = "pids added to the PMT", .label_name = NULL, .composite_input_reason = 0, .ts_label = 1},
    {.id = METRICS_ID_TS_PID_REMOVED_TOTAL, .name = "dvbipi_ts_pid_removed_total", .kind = M_COUNTER, .help = "pids removed from the PMT", .label_name = NULL, .composite_input_reason = 0, .ts_label = 1},
    {.id = METRICS_ID_TS_INPUT_STALLS_TOTAL, .name = "dvbipi_ts_input_stalls_total", .kind = M_COUNTER, .help = "times no packet arrived for over 1 s", .label_name = NULL, .composite_input_reason = 0, .ts_label = 1},
    {.id = METRICS_ID_TS_INPUT_STALL_MILLISECONDS_TOTAL, .name = "dvbipi_ts_input_stall_milliseconds_total", .kind = M_COUNTER, .help = "milliseconds spent stalled", .label_name = NULL, .composite_input_reason = 0, .ts_label = 1},
    {.id = METRICS_ID_TS_MAX_INTERPACKET_GAP_MILLISECONDS, .name = "dvbipi_ts_max_interpacket_gap_milliseconds", .kind = M_GAUGE, .help = "longest gap between packets, last 30 to 60 s", .label_name = NULL, .composite_input_reason = 0, .ts_label = 1},
    {.id = METRICS_ID_TS_PCR_MAX_INTERVAL_MILLISECONDS, .name = "dvbipi_ts_pcr_max_interval_milliseconds", .kind = M_GAUGE, .help = "longest PCR interval, last 30 to 60 s", .label_name = NULL, .composite_input_reason = 0, .ts_label = 1},
    {.id = METRICS_ID_TS_LAST_PACKET_TIMESTAMP_SECONDS, .name = "dvbipi_ts_last_packet_timestamp_seconds", .kind = M_GAUGE, .help = "unix time of the last packet", .label_name = NULL, .composite_input_reason = 0, .ts_label = 1},
    {.id = METRICS_ID_TS_TABLE_LAST_SEEN_TIMESTAMP_SECONDS, .name = "dvbipi_ts_table_last_seen_timestamp_seconds", .kind = M_GAUGE, .help = "unix time the table was last seen", .label_name = NULL, .composite_input_reason = 0, .ts_label = 2},
    {.id = METRICS_ID_TS_TABLE_CRC_ERRORS_TOTAL, .name = "dvbipi_ts_table_crc_errors_total", .kind = M_COUNTER, .help = "CRC failures by table", .label_name = NULL, .composite_input_reason = 0, .ts_label = 2},
    {.id = METRICS_ID_TS_TABLE_VERSION_CHANGES_TOTAL, .name = "dvbipi_ts_table_version_changes_total", .kind = M_COUNTER, .help = "version changes by table", .label_name = NULL, .composite_input_reason = 0, .ts_label = 2},
    {.id = METRICS_ID_TS_SI_CRC_ERRORS_TOTAL, .name = "dvbipi_ts_si_crc_errors_total", .kind = M_COUNTER, .help = "EIT and TOT CRC failures", .label_name = NULL, .composite_input_reason = 0, .ts_label = 1},
    {.id = METRICS_ID_TS_UNREFERENCED_PIDS_TOTAL, .name = "dvbipi_ts_unreferenced_pids_total", .kind = M_COUNTER, .help = "pids carrying packets that the PMT does not reference", .label_name = NULL, .composite_input_reason = 0, .ts_label = 1},
    {.id = METRICS_ID_TS_TRANSPORT_STREAM_ID, .name = "dvbipi_ts_transport_stream_id", .kind = M_GAUGE, .help = "transport_stream_id from the PAT", .label_name = NULL, .composite_input_reason = 0, .ts_label = 1},
    {.id = METRICS_ID_TS_PCR_JITTER_MAX_MICROSECONDS, .name = "dvbipi_ts_pcr_jitter_max_microseconds", .kind = M_GAUGE, .help = "largest PCR arrival versus PCR value deviation, last 30 to 60 s", .label_name = NULL, .composite_input_reason = 0, .ts_label = 1},
    {.id = METRICS_ID_SRT_SENDER_QUEUE_CHUNKS, .name = "dvbipi_srt_sender_queue_chunks", .kind = M_GAUGE, .help = "chunks waiting in the send queue", .label_name = "peer", .composite_input_reason = 0, .ts_label = 0},
    {.id = METRICS_ID_SRT_SENDER_QUEUE_CAPACITY_CHUNKS, .name = "dvbipi_srt_sender_queue_capacity_chunks", .kind = M_GAUGE, .help = "send queue capacity in chunks", .label_name = "peer", .composite_input_reason = 0, .ts_label = 0},
    {.id = METRICS_ID_SRT_SENDER_QUEUE_HIGH_WATERMARK_CHUNKS, .name = "dvbipi_srt_sender_queue_high_watermark_chunks", .kind = M_GAUGE, .help = "most chunks ever queued", .label_name = "peer", .composite_input_reason = 0, .ts_label = 0},
    {.id = METRICS_ID_SRT_SENDER_QUEUE_DROPPED_CHUNKS_TOTAL, .name = "dvbipi_srt_sender_queue_dropped_chunks_total", .kind = M_COUNTER, .help = "chunks dropped, send queue full", .label_name = "peer", .composite_input_reason = 0, .ts_label = 0},
    {.id = METRICS_ID_TS_PCR_ACCURACY_ERRORS_TOTAL, .name = "dvbipi_ts_pcr_accuracy_errors_total", .kind = M_COUNTER, .help = "PCR pairs whose arrival spacing deviates from their PCR spacing by over 500 ns", .label_name = NULL, .composite_input_reason = 0, .ts_label = 1},
    {.id = METRICS_ID_TS_PID_PACKETS_TOTAL, .name = "dvbipi_ts_pid_packets_total", .kind = M_COUNTER, .help = "packets on an operator listed pid", .label_name = NULL, .composite_input_reason = 0, .ts_label = 3},
    {.id = METRICS_ID_TS_PID_SCRAMBLED_PACKETS_TOTAL, .name = "dvbipi_ts_pid_scrambled_packets_total", .kind = M_COUNTER, .help = "scrambled packets on an operator listed pid", .label_name = NULL, .composite_input_reason = 0, .ts_label = 3},
    {.id = METRICS_ID_TS_SERVICE_PACKETS_TOTAL, .name = "dvbipi_ts_service_packets_total", .kind = M_COUNTER, .help = "packets on the pids of a service", .label_name = NULL, .composite_input_reason = 0, .ts_label = 4},
    {.id = METRICS_ID_TS_SERVICE_SCRAMBLED_PACKETS_TOTAL, .name = "dvbipi_ts_service_scrambled_packets_total", .kind = M_COUNTER, .help = "scrambled packets on the pids of a service", .label_name = NULL, .composite_input_reason = 0, .ts_label = 4},
    {.id = METRICS_ID_TS_UNREFERENCED_UNLISTED_PIDS_TOTAL, .name = "dvbipi_ts_unreferenced_unlisted_pids_total", .kind = M_COUNTER, .help = "unreferenced pids not on the operator's known list", .label_name = NULL, .composite_input_reason = 0, .ts_label = 1},
    {.id = METRICS_ID_TS_BITRATE_MGB1_BITS_PER_SECOND, .name = "dvbipi_ts_bitrate_mgb1_bits_per_second", .kind = M_GAUGE, .help = "TS bitrate, MGB1 profile (1 s slice, 1 s gate, 188 byte packets)", .label_name = NULL, .composite_input_reason = 0, .ts_label = 1},
    {.id = METRICS_ID_TS_BITRATE_MGB2_BITS_PER_SECOND, .name = "dvbipi_ts_bitrate_mgb2_bits_per_second", .kind = M_GAUGE, .help = "TS bitrate, MGB2 profile (100 ms slice, 1 s gate, 188 byte packets)", .label_name = NULL, .composite_input_reason = 0, .ts_label = 1},
    {.id = METRICS_ID_XY_TSPUSH_QUEUE_BYTES, .name = "dvbipi_xy_tspush_queue_bytes", .kind = M_GAUGE, .help = "bytes waiting in the raw TS push client queues", .label_name = NULL, .composite_input_reason = 0, .ts_label = 0},
    {.id = METRICS_ID_XY_TSPUSH_QUEUE_MAX_BYTES, .name = "dvbipi_xy_tspush_queue_max_bytes", .kind = M_GAUGE, .help = "bytes waiting in the fullest raw TS push client queue", .label_name = NULL, .composite_input_reason = 0, .ts_label = 0},
    {.id = METRICS_ID_XY_TSPUSH_QUEUE_HIGH_WATERMARK_BYTES, .name = "dvbipi_xy_tspush_queue_high_watermark_bytes", .kind = M_GAUGE, .help = "most bytes ever waiting in one raw TS push client queue", .label_name = NULL, .composite_input_reason = 0, .ts_label = 0},
    {.id = METRICS_ID_XY_TSPUSH_QUEUE_DROPPED_TOTAL, .name = "dvbipi_xy_tspush_queue_dropped_total", .kind = M_COUNTER, .help = "writes dropped, raw TS push client queue full", .label_name = NULL, .composite_input_reason = 0, .ts_label = 0},
    {.id = METRICS_ID_TS_PCR_FREQ_OFFSET_PPB, .name = "dvbipi_ts_pcr_freq_offset_ppb", .kind = M_GAUGE_SIGNED, .help = "PCR clock frequency offset against local arrival clock in parts per billion, 30 s window", .label_name = NULL, .composite_input_reason = 0, .ts_label = 1},
    {.id = METRICS_ID_TS_PCR_DRIFT_RATE_PPB_PER_SECOND, .name = "dvbipi_ts_pcr_drift_rate_ppb_per_second", .kind = M_GAUGE_SIGNED, .help = "change of the PCR frequency offset per second, 30 s window", .label_name = NULL, .composite_input_reason = 0, .ts_label = 1},
    {.id = METRICS_ID_TS_PCR_FREQ_OFFSET_ERRORS_TOTAL, .name = "dvbipi_ts_pcr_freq_offset_errors_total", .kind = M_COUNTER, .help = "30 s windows with PCR frequency offset beyond 30 ppm", .label_name = NULL, .composite_input_reason = 0, .ts_label = 1},
    {.id = METRICS_ID_TS_DTS_PCR_LEAD_MICROSECONDS, .name = "dvbipi_ts_dts_pcr_lead_microseconds", .kind = M_GAUGE_SIGNED, .help = "last DTS (PTS if none) minus extrapolated PCR clock at PES start, per elementary stream", .label_name = NULL, .composite_input_reason = 0, .ts_label = 3},
    {.id = METRICS_ID_TS_DTS_PCR_LEAD_UNDERRUNS_TOTAL, .name = "dvbipi_ts_dts_pcr_lead_underruns_total", .kind = M_COUNTER, .help = "PES whose DTS (PTS if none) was already behind the extrapolated PCR clock at arrival", .label_name = NULL, .composite_input_reason = 0, .ts_label = 3},
    {.id = METRICS_ID_TS_INPUT_BUFFER_MILLISECONDS, .name = "dvbipi_ts_input_buffer_milliseconds", .kind = M_GAUGE, .help = "age of the oldest datagram held in the input de-jitter buffer", .label_name = NULL, .composite_input_reason = 0, .ts_label = 1},
    {.id = METRICS_ID_TV_PES_RETIMED_TOTAL, .name = "dvbipi_tv_pes_retimed_total", .kind = M_COUNTER, .help = "PES headers whose PTS/DTS were shifted", .label_name = NULL, .composite_input_reason = 0, .ts_label = 0},
    {.id = METRICS_ID_TV_PES_RETIME_SKIPPED_TOTAL, .name = "dvbipi_tv_pes_retime_skipped_total", .kind = M_COUNTER, .help = "PES headers left unshifted, split across packets or scrambled at the source", .label_name = NULL, .composite_input_reason = 0, .ts_label = 0},
    {.id = METRICS_ID_TV_RETIME_RELATCHES_TOTAL, .name = "dvbipi_tv_retime_relatches_total", .kind = M_COUNTER, .help = "time offset re-latches after a source timestamp or PCR jump", .label_name = NULL, .composite_input_reason = 0, .ts_label = 0},
    {.id = METRICS_ID_TV_HOLD_FORCED_RELEASES_TOTAL, .name = "dvbipi_tv_hold_forced_releases_total", .kind = M_COUNTER, .help = "packets released early because a hold-back queue was full", .label_name = NULL, .composite_input_reason = 0, .ts_label = 0},
    {.id = METRICS_ID_TV_SCTE35_ADJUSTED_TOTAL, .name = "dvbipi_tv_scte35_adjusted_total", .kind = M_COUNTER, .help = "SCTE-35 sections whose pts_adjustment was shifted", .label_name = NULL, .composite_input_reason = 0, .ts_label = 0},
    {.id = METRICS_ID_TV_RELEASE_LEAD_MIN_MICROSECONDS, .name = "dvbipi_tv_release_lead_min_microseconds", .kind = M_GAUGE_SIGNED, .help = "smallest DTS (PTS if none) minus output PCR when a packet was released", .label_name = NULL, .composite_input_reason = 0, .ts_label = 0},
    {.id = METRICS_ID_TV_RELEASE_LEAD_MAX_MICROSECONDS, .name = "dvbipi_tv_release_lead_max_microseconds", .kind = M_GAUGE_SIGNED, .help = "largest DTS (PTS if none) minus output PCR when a packet was released", .label_name = NULL, .composite_input_reason = 0, .ts_label = 0},
    {.id = METRICS_ID_SRT_SENDER_QUEUE_MILLISECONDS, .name = "dvbipi_srt_sender_queue_milliseconds", .kind = M_GAUGE, .help = "send queue fill as time at the measured stream bitrate", .label_name = "peer", .composite_input_reason = 0, .ts_label = 0},
    {.id = METRICS_ID_XY_TSPUSH_QUEUE_MILLISECONDS, .name = "dvbipi_xy_tspush_queue_milliseconds", .kind = M_GAUGE, .help = "fullest raw TS push client queue as time at the measured stream bitrate", .label_name = NULL, .composite_input_reason = 0, .ts_label = 0},
    {.id = METRICS_ID_TS_UNREFERENCED_PACKETS_TOTAL, .name = "dvbipi_ts_unreferenced_packets_total", .kind = M_COUNTER, .help = "packets on pids the PMT does not reference", .label_name = NULL, .composite_input_reason = 0, .ts_label = 1},
};
#define N_DEFS (sizeof DEFS / sizeof DEFS[0])

/* backslash/quote/newline escaping per OpenMetrics/Prometheus text label-value grammar */
static void escape_label(const char *in, char *out, size_t out_cap) {
  size_t oi = 0;
  for (; *in && oi + 2 < out_cap; in++) {
    if (*in == '\\' || *in == '"') {
      out[oi++] = '\\';
      out[oi++] = *in;
    } else if (*in == '\n') {
      out[oi++] = '\\';
      out[oi++] = 'n';
    } else {
      out[oi++] = *in;
    }
  }
  out[oi] = '\0';
}

/* label named headend_id, not instance: Prometheus already assigns instance
   per scrape target, colliding names get renamed exported_instance */
static void append_base_labels(dstrbuf_t *sb, const store_slot_t *slot) {
  char esc_headend_id[2 * METRICS_ID_MAX + 2];
  escape_label(slot->metrics_id, esc_headend_id, sizeof esc_headend_id);
  dstrbuf_add(sb, "component=\"");
  dstrbuf_add(sb, metrics_component_name(slot->component));
  dstrbuf_add(sb, "\",headend_id=\"");
  dstrbuf_add(sb, esc_headend_id);
  dstrbuf_add(sb, "\"");
}

static void append_composite_input_reason(dstrbuf_t *sb, const char *label) {
  char input_part[METRICS_LABEL_MAX + 1], reason_part[METRICS_LABEL_MAX + 1];
  char esc_input[2 * METRICS_LABEL_MAX + 2], esc_reason[2 * METRICS_LABEL_MAX + 2];
  const char *sep = strchr(label, METRICS_LABEL_SEP);
  if (sep) {
    size_t ilen = (size_t)(sep - label);
    if (ilen >= sizeof input_part)
      ilen = sizeof input_part - 1;
    memcpy(input_part, label, ilen);
    input_part[ilen] = '\0';
    bufcpy(reason_part, sizeof reason_part, sep + 1);
  } else {
    bufcpy(input_part, sizeof input_part, label);
    reason_part[0] = '\0';
  }
  escape_label(input_part, esc_input, sizeof esc_input);
  escape_label(reason_part, esc_reason, sizeof esc_reason);
  dstrbuf_add(sb, ",input=\"");
  dstrbuf_add(sb, esc_input);
  dstrbuf_add(sb, "\",reason=\"");
  dstrbuf_add(sb, esc_reason);
  dstrbuf_add(sb, "\"");
}

static void append_ts_labels(dstrbuf_t *sb, const char *label, ts_label_kind_t kind) {
  char stream[METRICS_LABEL_MAX + 1];
  char esc[2 * METRICS_LABEL_MAX + 2];
  const char *sep = kind >= TS_LABEL_TABLE ? strchr(label, METRICS_LABEL_SEP) : NULL;
  size_t n = sep ? (size_t)(sep - label) : strlen(label);

  if (!label[0]) return;
  memcpy(stream, label, n);
  stream[n] = '\0';
  dstrbuf_add(sb, ",direction=\"");
  dstrbuf_add(sb, strncmp(stream, "in", 2) == 0 ? "input" : "output");
  dstrbuf_add(sb, "\",stream=\"");
  escape_label(stream, esc, sizeof esc);
  dstrbuf_add(sb, esc);
  dstrbuf_add(sb, "\"");
  if (sep) {
    const char *suffix_label;
    switch (kind) {
      case TS_LABEL_SERVICE: suffix_label = ",service=\""; break;
      case TS_LABEL_PID:     suffix_label = ",pid=\""; break;
      default:               suffix_label = ",table=\""; break;
    }
    escape_label(sep + 1, esc, sizeof esc);
    dstrbuf_add(sb, suffix_label);
    dstrbuf_add(sb, esc);
    dstrbuf_add(sb, "\"");
  }
}

#define DEF_ID_MAX 300 /* comfortably above highest metrics_id_t value */

typedef struct {
  const store_slot_t *slot;
  const char *label;
  size_t label_len;
  uint64_t value;
} entry_ref_t;

static void count_entries(const store_t *st, const int *def_idx, size_t *count, size_t *total) {
  for (int si = 0; si < STORE_MAX_INSTANCES; si++) {
    const store_slot_t *slot = &st->slots[si];
    metrics_reader_t r;
    metrics_id_t id;
    const char *label;
    size_t label_len;
    uint64_t value;
    if (!slot->valid) continue;
    metrics_reader_init_body(&r, slot->version, slot->live.data, slot->live.len);
    while (metrics_reader_next_ref(&r, &id, &label, &label_len, &value) == 1) {
      int di = ((unsigned)id < DEF_ID_MAX) ? def_idx[id] : -1;
      if (di >= 0) {
        count[(unsigned)di]++;
        (*total)++;
      }
    }
  }
}

static void fill_entry_refs(const store_t *st, const int *def_idx, entry_ref_t *refs, size_t *cursor) {
  for (int si = 0; si < STORE_MAX_INSTANCES; si++) {
    const store_slot_t *slot = &st->slots[si];
    metrics_reader_t r;
    metrics_id_t id;
    const char *label;
    size_t label_len;
    uint64_t value;
    if (!slot->valid) continue;
    metrics_reader_init_body(&r, slot->version, slot->live.data, slot->live.len);
    while (metrics_reader_next_ref(&r, &id, &label, &label_len, &value) == 1) {
      int di = ((unsigned)id < DEF_ID_MAX) ? def_idx[id] : -1;
      if (di >= 0) {
        entry_ref_t *ref = &refs[cursor[(unsigned)di]++];
        ref->slot = slot;
        ref->label = label;
        ref->label_len = label_len;
        ref->value = value;
      }
    }
  }
}

/* closes a label set opened at mark (just after '{'). without base labels the set can be empty or lead with ',' */
static void close_labels(dstrbuf_t *sb, size_t mark, int instance_labels) {
  if (!sb->buf) return;
  if (instance_labels) {
    dstrbuf_add(sb, "}");
    return;
  }
  if (sb->len == mark) {
    sb->len = mark - 1;
  } else {
    if (sb->buf[mark] == ',') {
      memmove(sb->buf + mark, sb->buf + mark + 1, sb->len - mark - 1);
      sb->len--;
    }
    dstrbuf_add(sb, "}");
  }
  sb->buf[sb->len] = '\0';
}

/* one pass over every stored entry, bucketed by def instead of one scan per def */
void render_series(dstrbuf_t *sb, const store_t *st, int instance_labels) {
  int def_idx[DEF_ID_MAX]; /* metrics_id_t -> DEFS[] index, -1 if unused */
  size_t count[N_DEFS];
  size_t start[N_DEFS];
  entry_ref_t *refs = NULL;
  size_t total = 0;
  for (unsigned i = 0; i < DEF_ID_MAX; i++) def_idx[i] = -1;
  for (unsigned i = 0; i < N_DEFS; i++) {
    count[i] = 0;
    start[i] = 0;
    if ((unsigned)DEFS[i].id < DEF_ID_MAX) def_idx[DEFS[i].id] = (int)i;
  }

  count_entries(st, def_idx, count, &total);

  if (total) {
    refs = malloc(sizeof *refs * total);
    if (refs) {
      size_t cursor[N_DEFS];
      size_t off = 0;
      for (unsigned i = 0; i < N_DEFS; i++) {
        start[i] = off;
        cursor[i] = off;
        off += count[i];
      }
      fill_entry_refs(st, def_idx, refs, cursor);
    }
  }

  for (unsigned i = 0; i < N_DEFS; i++) {
    const metric_def_t *def = &DEFS[i];
    if (!count[i] || !refs) continue;
    dstrbuf_add(sb, "# HELP ");
    dstrbuf_add(sb, def->name);
    dstrbuf_add(sb, " ");
    dstrbuf_add(sb, def->help);
    dstrbuf_add(sb, "\n# TYPE ");
    dstrbuf_add(sb, def->name);
    dstrbuf_add(sb, " ");
    dstrbuf_add(sb, metric_kind_name(def->kind));
    dstrbuf_add(sb, "\n");
    for (size_t k = start[i]; k < start[i] + count[i]; k++) {
      const entry_ref_t *e = &refs[k];
      char label[METRICS_LABEL_MAX + 1];
      size_t mark;
      memcpy(label, e->label, e->label_len);
      label[e->label_len] = '\0';
      dstrbuf_add(sb, def->name);
      dstrbuf_add(sb, "{");
      mark = sb->len;
      if (instance_labels) append_base_labels(sb, e->slot);
      if (def->ts_label) {
        append_ts_labels(sb, label, def->ts_label);
      } else if (def->composite_input_reason) {
        append_composite_input_reason(sb, label);
      } else if (def->label_name) {
        char esc[2 * METRICS_LABEL_MAX + 2];
        escape_label(label, esc, sizeof esc);
        dstrbuf_add(sb, ",");
        dstrbuf_add(sb, def->label_name);
        dstrbuf_add(sb, "=\"");
        dstrbuf_add(sb, esc);
        dstrbuf_add(sb, "\"");
      }
      close_labels(sb, mark, instance_labels);
      dstrbuf_add(sb, " ");
      if (def->kind == M_GAUGE_SIGNED) dstrbuf_add_i64(sb, metrics_unzigzag(e->value));
      else dstrbuf_add_u64(sb, e->value);
      dstrbuf_add(sb, "\n");
    }
  }
  free(refs);
}

static void add_head(dstrbuf_t *sb, const char *name, const char *type, const char *help) {
  dstrbuf_add(sb, "# HELP ");
  dstrbuf_add(sb, name);
  dstrbuf_add(sb, " ");
  dstrbuf_add(sb, help);
  dstrbuf_add(sb, "\n# TYPE ");
  dstrbuf_add(sb, name);
  dstrbuf_add(sb, " ");
  dstrbuf_add(sb, type);
  dstrbuf_add(sb, "\n");
}

static void add_sample(dstrbuf_t *sb, const char *series, uint64_t value) {
  dstrbuf_add(sb, series);
  dstrbuf_add(sb, " ");
  dstrbuf_add_u64(sb, value);
  dstrbuf_add(sb, "\n");
}

static void render_snapshot_age(dstrbuf_t *sb, const store_t *st, double now_mono) {
  int any = 0;
  for (int i = 0; i < STORE_MAX_INSTANCES; i++) if (st->slots[i].valid) any = 1;
  if (!any) return;

  add_head(sb, "dvbipi_metrics_snapshot_age_seconds", "gauge", "seconds since this instance's last snapshot was received");
  for (int i = 0; i < STORE_MAX_INSTANCES; i++) {
    const store_slot_t *slot = &st->slots[i];
    if (!slot->valid) continue;
    dstrbuf_add(sb, "dvbipi_metrics_snapshot_age_seconds{");
    append_base_labels(sb, slot);
    dstrbuf_appendf(sb, "} %.3f\n", now_mono - slot->received_mono);
  }
}

static void render_self_metrics(dstrbuf_t *sb, const store_t *st) {
  uint64_t active = 0;
  for (int i = 0; i < STORE_MAX_INSTANCES; i++) if (st->slots[i].valid) active++;

  add_head(sb, "dvbipi_metrics_instances", "gauge", "exporter instances currently tracked");
  add_sample(sb, "dvbipi_metrics_instances", active);

  add_head(sb, "dvbipi_metrics_snapshots_received_total", "counter", "snapshots accepted and stored");
  add_sample(sb, "dvbipi_metrics_snapshots_received_total", st->stats.snapshots_received_total);

  add_head(sb, "dvbipi_metrics_snapshots_rejected_total", "counter", "snapshots rejected by reason");
  add_sample(sb, "dvbipi_metrics_snapshots_rejected_total{reason=\"malformed\"}", st->stats.snapshots_rejected_malformed);
  add_sample(sb, "dvbipi_metrics_snapshots_rejected_total{reason=\"stale\"}", st->stats.snapshots_rejected_stale);
  add_sample(sb, "dvbipi_metrics_snapshots_rejected_total{reason=\"full\"}", st->stats.snapshots_rejected_full);
  add_sample(sb, "dvbipi_metrics_snapshots_rejected_total{reason=\"version\"}", st->stats.snapshots_rejected_version);
  add_sample(sb, "dvbipi_metrics_snapshots_rejected_total{reason=\"toolarge\"}", st->stats.snapshots_rejected_toolarge);

  add_head(sb, "dvbipi_metrics_snapshots_incomplete_total", "counter", "snapshots discarded because a piece of them was lost");
  add_sample(sb, "dvbipi_metrics_snapshots_incomplete_total", st->stats.snapshots_incomplete);

  add_head(sb, "dvbipi_metrics_parts_orphaned_total", "counter", "stray snapshot pieces dropped");
  add_sample(sb, "dvbipi_metrics_parts_orphaned_total", st->stats.parts_orphaned);

  add_head(sb, "dvbipi_metrics_http_requests_total", "counter", "/metrics HTTP requests by response status");
  add_sample(sb, "dvbipi_metrics_http_requests_total{status=\"200\"}", st->stats.http_requests_200);
  add_sample(sb, "dvbipi_metrics_http_requests_total{status=\"404\"}", st->stats.http_requests_404);
}

void render_openmetrics(const store_t *st, double now_mono, char **out, size_t *out_len) {
  dstrbuf_t sb;
  dstrbuf_init(&sb);
  render_series(&sb, st, 1);
  render_snapshot_age(&sb, st, now_mono);
  render_self_metrics(&sb, st);
  dstrbuf_add(&sb, "# EOF\n");
  *out = sb.buf;
  *out_len = sb.len;
}

static int local_flush(void *ctx, const unsigned char *buf, size_t len) {
  store_ingest(ctx, buf, len, 0.0, 0);
  return 0;
}

int render_local(metrics_component_t component, metrics_extra_fn fill, void *ctx, char **out, size_t *out_len) {
  store_t st;
  metrics_writer_t w;
  metrics_hdr_t hdr;
  dstrbuf_t sb;
  size_t len;

  memset(&hdr, 0, sizeof hdr);
  hdr.proto_version = METRICS_PROTO_VERSION;
  hdr.component = component;
  bufcpy(hdr.metrics_id, sizeof hdr.metrics_id, "local");
  hdr.process_start_time = 1;
  hdr.sequence = 1;
  if (metrics_writer_begin(&w, &hdr)) return -1;
  store_init(&st);
  w.flush = local_flush;
  w.flush_ctx = &st;
  fill(&w, ctx);
  len = metrics_writer_finish(&w);
  if (!len) {
    store_free(&st);
    return -1;
  }
  store_ingest(&st, w.buf, len, 0.0, 0);
  dstrbuf_init(&sb);
  render_series(&sb, &st, 0);
  store_free(&st);
  if (!sb.buf) return -1;
  *out = sb.buf;
  *out_len = sb.len;
  return 0;
}
