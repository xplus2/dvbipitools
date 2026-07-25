# Copyright 2026 dvbipitools authors. Licensed under GPL-3.0-or-later.
# See NOTICE and LICENSE for details and authorship information.

# run ./cofigure first for config.mk

include config.mk

.DEFAULT_GOAL := all

TOOLS := dipirec dipiscan dipiradiohead dipisds dipixmltv dipibim dipiepg dipitvhead

dipirec_SRCS := \
	src/dipirec/main.c \
	src/dipirec/args.c \
	src/dipirec/record.c \
	src/dipirec/ret_client.c \
	src/lib/log.c \
	src/lib/signal.c \
	src/lib/net/multicast.c \
	src/lib/net/udpxy.c \
	src/lib/demux/rtp.c \
	src/lib/demux/rtx.c \
	src/lib/demux/crc32.c \
	src/lib/demux/psi.c \
	src/lib/demux/tspack.c \
	src/lib/demux/pes.c \
	src/lib/mux/rtcp_build.c \
	src/dipirec/filter/ts.c \
	src/dipirec/mux/ebml.c \
	src/dipirec/mux/mkv.c \
	src/dipirec/mux/teletext.c

dipiscan_SRCS := \
	src/dipiscan/main.c \
	src/dipiscan/args.c \
	src/dipiscan/format.c \
	src/dipiscan/scan.c \
	src/lib/log.c \
	src/lib/signal.c \
	src/lib/sds_xml.c \
	src/lib/xml_util.c \
	src/lib/net/multicast.c \
	src/lib/net/udpxy.c \
	src/lib/demux/rtp.c \
	src/lib/demux/crc32.c \
	src/lib/demux/psi.c \
	src/lib/demux/tspack.c

dipisds_SRCS := \
	src/dipisds/main.c \
	src/dipisds/args.c \
	src/dipisds/input.c \
	src/dipisds/format_out.c \
	src/dipisds/announce.c \
	src/dipisds/listen.c \
	src/lib/log.c \
	src/lib/signal.c \
	src/lib/sds_xml.c \
	src/lib/xml_util.c \
	src/lib/net/multicast.c \
	src/lib/net/dvbstp.c \
	src/lib/demux/crc32.c

dipixmltv_SRCS := \
	src/dipixmltv/main.c \
	src/dipixmltv/args.c \
	src/dipixmltv/revmap.c \
	src/dipixmltv/suggest.c \
	src/lib/log.c \
	src/lib/xml_util.c \
	src/lib/ioutil.c \
	src/lib/tva/epg_doc.c \
	src/lib/tva/tva_xml.c \
	src/lib/tva/mapping.c \
	src/lib/tva/xmltv.c \
	src/lib/tva/timefmt.c

dipibim_SRCS := \
	src/dipibim/main.c \
	src/dipibim/args.c \
	src/lib/log.c \
	src/lib/xml_util.c \
	src/lib/ioutil.c \
	src/lib/tva/epg_doc.c \
	src/lib/tva/tva_xml.c \
	src/lib/bim/bitwriter.c \
	src/lib/bim/bitreader.c \
	src/lib/bim/strrepo.c \
	src/lib/bim/codec.c \
	src/lib/bim/fragment.c \
	src/lib/bim/accessunit.c

dipiepg_SRCS := \
	src/dipiepg/main.c \
	src/dipiepg/args.c \
	src/dipiepg/announce.c \
	src/dipiepg/listen.c \
	src/dipiepg/container.c \
	src/lib/log.c \
	src/lib/signal.c \
	src/lib/xml_util.c \
	src/lib/ioutil.c \
	src/lib/net/multicast.c \
	src/lib/net/dvbstp.c \
	src/lib/demux/crc32.c \
	src/lib/tva/epg_doc.c \
	src/lib/tva/tva_xml.c \
	src/lib/tva/mapping.c \
	src/lib/tva/xmltv.c \
	src/lib/tva/timefmt.c \
	src/lib/bim/bitwriter.c \
	src/lib/bim/bitreader.c \
	src/lib/bim/strrepo.c \
	src/lib/bim/codec.c \
	src/lib/bim/fragment.c \
	src/lib/bim/accessunit.c

HAVE_OPENSSL := $(shell pkg-config --exists openssl && echo yes)

ifeq ($(HAVE_OPENSSL),yes)
dipiradiohead_TLS_SRC := src/lib/net/tls.c
dipiradiohead_EXTRA_CFLAGS := $(shell pkg-config --cflags openssl)
ifneq (,$(findstring -static,$(LDFLAGS)))
dipiradiohead_EXTRA_LDFLAGS := $(shell pkg-config --static --libs openssl)
else
dipiradiohead_EXTRA_LDFLAGS := $(shell pkg-config --libs openssl)
endif
else
dipiradiohead_TLS_SRC := src/lib/net/tls_stub.c
$(warning dipiradiohead: OpenSSL not found via pkg-config, building without HTTPS support)
endif

dipiradiohead_SRCS := \
	src/dipiradiohead/main.c \
	src/dipiradiohead/args.c \
	src/dipiradiohead/radiohead.c \
	src/lib/log.c \
	src/lib/signal.c \
	src/lib/net/multicast.c \
	$(dipiradiohead_TLS_SRC) \
	src/lib/net/httpclient.c \
	src/lib/demux/crc32.c \
	src/lib/demux/bitreader.c \
	src/dipiradiohead/input/playlist.c \
	src/dipiradiohead/input/icy.c \
	src/dipiradiohead/input/id3.c \
	src/dipiradiohead/input/source.c \
	src/dipiradiohead/framer/mpegaudio.c \
	src/dipiradiohead/framer/aac_adts.c \
	src/dipiradiohead/framer/aac_latm.c \
	src/dipiradiohead/mux/psi.c \
	src/dipiradiohead/mux/pes.c \
	src/dipiradiohead/mux/tspacketizer.c \
	src/lib/mux/rtpheader.c \
	src/lib/mux/psi_build.c \
	src/lib/mux/tspacket_write.c

ifeq ($(HAVE_OPENSSL),yes)
dipitvhead_TLS_SRC := src/lib/net/tls.c
dipitvhead_EXTRA_CFLAGS := $(shell pkg-config --cflags openssl)
ifneq (,$(findstring -static,$(LDFLAGS)))
dipitvhead_EXTRA_LDFLAGS := $(shell pkg-config --static --libs openssl)
else
dipitvhead_EXTRA_LDFLAGS := $(shell pkg-config --libs openssl)
endif
else
dipitvhead_TLS_SRC := src/lib/net/tls_stub.c
$(warning dipitvhead: OpenSSL not found via pkg-config, building without HTTPS support)
endif

dipitvhead_SRCS := \
	src/dipitvhead/main.c \
	src/dipitvhead/args.c \
	src/dipitvhead/tvhead.c \
	src/dipitvhead/input/source.c \
	src/dipitvhead/mux/pmtbuild.c \
	src/dipitvhead/mux/aitbuild.c \
	src/dipitvhead/mux/remux.c \
	src/dipitvhead/mux/bitrate.c \
	src/lib/log.c \
	src/lib/signal.c \
	src/lib/net/multicast.c \
	$(dipitvhead_TLS_SRC) \
	src/lib/net/httpclient.c \
	src/lib/demux/crc32.c \
	src/lib/demux/psi.c \
	src/lib/demux/tspack.c \
	src/lib/demux/rtp.c \
	src/lib/mux/rtpheader.c \
	src/lib/mux/psi_build.c \
	src/lib/mux/tspacket_write.c

HAVE_LIBPCAP := $(shell pkg-config --exists libpcap && echo yes)

ifeq ($(HAVE_LIBPCAP),yes)
TOOLS += dipifccret
dipifccret_EXTRA_CFLAGS := -pthread $(shell pkg-config --cflags libpcap)
dipifccret_EXTRA_LDFLAGS := -pthread $(shell pkg-config --libs libpcap)
dipifccret_SRCS := \
	src/dipifccret/main.c \
	src/dipifccret/args.c \
	src/dipifccret/capture.c \
	src/dipifccret/channel.c \
	src/dipifccret/listen.c \
	src/dipifccret/ret/ret.c \
	src/dipifccret/ret/mcsend.c \
	src/dipifccret/fcc/burst.c \
	src/lib/log.c \
	src/lib/signal.c \
	src/lib/net/multicast.c \
	src/lib/demux/rtp.c \
	src/lib/demux/rtcp.c \
	src/lib/demux/psi.c \
	src/lib/demux/crc32.c \
	src/lib/mux/rtcp_build.c \
	src/lib/mux/rtx.c
else
$(warning dipifccret: libpcap not found via pkg-config, skipping this tool entirely (not an optional-feature degrade, capture is its whole purpose))
endif

ALL_OBJS :=

define TOOL_template
$(1)_OBJS := $$($(1)_SRCS:.c=.o)
ALL_OBJS += $$($(1)_OBJS)
$$($(1)_OBJS): CFLAGS += $$($(1)_EXTRA_CFLAGS)
$(1): $$($(1)_OBJS)
	$$(CC) $$^ $$(LDFLAGS) $$($(1)_EXTRA_LDFLAGS) -o $$@
endef

$(foreach t,$(TOOLS),$(eval $(call TOOL_template,$(t))))

UNIT_TESTS := lib_demux_crc32 lib_demux_psi lib_demux_bitreader lib_demux_rtp lib_demux_rtx lib_demux_tspack lib_demux_pes \
	lib_mux_psi_build lib_mux_rtpheader lib_mux_rtx lib_mux_rtcp_build lib_mux_tspacket_write \
	lib_bim_bitwriter lib_bim_bitreader lib_bim_strrepo lib_bim_codec \
	lib_xml_util lib_tva_timefmt lib_tva_epg_doc lib_tva_mapping lib_tva_xmltv lib_tva_tva_xml \
	lib_bim_fragment lib_bim_accessunit \
	lib_sds_xml dipiscan_format dipixmltv_revmap dipixmltv_suggest \
	dipiradiohead_mpegaudio dipiradiohead_aac_adts dipiradiohead_aac_latm \
	dipiradiohead_psi dipiradiohead_pes dipiradiohead_tspacketizer \
	dipitvhead_pmtbuild dipitvhead_aitbuild dipitvhead_bitrate dipitvhead_remux \
	dipirec_ebml dipirec_ts_filter dipirec_teletext dipirec_mkv \
	dipifccret_channel dipifccret_ret_mcsend dipifccret_burst dipifccret_ret

lib_demux_crc32_BIN := tests/unit/lib/demux/test_crc32
lib_demux_crc32_SRCS := \
	tests/unit/lib/demux/test_crc32.c \
	src/lib/demux/crc32.c

lib_demux_psi_BIN := tests/unit/lib/demux/test_psi
lib_demux_psi_SRCS := \
	tests/unit/lib/demux/test_psi.c \
	src/lib/demux/psi.c \
	src/lib/demux/crc32.c

lib_demux_bitreader_BIN := tests/unit/lib/demux/test_bitreader
lib_demux_bitreader_SRCS := \
	tests/unit/lib/demux/test_bitreader.c \
	src/lib/demux/bitreader.c

lib_demux_rtp_BIN := tests/unit/lib/demux/test_rtp
lib_demux_rtp_SRCS := \
	tests/unit/lib/demux/test_rtp.c \
	src/lib/demux/rtp.c

lib_demux_rtx_BIN := tests/unit/lib/demux/test_rtx
lib_demux_rtx_SRCS := \
	tests/unit/lib/demux/test_rtx.c \
	src/lib/demux/rtx.c \
	src/lib/demux/rtp.c

lib_demux_tspack_BIN := tests/unit/lib/demux/test_tspack
lib_demux_tspack_SRCS := \
	tests/unit/lib/demux/test_tspack.c \
	src/lib/demux/tspack.c

lib_demux_pes_BIN := tests/unit/lib/demux/test_pes
lib_demux_pes_SRCS := \
	tests/unit/lib/demux/test_pes.c \
	src/lib/demux/pes.c

lib_mux_psi_build_BIN := tests/unit/lib/mux/test_psi_build
lib_mux_psi_build_SRCS := \
	tests/unit/lib/mux/test_psi_build.c \
	src/lib/mux/psi_build.c \
	src/lib/demux/psi.c \
	src/lib/demux/crc32.c

lib_mux_rtpheader_BIN := tests/unit/lib/mux/test_rtpheader
lib_mux_rtpheader_SRCS := \
	tests/unit/lib/mux/test_rtpheader.c \
	src/lib/mux/rtpheader.c \
	src/lib/demux/rtp.c

lib_mux_rtx_BIN := tests/unit/lib/mux/test_rtx
lib_mux_rtx_SRCS := \
	tests/unit/lib/mux/test_rtx.c \
	src/lib/mux/rtx.c \
	src/lib/demux/rtx.c \
	src/lib/demux/rtp.c

lib_mux_rtcp_build_BIN := tests/unit/lib/mux/test_rtcp_build
lib_mux_rtcp_build_SRCS := \
	tests/unit/lib/mux/test_rtcp_build.c \
	src/lib/mux/rtcp_build.c \
	src/lib/demux/rtcp.c

lib_mux_tspacket_write_BIN := tests/unit/lib/mux/test_tspacket_write
lib_mux_tspacket_write_SRCS := \
	tests/unit/lib/mux/test_tspacket_write.c \
	src/lib/mux/tspacket_write.c

lib_bim_bitwriter_BIN := tests/unit/lib/bim/test_bitwriter
lib_bim_bitwriter_SRCS := \
	tests/unit/lib/bim/test_bitwriter.c \
	src/lib/bim/bitwriter.c \
	src/lib/bim/bitreader.c

lib_bim_bitreader_BIN := tests/unit/lib/bim/test_bitreader
lib_bim_bitreader_SRCS := \
	tests/unit/lib/bim/test_bitreader.c \
	src/lib/bim/bitreader.c

lib_bim_strrepo_BIN := tests/unit/lib/bim/test_strrepo
lib_bim_strrepo_SRCS := \
	tests/unit/lib/bim/test_strrepo.c \
	src/lib/bim/strrepo.c

lib_bim_codec_BIN := tests/unit/lib/bim/test_codec
lib_bim_codec_SRCS := \
	tests/unit/lib/bim/test_codec.c \
	src/lib/bim/codec.c \
	src/lib/bim/bitwriter.c \
	src/lib/bim/bitreader.c \
	src/lib/bim/strrepo.c

lib_xml_util_BIN := tests/unit/lib/test_xml_util
lib_xml_util_SRCS := \
	tests/unit/lib/test_xml_util.c \
	src/lib/xml_util.c

lib_sds_xml_BIN := tests/unit/lib/test_sds_xml
lib_sds_xml_SRCS := \
	tests/unit/lib/test_sds_xml.c \
	src/lib/sds_xml.c \
	src/lib/xml_util.c

lib_tva_timefmt_BIN := tests/unit/lib/tva/test_timefmt
lib_tva_timefmt_SRCS := \
	tests/unit/lib/tva/test_timefmt.c \
	src/lib/tva/timefmt.c

lib_tva_epg_doc_BIN := tests/unit/lib/tva/test_epg_doc
lib_tva_epg_doc_SRCS := \
	tests/unit/lib/tva/test_epg_doc.c \
	src/lib/tva/epg_doc.c

lib_tva_mapping_BIN := tests/unit/lib/tva/test_mapping
lib_tva_mapping_SRCS := \
	tests/unit/lib/tva/test_mapping.c \
	src/lib/tva/mapping.c

lib_tva_xmltv_BIN := tests/unit/lib/tva/test_xmltv
lib_tva_xmltv_SRCS := \
	tests/unit/lib/tva/test_xmltv.c \
	src/lib/tva/xmltv.c \
	src/lib/tva/timefmt.c \
	src/lib/tva/epg_doc.c \
	src/lib/xml_util.c \
	src/lib/ioutil.c

lib_tva_tva_xml_BIN := tests/unit/lib/tva/test_tva_xml
lib_tva_tva_xml_SRCS := \
	tests/unit/lib/tva/test_tva_xml.c \
	src/lib/tva/tva_xml.c \
	src/lib/tva/epg_doc.c \
	src/lib/xml_util.c \
	src/lib/ioutil.c

BIM_FRAGMENT_DEPS := \
	src/lib/bim/fragment.c \
	src/lib/bim/codec.c \
	src/lib/bim/bitwriter.c \
	src/lib/bim/bitreader.c \
	src/lib/bim/strrepo.c \
	src/lib/tva/tva_xml.c \
	src/lib/tva/epg_doc.c \
	src/lib/xml_util.c \
	src/lib/ioutil.c

lib_bim_fragment_BIN := tests/unit/lib/bim/test_fragment
lib_bim_fragment_SRCS := \
	tests/unit/lib/bim/test_fragment.c \
	$(BIM_FRAGMENT_DEPS)

lib_bim_accessunit_BIN := tests/unit/lib/bim/test_accessunit
lib_bim_accessunit_SRCS := \
	tests/unit/lib/bim/test_accessunit.c \
	src/lib/bim/accessunit.c \
	$(BIM_FRAGMENT_DEPS)

dipiscan_format_BIN := tests/unit/dipiscan/test_format
dipiscan_format_SRCS := \
	tests/unit/dipiscan/test_format.c \
	src/dipiscan/format.c \
	src/lib/sds_xml.c \
	src/lib/xml_util.c

dipixmltv_revmap_BIN := tests/unit/dipixmltv/test_revmap
dipixmltv_revmap_SRCS := \
	tests/unit/dipixmltv/test_revmap.c \
	src/dipixmltv/revmap.c

dipixmltv_suggest_BIN := tests/unit/dipixmltv/test_suggest
dipixmltv_suggest_SRCS := \
	tests/unit/dipixmltv/test_suggest.c \
	src/dipixmltv/suggest.c \
	src/lib/tva/xmltv.c \
	src/lib/tva/timefmt.c \
	src/lib/tva/epg_doc.c \
	src/lib/xml_util.c \
	src/lib/ioutil.c

dipiradiohead_mpegaudio_BIN := tests/unit/dipiradiohead/test_mpegaudio
dipiradiohead_mpegaudio_SRCS := \
	tests/unit/dipiradiohead/test_mpegaudio.c \
	src/dipiradiohead/framer/mpegaudio.c

dipiradiohead_aac_adts_BIN := tests/unit/dipiradiohead/test_aac_adts
dipiradiohead_aac_adts_SRCS := \
	tests/unit/dipiradiohead/test_aac_adts.c \
	src/dipiradiohead/framer/aac_adts.c

dipiradiohead_aac_latm_BIN := tests/unit/dipiradiohead/test_aac_latm
dipiradiohead_aac_latm_SRCS := \
	tests/unit/dipiradiohead/test_aac_latm.c \
	src/dipiradiohead/framer/aac_latm.c \
	src/lib/demux/bitreader.c \
	src/lib/bim/bitwriter.c

dipiradiohead_psi_BIN := tests/unit/dipiradiohead/test_psi
dipiradiohead_psi_SRCS := \
	tests/unit/dipiradiohead/test_psi.c \
	src/dipiradiohead/mux/psi.c \
	src/lib/mux/psi_build.c \
	src/lib/demux/psi.c \
	src/lib/demux/crc32.c

dipiradiohead_pes_BIN := tests/unit/dipiradiohead/test_pes
dipiradiohead_pes_SRCS := \
	tests/unit/dipiradiohead/test_pes.c \
	src/dipiradiohead/mux/pes.c \
	src/lib/demux/pes.c

dipiradiohead_tspacketizer_BIN := tests/unit/dipiradiohead/test_tspacketizer
dipiradiohead_tspacketizer_SRCS := \
	tests/unit/dipiradiohead/test_tspacketizer.c \
	src/dipiradiohead/mux/tspacketizer.c \
	src/dipiradiohead/mux/psi.c \
	src/dipiradiohead/mux/pes.c \
	src/lib/mux/psi_build.c \
	src/lib/mux/tspacket_write.c \
	src/lib/demux/crc32.c

dipitvhead_pmtbuild_BIN := tests/unit/dipitvhead/test_pmtbuild
dipitvhead_pmtbuild_SRCS := \
	tests/unit/dipitvhead/test_pmtbuild.c \
	src/dipitvhead/mux/pmtbuild.c \
	src/lib/mux/psi_build.c \
	src/lib/demux/psi.c \
	src/lib/demux/crc32.c

dipitvhead_aitbuild_BIN := tests/unit/dipitvhead/test_aitbuild
dipitvhead_aitbuild_SRCS := \
	tests/unit/dipitvhead/test_aitbuild.c \
	src/dipitvhead/mux/aitbuild.c \
	src/lib/mux/psi_build.c \
	src/lib/demux/crc32.c

dipitvhead_bitrate_BIN := tests/unit/dipitvhead/test_bitrate
dipitvhead_bitrate_SRCS := \
	tests/unit/dipitvhead/test_bitrate.c \
	src/dipitvhead/mux/bitrate.c

dipitvhead_remux_BIN := tests/unit/dipitvhead/test_remux
dipitvhead_remux_SRCS := \
	tests/unit/dipitvhead/test_remux.c \
	src/dipitvhead/mux/remux.c \
	src/dipitvhead/mux/pmtbuild.c \
	src/dipitvhead/mux/aitbuild.c \
	src/lib/mux/psi_build.c \
	src/lib/mux/tspacket_write.c \
	src/lib/demux/psi.c \
	src/lib/demux/crc32.c \
	src/lib/log.c

dipirec_ebml_BIN := tests/unit/dipirec/test_ebml
dipirec_ebml_SRCS := \
	tests/unit/dipirec/test_ebml.c \
	src/dipirec/mux/ebml.c

dipirec_ts_filter_BIN := tests/unit/dipirec/test_ts_filter
dipirec_ts_filter_SRCS := \
	tests/unit/dipirec/test_ts_filter.c \
	src/dipirec/filter/ts.c \
	src/lib/demux/psi.c \
	src/lib/demux/crc32.c

dipirec_teletext_BIN := tests/unit/dipirec/test_teletext
dipirec_teletext_SRCS := \
	tests/unit/dipirec/test_teletext.c \
	src/dipirec/mux/teletext.c

dipirec_mkv_BIN := tests/unit/dipirec/test_mkv
dipirec_mkv_SRCS := \
	tests/unit/dipirec/test_mkv.c \
	src/dipirec/mux/mkv.c \
	src/dipirec/mux/ebml.c \
	src/dipirec/mux/teletext.c \
	src/dipirec/args.c \
	src/lib/mux/psi_build.c \
	src/lib/demux/pes.c \
	src/lib/demux/psi.c \
	src/lib/demux/crc32.c \
	src/lib/log.c

dipifccret_channel_BIN := tests/unit/dipifccret/test_channel
dipifccret_channel_SRCS := \
	tests/unit/dipifccret/test_channel.c \
	src/dipifccret/channel.c \
	src/lib/mux/psi_build.c \
	src/lib/demux/psi.c \
	src/lib/demux/crc32.c \
	src/lib/log.c

dipifccret_ret_mcsend_BIN := tests/unit/dipifccret/ret/test_mcsend
dipifccret_ret_mcsend_SRCS := \
	tests/unit/dipifccret/ret/test_mcsend.c \
	src/dipifccret/ret/mcsend.c \
	src/lib/net/multicast.c \
	src/lib/log.c

dipifccret_burst_BIN := tests/unit/dipifccret/test_burst
dipifccret_burst_SRCS := \
	tests/unit/dipifccret/test_burst.c \
	src/dipifccret/fcc/burst.c \
	src/dipifccret/channel.c \
	src/lib/mux/rtx.c \
	src/lib/demux/rtx.c \
	src/lib/demux/rtp.c \
	src/lib/mux/psi_build.c \
	src/lib/demux/psi.c \
	src/lib/demux/crc32.c \
	src/lib/log.c

dipifccret_ret_BIN := tests/unit/dipifccret/test_ret
dipifccret_ret_SRCS := \
	tests/unit/dipifccret/test_ret.c \
	src/dipifccret/ret/ret.c \
	src/dipifccret/channel.c \
	src/lib/demux/rtcp.c \
	src/lib/mux/rtcp_build.c \
	src/lib/mux/rtx.c \
	src/lib/demux/rtx.c \
	src/lib/demux/rtp.c \
	src/lib/mux/psi_build.c \
	src/lib/demux/psi.c \
	src/lib/demux/crc32.c \
	src/lib/log.c

# _BIN/_SRCS/TEST_BINS stay unconditional (unlike TESTS=yes gate below),
# 'make clean' finds these paths regardless of current config.mk state -
# a prior '--tests' build's artifacts must clean up even after
# reconfiguring without --tests.
define UNIT_TEST_template
$(1)_OBJS := $$($(1)_SRCS:.c=.o)
ALL_OBJS += $$($(1)_OBJS)
$$($(1)_OBJS): CFLAGS += $(CHECK_CFLAGS)
$$($(1)_BIN): $$($(1)_OBJS)
	$$(CC) $$^ $$(LDFLAGS) $(CHECK_LIBS) -pthread -o $$@
endef

$(foreach t,$(UNIT_TESTS),$(eval $(call UNIT_TEST_template,$(t))))

TEST_BINS := $(foreach t,$(UNIT_TESTS),$($(t)_BIN))

.PHONY: test
ifeq ($(TESTS),yes)
test: $(TEST_BINS)
	@set -e; for t in $(TEST_BINS); do echo "running $$t"; ./$$t; done
else
test:
	@echo "test: reconfigure with './configure --tests' to enable" >&2; exit 1
endif

INTEGRATION_DIPIBIM_SCRIPTS := $(wildcard tests/integration/dipibim/*.sh)
INTEGRATION_DIPIXMLTV_SCRIPTS := $(wildcard tests/integration/dipixmltv/*.sh)

.PHONY: integration-test
integration-test: dipibim dipixmltv
	@set -e; \
	for s in $(INTEGRATION_DIPIBIM_SCRIPTS); do echo "running $$s"; sh $$s ./dipibim; done; \
	for s in $(INTEGRATION_DIPIXMLTV_SCRIPTS); do echo "running $$s"; sh $$s ./dipixmltv; done

FUZZ_BIM_DEPS := \
	src/lib/bim/accessunit.c \
	src/lib/bim/fragment.c \
	src/lib/bim/codec.c \
	src/lib/bim/bitwriter.c \
	src/lib/bim/bitreader.c \
	src/lib/bim/strrepo.c \
	src/lib/tva/tva_xml.c \
	src/lib/tva/epg_doc.c \
	src/lib/xml_util.c \
	src/lib/ioutil.c

# _BIN/_SRCS and FUZZ_BINS stay unconditional (unlike FUZZING=yes gate
# below), 'make clean' finds these paths regardless of current config.mk
# state - a prior '--fuzz' build's artifacts must clean up even after
# reconfiguring without --fuzz.
FUZZ_HARNESSES := fuzz_psi fuzz_bim_accessunit fuzz_sds_xml fuzz_rtcp

fuzz_psi_BIN := tests/fuzz/fuzz_psi
fuzz_psi_SRCS := \
	tests/fuzz/fuzz_psi.c \
	src/lib/demux/psi.c \
	src/lib/demux/crc32.c

fuzz_bim_accessunit_BIN := tests/fuzz/fuzz_bim_accessunit
fuzz_bim_accessunit_SRCS := \
	tests/fuzz/fuzz_bim_accessunit.c \
	$(FUZZ_BIM_DEPS)

fuzz_sds_xml_BIN := tests/fuzz/fuzz_sds_xml
fuzz_sds_xml_SRCS := \
	tests/fuzz/fuzz_sds_xml.c \
	src/lib/sds_xml.c \
	src/lib/xml_util.c

fuzz_rtcp_BIN := tests/fuzz/fuzz_rtcp
fuzz_rtcp_SRCS := \
	tests/fuzz/fuzz_rtcp.c \
	src/lib/demux/rtcp.c

fuzz_gen_seeds_BIN := tests/fuzz/gen_seeds
fuzz_gen_seeds_SRCS := \
	tests/fuzz/gen_seeds.c \
	$(FUZZ_BIM_DEPS) \
	src/lib/mux/psi_build.c \
	src/lib/mux/rtcp_build.c \
	src/lib/demux/rtcp.c \
	src/lib/demux/crc32.c \
	src/lib/sds_xml.c

define FUZZ_TARGET_template
$(1)_OBJS := $$($(1)_SRCS:.c=.o)
ALL_OBJS += $$($(1)_OBJS)
$$($(1)_BIN): $$($(1)_OBJS)
	$$(CC) $$^ $$(LDFLAGS) -o $$@
endef

$(foreach t,$(FUZZ_HARNESSES) fuzz_gen_seeds,$(eval $(call FUZZ_TARGET_template,$(t))))

FUZZ_BINS := $(foreach t,$(FUZZ_HARNESSES) fuzz_gen_seeds,$($(t)_BIN))

.PHONY: fuzz-harnesses
ifeq ($(FUZZING),yes)
fuzz-harnesses: $(FUZZ_BINS)
else
fuzz-harnesses:
	@echo "fuzz-harnesses: reconfigure with './configure --fuzz' to enable" >&2; exit 1
endif

.PHONY: all clean
all: $(TOOLS)

%.o: %.c config.mk
	$(CC) $(CFLAGS) -MMD -MP -c $< -o $@

-include $(ALL_OBJS:.o=.d)

TLS_VARIANTS := src/lib/net/tls.o src/lib/net/tls_stub.o

clean:
	rm -f $(ALL_OBJS) $(ALL_OBJS:.o=.d) $(TLS_VARIANTS) $(TLS_VARIANTS:.o=.d) $(TOOLS) $(TEST_BINS) $(FUZZ_BINS)
