# Copyright 2026 dvbipitools authors. Licensed under GPL-3.0-or-later.
# See NOTICE and LICENSE for details and authorship information.

# run ./cofigure first for config.mk

include config.mk

.DEFAULT_GOAL := all

PREFIX ?= /usr/local
BINDIR := $(PREFIX)/bin
MANDIR := $(PREFIX)/share/man/man1

TOOLS := dipirec dipiscan dipiradiohead dipisds dipixmltv dipibim dipibcg dipitvhead dipimetrics

dipimetrics_SRCS := \
	src/dipimetrics/main.c \
	src/dipimetrics/args.c \
	src/dipimetrics/store.c \
	src/dipimetrics/render.c \
	src/dipimetrics/httpserver.c \
	src/lib/log.c \
	src/lib/argutil.c \
	src/lib/signal.c \
	src/lib/ioutil.c \
	src/lib/metrics/protocol.c

dipiscan_SRCS := \
	src/dipiscan/main.c \
	src/dipiscan/args.c \
	src/dipiscan/format.c \
	src/dipiscan/scan.c \
	src/lib/playlist_out.c \
	src/lib/log.c \
	src/lib/argutil.c \
	src/lib/signal.c \
	src/lib/sds_xml.c \
	src/lib/xml_util.c \
	src/lib/ioutil.c \
	src/lib/net/multicast.c \
	src/lib/net/netconnect.c \
	src/lib/net/httpclient/httpclient.c \
	src/lib/net/httpclient/url.c \
	src/lib/net/httpclient/read.c \
	src/lib/net/httpclient/async.c \
	src/lib/net/tls_stub.c \
	src/lib/demux/rtp.c \
	src/lib/demux/crc32.c \
	src/lib/demux/psi/psi.c \
	src/lib/demux/psi/parse.c \
	src/lib/demux/psi/descriptors.c \
	src/lib/demux/psi/section_asm.c \
	src/lib/demux/tspack.c

dipisds_SRCS := \
	src/dipisds/main.c \
	src/dipisds/args.c \
	src/dipisds/input.c \
	src/dipisds/format_out.c \
	src/dipisds/announce.c \
	src/dipisds/listen.c \
	src/lib/playlist_out.c \
	src/lib/log.c \
	src/lib/argutil.c \
	src/lib/uriparse.c \
	src/lib/signal.c \
	src/lib/metrics/protocol.c \
	src/lib/metrics/export.c \
	src/lib/sds_xml.c \
	src/lib/xml_util.c \
	src/lib/ioutil.c \
	src/lib/net/multicast.c \
	src/lib/net/netconnect.c \
	src/lib/net/dvbstp.c \
	src/lib/demux/crc32.c

dipixmltv_SRCS := \
	src/dipixmltv/main.c \
	src/dipixmltv/args.c \
	src/dipixmltv/revmap.c \
	src/dipixmltv/suggest.c \
	src/lib/log.c \
	src/lib/argutil.c \
	src/lib/xml_util.c \
	src/lib/ioutil.c \
	src/lib/tva/bcg_doc.c \
	src/lib/tva/tva_xml.c \
	src/lib/tva/mapping.c \
	src/lib/tva/xmltv.c \
	src/lib/tva/timefmt.c

dipibim_SRCS := \
	src/dipibim/main.c \
	src/dipibim/args.c \
	src/lib/log.c \
	src/lib/argutil.c \
	src/lib/xml_util.c \
	src/lib/ioutil.c \
	src/lib/tva/bcg_doc.c \
	src/lib/tva/tva_xml.c \
	src/lib/bim/bitwriter.c \
	src/lib/bim/bitreader.c \
	src/lib/bim/strrepo.c \
	src/lib/bim/codec.c \
	src/lib/bim/fragment.c \
	src/lib/bim/accessunit.c

LDFLAGS += -latomic

HAVE_LIBZ := $(shell pkg-config --exists zlib && echo yes)

ifeq ($(ZLIB),no)
HAVE_ZLIB := no
else
HAVE_ZLIB := $(HAVE_LIBZ)
endif

ifeq ($(HAVE_ZLIB),yes)
dipibcg_ZLIB_SRC := src/dipibcg/wrapper.c
dipibcg_EXTRA_CFLAGS := $(shell pkg-config --cflags zlib)
ifneq (,$(findstring -static,$(LDFLAGS)))
dipibcg_EXTRA_LDFLAGS := $(shell pkg-config --static --libs zlib)
else
dipibcg_EXTRA_LDFLAGS := $(shell pkg-config --libs zlib)
endif
else
dipibcg_ZLIB_SRC := src/dipibcg/wrapper_stub.c
ifneq ($(ZLIB),no)
$(warning dipibcg: zlib not found via pkg-config, building without BCG container compression support)
endif
endif

dipibcg_SRCS := \
	src/dipibcg/main.c \
	src/dipibcg/args.c \
	src/dipibcg/announce.c \
	src/dipibcg/listen.c \
	src/dipibcg/container.c \
	$(dipibcg_ZLIB_SRC) \
	src/lib/log.c \
	src/lib/argutil.c \
	src/lib/uriparse.c \
	src/lib/signal.c \
	src/lib/metrics/protocol.c \
	src/lib/metrics/export.c \
	src/lib/xml_util.c \
	src/lib/ioutil.c \
	src/lib/net/multicast.c \
	src/lib/net/netconnect.c \
	src/lib/net/dvbstp.c \
	src/lib/demux/crc32.c \
	src/lib/tva/bcg_doc.c \
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

ifeq ($(TLS),no)
HAVE_TLS := no
else
HAVE_TLS := $(HAVE_OPENSSL)
endif

ifeq ($(CSA),no)
HAVE_CSA := no
else
HAVE_CSA := yes
endif

# real libdvbcsa-dev, only for lib_scrambler_csa2's correctness. no build-time dependency
HAVE_DVBCSA := $(shell printf '#include <dvbcsa/dvbcsa.h>\nint main(void){return 0;}\n' | $(CC) -xc - -ldvbcsa -o /dev/null >/dev/null 2>&1 && echo yes)

HAVE_LIBRIST := $(shell pkg-config --exists librist && echo yes)

ifeq ($(RIST),no)
HAVE_RIST := no
else
HAVE_RIST := $(HAVE_LIBRIST)
endif

HAVE_LIBSRT := $(shell pkg-config --exists srt && echo yes)

ifeq ($(SRT),no)
HAVE_SRT := no
else
HAVE_SRT := $(HAVE_LIBSRT)
endif

ifeq ($(HAVE_TLS),yes)
dipirec_TLS_SRC := src/lib/net/tls.c
dipirec_AUTH_SRC := src/lib/net/rtmp/auth.c
dipirec_EXTRA_CFLAGS := $(shell pkg-config --cflags openssl)
ifneq (,$(findstring -static,$(LDFLAGS)))
dipirec_EXTRA_LDFLAGS := $(shell pkg-config --static --libs openssl)
else
dipirec_EXTRA_LDFLAGS := $(shell pkg-config --libs openssl)
endif
else
dipirec_TLS_SRC := src/lib/net/tls_stub.c
dipirec_AUTH_SRC := src/lib/net/rtmp/auth_stub.c
ifneq ($(TLS),no)
$(warning dipirec: OpenSSL not found via pkg-config, building without HTTPS support)
endif
endif

ifeq ($(HAVE_RIST),yes)
dipirec_RIST_SRC := src/lib/net/rist/ristout.c src/lib/net/rist/ristin.c src/lib/net/rist/ristlog.c
dipirec_EXTRA_CFLAGS += $(shell pkg-config --cflags librist)
ifneq (,$(findstring -static,$(LDFLAGS)))
dipirec_EXTRA_LDFLAGS += $(shell pkg-config --static --libs librist)
else
dipirec_EXTRA_LDFLAGS += $(shell pkg-config --libs librist)
endif
else
dipirec_RIST_SRC := src/lib/net/rist/ristout_stub.c src/lib/net/rist/ristin_stub.c src/lib/net/rist/ristlog_stub.c
endif
dipirec_EXTRA_CFLAGS += -pthread
dipirec_EXTRA_LDFLAGS += -pthread

dipirec_SRCS := \
	src/dipirec/main.c \
	src/dipirec/args.c \
	src/dipirec/record.c \
	src/dipirec/record/sink.c \
	src/dipirec/record/rtmp_fanout.c \
	src/dipirec/record/stats.c \
	src/dipirec/record/run.c \
	src/dipirec/ret_client.c \
	src/lib/log.c \
	src/lib/argutil.c \
	src/lib/uriparse.c \
	src/lib/signal.c \
	src/lib/net/multicast.c \
	src/lib/net/netconnect.c \
	src/lib/net/tssource.c \
	src/lib/net/tssink.c \
	src/lib/metrics/protocol.c \
	src/lib/metrics/export.c \
	$(dipirec_TLS_SRC) \
	$(dipirec_RIST_SRC) \
	src/lib/net/httpclient/httpclient.c \
	src/lib/net/httpclient/url.c \
	src/lib/net/httpclient/read.c \
	src/lib/net/httpclient/async.c \
	src/lib/ioutil.c \
	src/lib/demux/rtp.c \
	src/lib/demux/rtx.c \
	src/lib/demux/crc32.c \
	src/lib/demux/psi/psi.c \
	src/lib/demux/psi/parse.c \
	src/lib/demux/psi/descriptors.c \
	src/lib/demux/psi/section_asm.c \
	src/lib/demux/tspack.c \
	src/lib/demux/pes.c \
	src/lib/demux/mpts_probe.c \
	src/lib/mux/rtcp_build.c \
	src/lib/mux/rtpheader.c \
	src/lib/mux/ebml.c \
	src/lib/mux/mkv/mkv.c \
	src/lib/demux/bitreader.c \
	src/lib/demux/escodec/aubuild.c \
	src/lib/demux/escodec/audio.c \
	src/lib/demux/escodec/video.c \
	src/lib/mux/mkv/video.c \
	src/lib/mux/mkv/write.c \
	src/lib/mux/mkv/feed.c \
	src/lib/mux/teletext.c \
	src/lib/mux/amf.c \
	src/lib/mux/flv/flv.c \
	src/lib/mux/flv/feed.c \
	src/lib/mux/flv/write.c \
	src/lib/net/rtmp/handshake.c \
	src/lib/net/rtmp/chunk.c \
	src/lib/net/rtmp/session.c \
	src/lib/net/rtmp/command.c \
	src/lib/net/rtmp/rtmp.c \
	src/lib/net/rtmp/rtmpout.c \
	$(dipirec_AUTH_SRC) \
	src/dipirec/filter/ts.c \
	src/dipirec/filter/pace.c

ifeq ($(HAVE_OPENSSL),yes)
dipiradiohead_EXTRA_CFLAGS := $(shell pkg-config --cflags openssl)
ifneq (,$(findstring -static,$(LDFLAGS)))
dipiradiohead_EXTRA_LDFLAGS := $(shell pkg-config --static --libs openssl)
else
dipiradiohead_EXTRA_LDFLAGS := $(shell pkg-config --libs openssl)
endif
endif

ifeq ($(HAVE_TLS),yes)
dipiradiohead_TLS_SRC := src/lib/net/tls.c
else
dipiradiohead_TLS_SRC := src/lib/net/tls_stub.c
ifneq ($(TLS),no)
$(warning dipiradiohead: OpenSSL not found via pkg-config, building without HTTPS support)
endif
endif

ifeq ($(HAVE_OPENSSL),yes)
dipiradiohead_CISSA_SRC := src/lib/scrambler/cissa.c
dipiradiohead_BISS_SRC := src/lib/cas/biss/biss.c src/lib/cas/biss/hex.c
dipiradiohead_BISS_CA_SRC := src/lib/cas/biss/ca.c
else
dipiradiohead_CISSA_SRC := src/lib/scrambler/cissa_stub.c
dipiradiohead_BISS_SRC := src/lib/cas/biss/stub.c src/lib/cas/biss/hex.c
dipiradiohead_BISS_CA_SRC := src/lib/cas/biss/ca_stub.c
$(warning dipiradiohead: OpenSSL not found via pkg-config, building without CISSA support)
endif

ifeq ($(HAVE_CSA),yes)
dipiradiohead_CSA2_SRC := src/lib/scrambler/csa2.c
dipiradiohead_CSA2_EXTRA_LDFLAGS := -ldl
else
dipiradiohead_CSA2_SRC := src/lib/scrambler/csa2_stub.c
endif

dipiradiohead_EXTRA_LDFLAGS += $(dipiradiohead_CSA2_EXTRA_LDFLAGS)

# ECMG client thread: pthread is a hard dependency from here on, not optional
dipiradiohead_EXTRA_CFLAGS += -pthread
dipiradiohead_EXTRA_LDFLAGS += -pthread

ifeq ($(HAVE_RIST),yes)
dipiradiohead_RIST_SRC := src/lib/net/rist/ristout.c src/lib/net/rist/ristlog.c
dipiradiohead_EXTRA_CFLAGS += $(shell pkg-config --cflags librist)
ifneq (,$(findstring -static,$(LDFLAGS)))
dipiradiohead_EXTRA_LDFLAGS += $(shell pkg-config --static --libs librist)
else
dipiradiohead_EXTRA_LDFLAGS += $(shell pkg-config --libs librist)
endif
else
dipiradiohead_RIST_SRC := src/lib/net/rist/ristout_stub.c src/lib/net/rist/ristlog_stub.c
endif

dipiradiohead_SRCS := \
	src/dipiradiohead/main.c \
	src/dipiradiohead/args.c \
	src/dipiradiohead/radiohead/radiohead.c \
	src/dipiradiohead/radiohead/metrics.c \
	src/dipiradiohead/radiohead/mpts.c \
	src/lib/log.c \
	src/lib/toolmain.c \
	src/lib/argutil.c \
	src/lib/uriparse.c \
	src/lib/signal.c \
	src/lib/metrics/protocol.c \
	src/lib/metrics/export.c \
	src/lib/net/multicast.c \
	src/lib/net/netconnect.c \
	$(dipiradiohead_TLS_SRC) \
	$(dipiradiohead_RIST_SRC) \
	src/lib/net/httpclient/httpclient.c \
	src/lib/net/httpclient/url.c \
	src/lib/net/httpclient/read.c \
	src/lib/net/httpclient/async.c \
	src/lib/ioutil.c \
	src/lib/demux/crc32.c \
	src/lib/demux/bitreader.c \
	src/dipiradiohead/input/playlist.c \
	src/dipiradiohead/input/icy.c \
	src/dipiradiohead/input/id3.c \
	src/dipiradiohead/input/source/open.c \
	src/dipiradiohead/input/source/open_async.c \
	src/dipiradiohead/input/source/frame.c \
	src/dipiradiohead/input/inputset.c \
	src/lib/net/retryset.c \
	src/dipiradiohead/framer/mpegaudio.c \
	src/dipiradiohead/framer/aac_adts.c \
	src/dipiradiohead/framer/aac_latm.c \
	src/dipiradiohead/mux/psi.c \
	src/dipiradiohead/mux/pes.c \
	src/dipiradiohead/mux/tspacketizer.c \
	src/dipiradiohead/cas/cas.c \
	src/lib/mux/cadescbuild.c \
	src/lib/cas/cas_args.c \
	src/lib/cas/simulcrypt_msg.c \
	src/lib/cas/ecmg_client/ecmg_client.c \
	src/lib/cas/ecmg_client/protocol.c \
	src/lib/cas/ecmg_client/connect.c \
	src/lib/cas/ecmg_client/run.c \
	src/lib/cas/emmg_server/emmg_server.c \
	src/lib/cas/emmg_server/protocol.c \
	src/lib/cas/emmg_server/worker.c \
	src/lib/cas/cas_group.c \
	src/lib/cas/cas_scramble_engine.c \
	src/lib/cas/cas_core.c \
	src/lib/mux/rtpheader.c \
	src/lib/mux/psi_build.c \
	src/lib/mux/mpts.c \
	src/lib/mux/tspacket_write.c \
	src/lib/scrambler/scrambler.c \
	$(dipiradiohead_CISSA_SRC) \
	$(dipiradiohead_CSA2_SRC) \
	$(dipiradiohead_BISS_SRC) \
	$(dipiradiohead_BISS_CA_SRC) \
	src/lib/cas/biss/ca_sections.c \
	src/lib/cas/biss/ca_engine.c

ifeq ($(HAVE_OPENSSL),yes)
dipitvhead_EXTRA_CFLAGS := $(shell pkg-config --cflags openssl)
ifneq (,$(findstring -static,$(LDFLAGS)))
dipitvhead_EXTRA_LDFLAGS := $(shell pkg-config --static --libs openssl)
else
dipitvhead_EXTRA_LDFLAGS := $(shell pkg-config --libs openssl)
endif
endif

ifeq ($(HAVE_TLS),yes)
dipitvhead_TLS_SRC := src/lib/net/tls.c
else
dipitvhead_TLS_SRC := src/lib/net/tls_stub.c
ifneq ($(TLS),no)
$(warning dipitvhead: OpenSSL not found via pkg-config, building without HTTPS support)
endif
endif

ifeq ($(HAVE_OPENSSL),yes)
dipitvhead_CISSA_SRC := src/lib/scrambler/cissa.c
dipitvhead_BISS_SRC := src/lib/cas/biss/biss.c src/lib/cas/biss/hex.c
dipitvhead_BISS_CA_SRC := src/lib/cas/biss/ca.c
else
dipitvhead_CISSA_SRC := src/lib/scrambler/cissa_stub.c
dipitvhead_BISS_SRC := src/lib/cas/biss/stub.c src/lib/cas/biss/hex.c
dipitvhead_BISS_CA_SRC := src/lib/cas/biss/ca_stub.c
$(warning dipitvhead: OpenSSL not found via pkg-config, building without CISSA support)
endif

ifeq ($(HAVE_CSA),yes)
dipitvhead_CSA2_SRC := src/lib/scrambler/csa2.c
dipitvhead_CSA2_EXTRA_LDFLAGS := -ldl
else
dipitvhead_CSA2_SRC := src/lib/scrambler/csa2_stub.c
endif

dipitvhead_EXTRA_LDFLAGS += $(dipitvhead_CSA2_EXTRA_LDFLAGS)

# ECMG client thread: pthread is a hard dependency from here on, not optional
dipitvhead_EXTRA_CFLAGS += -pthread
dipitvhead_EXTRA_LDFLAGS += -pthread

ifeq ($(HAVE_RIST),yes)
dipitvhead_RIST_SRC := src/lib/net/rist/ristout.c src/lib/net/rist/ristin.c src/lib/net/rist/ristlog.c
dipitvhead_EXTRA_CFLAGS += $(shell pkg-config --cflags librist)
ifneq (,$(findstring -static,$(LDFLAGS)))
dipitvhead_EXTRA_LDFLAGS += $(shell pkg-config --static --libs librist)
else
dipitvhead_EXTRA_LDFLAGS += $(shell pkg-config --libs librist)
endif
else
dipitvhead_RIST_SRC := src/lib/net/rist/ristout_stub.c src/lib/net/rist/ristin_stub.c src/lib/net/rist/ristlog_stub.c
endif

dipitvhead_SRCS := \
	src/dipitvhead/main.c \
	src/dipitvhead/args.c \
	src/dipitvhead/tvhead/tvhead.c \
	src/dipitvhead/tvhead/discover.c \
	src/dipitvhead/tvhead/output.c \
	src/dipitvhead/tvhead/single.c \
	src/dipitvhead/tvhead/mpts.c \
	src/dipitvhead/tvhead/mpts/retryset_adapter.c \
	src/dipitvhead/tvhead/mpts/discover_feed.c \
	src/dipitvhead/tvhead/mpts/cas_adapter.c \
	src/dipitvhead/input/source.c \
	src/dipitvhead/mux/pmtbuild.c \
	src/dipitvhead/mux/aitbuild.c \
	src/dipitvhead/mux/remux/lifecycle.c \
	src/dipitvhead/mux/remux/psi.c \
	src/dipitvhead/mux/remux/eit.c \
	src/dipitvhead/mux/remux/feed.c \
	src/dipitvhead/mux/bitrate.c \
	src/dipitvhead/cas/cas.c \
	src/lib/mux/cadescbuild.c \
	src/lib/cas/cas_args.c \
	src/lib/cas/simulcrypt_msg.c \
	src/lib/cas/ecmg_client/ecmg_client.c \
	src/lib/cas/ecmg_client/protocol.c \
	src/lib/cas/ecmg_client/connect.c \
	src/lib/cas/ecmg_client/run.c \
	src/lib/cas/emmg_server/emmg_server.c \
	src/lib/cas/emmg_server/protocol.c \
	src/lib/cas/emmg_server/worker.c \
	src/lib/cas/cas_group.c \
	src/lib/cas/cas_scramble_engine.c \
	src/lib/cas/cas_core.c \
	src/lib/log.c \
	src/lib/toolmain.c \
	src/lib/argutil.c \
	src/lib/uriparse.c \
	src/lib/signal.c \
	src/lib/metrics/protocol.c \
	src/lib/metrics/export.c \
	src/lib/net/multicast.c \
	src/lib/net/netconnect.c \
	src/lib/net/tssource.c \
	src/lib/net/retryset.c \
	$(dipitvhead_TLS_SRC) \
	$(dipitvhead_RIST_SRC) \
	src/lib/net/httpclient/httpclient.c \
	src/lib/net/httpclient/url.c \
	src/lib/net/httpclient/read.c \
	src/lib/net/httpclient/async.c \
	src/lib/ioutil.c \
	src/lib/demux/crc32.c \
	src/lib/demux/psi/psi.c \
	src/lib/demux/psi/parse.c \
	src/lib/demux/psi/descriptors.c \
	src/lib/demux/psi/section_asm.c \
	src/lib/demux/tspack.c \
	src/lib/demux/rtp.c \
	src/lib/mux/rtpheader.c \
	src/lib/mux/psi_build.c \
	src/lib/mux/mpts.c \
	src/lib/mux/tspacket_write.c \
	src/lib/scrambler/scrambler.c \
	$(dipitvhead_CISSA_SRC) \
	$(dipitvhead_CSA2_SRC) \
	$(dipitvhead_BISS_SRC) \
	$(dipitvhead_BISS_CA_SRC) \
	src/lib/cas/biss/ca_sections.c \
	src/lib/cas/biss/ca_engine.c

TOOLS += dipifccret
dipifccret_EXTRA_CFLAGS := -pthread
# channel.c's lock-free ring buffers use C11 64-bit atomics; platforms without
# a native 8-byte atomic instruction (32-bit ARM) need libatomic's runtime
# helpers (__atomic_load_8/__atomic_store_8). Safe unconditionally: this
# project only targets Linux, where libatomic ships alongside libgcc on
# every GCC/Clang toolchain.
dipifccret_EXTRA_LDFLAGS := -pthread -latomic
dipifccret_SRCS := \
	src/dipifccret/main.c \
	src/dipifccret/run/dispatch.c \
	src/dipifccret/run/pacer.c \
	src/dipifccret/run/rsi.c \
	src/dipifccret/run/metrics.c \
	src/dipifccret/args.c \
	src/dipifccret/capture/capture.c \
	src/dipifccret/capture/ranges.c \
	src/dipifccret/capture/bpf.c \
	src/dipifccret/capture/frame.c \
	src/dipifccret/channel/channel.c \
	src/dipifccret/channel/hash.c \
	src/dipifccret/channel/ring.c \
	src/dipifccret/listen.c \
	src/dipifccret/ret/ret.c \
	src/dipifccret/ret/rtx_session_table.c \
	src/dipifccret/ret/mcsend.c \
	src/dipifccret/fcc/burst.c \
	src/dipifccret/fcc/burst_table.c \
	src/lib/metrics/protocol.c \
	src/lib/metrics/export.c \
	src/lib/log.c \
	src/lib/argutil.c \
	src/lib/signal.c \
	src/lib/ioutil.c \
	src/lib/net/sockaddr_index.c \
	src/lib/net/multicast.c \
	src/lib/net/netconnect.c \
	src/lib/demux/rtp.c \
	src/lib/demux/rtcp.c \
	src/lib/demux/psi/psi.c \
	src/lib/demux/psi/parse.c \
	src/lib/demux/psi/descriptors.c \
	src/lib/demux/psi/section_asm.c \
	src/lib/demux/tspack.c \
	src/lib/demux/crc32.c \
	src/lib/mux/rtcp_build.c \
	src/lib/mux/rtx.c

ifeq ($(HAVE_OPENSSL),yes)
TOOLS += dipicam378
dipicam378_EXTRA_CFLAGS := -pthread $(shell pkg-config --cflags openssl)
ifneq (,$(findstring -static,$(LDFLAGS)))
dipicam378_EXTRA_LDFLAGS := -pthread $(shell pkg-config --static --libs openssl)
else
dipicam378_EXTRA_LDFLAGS := -pthread $(shell pkg-config --libs openssl)
endif
dipicam378_SRCS := \
	src/dipicam378/main.c \
	src/dipicam378/args.c \
	src/dipicam378/cs378x/cs378x.c \
	src/dipicam378/cs378x/crypto.c \
	src/dipicam378/cs378x/protocol.c \
	src/dipicam378/cs378x/worker.c \
	src/dipicam378/device.c \
	src/lib/cas/device_crypto.c \
	src/lib/cas/device_state_core.c \
	src/lib/metrics/protocol.c \
	src/lib/metrics/export.c \
	src/lib/net/netconnect.c \
	src/lib/log.c \
	src/lib/argutil.c \
	src/lib/ioutil.c \
	src/lib/signal.c \
	src/lib/secure_zero.c
else
$(warning dipicam378: OpenSSL not found via pkg-config, skipping this tool entirely (RSA/AES crypto is its whole purpose))
endif

ifeq ($(HAVE_OPENSSL),yes)
TOOLS += dipidescramble
dipidescramble_EXTRA_CFLAGS := $(shell pkg-config --cflags openssl)
ifneq (,$(findstring -static,$(LDFLAGS)))
dipidescramble_EXTRA_LDFLAGS := $(shell pkg-config --static --libs openssl)
else
dipidescramble_EXTRA_LDFLAGS := $(shell pkg-config --libs openssl)
endif

ifeq ($(HAVE_CSA),yes)
dipidescramble_CSA2_SRC := src/lib/scrambler/csa2.c
dipidescramble_EXTRA_LDFLAGS += -ldl
else
dipidescramble_CSA2_SRC := src/lib/scrambler/csa2_stub.c
endif

ifeq ($(HAVE_RIST),yes)
dipidescramble_RIST_SRC := src/lib/net/rist/ristin.c src/lib/net/rist/ristlog.c
dipidescramble_EXTRA_CFLAGS += $(shell pkg-config --cflags librist)
ifneq (,$(findstring -static,$(LDFLAGS)))
dipidescramble_EXTRA_LDFLAGS += $(shell pkg-config --static --libs librist)
else
dipidescramble_EXTRA_LDFLAGS += $(shell pkg-config --libs librist)
endif
else
dipidescramble_RIST_SRC := src/lib/net/rist/ristin_stub.c src/lib/net/rist/ristlog_stub.c
endif
dipidescramble_EXTRA_CFLAGS += -pthread
dipidescramble_EXTRA_LDFLAGS += -pthread

dipidescramble_SRCS := \
	src/dipidescramble/main.c \
	src/dipidescramble/pipeline.c \
	src/dipidescramble/args.c \
	src/dipidescramble/crypto.c \
	src/dipidescramble/device.c \
	src/dipidescramble/ecm_profile/common.c \
	src/dipidescramble/ecm_profile/parse.c \
	src/dipidescramble/ecm_profile/validate.c \
	src/dipidescramble/ecm_profile/wire.c \
	src/dipidescramble/ecm_profile/crypto.c \
	src/dipidescramble/biss_ca_state.c \
	src/dipidescramble/emmcache.c \
	src/dipidescramble/ipiclient.c \
	src/lib/cas/device_crypto.c \
	src/lib/cas/device_state_core.c \
	src/lib/metrics/protocol.c \
	src/lib/metrics/export.c \
	src/lib/log.c \
	src/lib/argutil.c \
	src/lib/uriparse.c \
	src/lib/signal.c \
	src/lib/secure_zero.c \
	src/lib/net/multicast.c \
	src/lib/net/netconnect.c \
	src/lib/net/tssource.c \
	src/lib/net/tls.c \
	src/lib/net/httpclient/httpclient.c \
	src/lib/net/httpclient/url.c \
	src/lib/net/httpclient/read.c \
	src/lib/net/httpclient/async.c \
	src/lib/ioutil.c \
	src/lib/demux/crc32.c \
	src/lib/demux/psi/psi.c \
	src/lib/demux/psi/parse.c \
	src/lib/demux/psi/descriptors.c \
	src/lib/demux/psi/section_asm.c \
	src/lib/demux/rtp.c \
	src/lib/demux/tspack.c \
	src/lib/demux/pes.c \
	src/lib/demux/mpts_probe.c \
	src/lib/mux/ebml.c \
	src/lib/mux/mkv/mkv.c \
	src/lib/demux/bitreader.c \
	src/lib/demux/escodec/aubuild.c \
	src/lib/demux/escodec/audio.c \
	src/lib/demux/escodec/video.c \
	src/lib/mux/mkv/video.c \
	src/lib/mux/mkv/write.c \
	src/lib/mux/mkv/feed.c \
	src/lib/mux/teletext.c \
	src/lib/mux/psi_build.c \
	src/lib/mux/amf.c \
	src/lib/mux/flv/flv.c \
	src/lib/mux/flv/feed.c \
	src/lib/mux/flv/write.c \
	src/lib/net/rtmp/handshake.c \
	src/lib/net/rtmp/chunk.c \
	src/lib/net/rtmp/session.c \
	src/lib/net/rtmp/command.c \
	src/lib/net/rtmp/rtmp.c \
	src/lib/net/rtmp/rtmpout.c \
	src/lib/net/rtmp/auth.c \
	src/lib/scrambler/scrambler.c \
	src/lib/scrambler/cissa.c \
	src/lib/cas/biss/biss.c \
	src/lib/cas/biss/hex.c \
	src/lib/cas/biss/ca.c \
	src/lib/cas/biss/ca_sections.c \
	$(dipidescramble_CSA2_SRC) \
	$(dipidescramble_RIST_SRC)
else
$(warning dipidescramble: OpenSSL not found via pkg-config, skipping this tool entirely (RSA/AES crypto is its whole purpose))
endif

ifeq ($(HAVE_RIST),yes)
TOOLS += dipirist
dipirist_EXTRA_CFLAGS := $(shell pkg-config --cflags librist)
ifneq (,$(findstring -static,$(LDFLAGS)))
dipirist_EXTRA_LDFLAGS := $(shell pkg-config --static --libs librist)
else
dipirist_EXTRA_LDFLAGS := $(shell pkg-config --libs librist)
endif

DIPIRIST_AVGBUF_TEST_SRC := '\#include <librist/librist.h>\nint main(void){struct rist_stats_receiver_flow f;return (int)f.avg_buffer_time;}\n'
HAVE_AVG_BUFFER_TIME := $(shell printf $(DIPIRIST_AVGBUF_TEST_SRC) | $(CC) -xc - $(dipirist_EXTRA_CFLAGS) -o /dev/null >/dev/null 2>&1 && echo yes)
ifeq ($(HAVE_AVG_BUFFER_TIME),yes)
dipirist_EXTRA_CFLAGS += -DDIPIRIST_HAVE_AVG_BUFFER_TIME
endif

DIPIRIST_LIBRIST_VERSION := $(shell pkg-config --modversion librist)
DIPIRIST_LIBRIST_IPV6_WARN := $(shell dpkg --compare-versions '$(DIPIRIST_LIBRIST_VERSION)' le '0.2.20' && echo yes)
ifeq ($(DIPIRIST_LIBRIST_IPV6_WARN),yes)
dipirist_EXTRA_CFLAGS += -DDIPIRIST_LIBRIST_IPV6_WARN
endif

ifeq ($(HAVE_TLS),yes)
dipirist_TLS_SRC := src/lib/net/tls.c
dipirist_EXTRA_CFLAGS += $(shell pkg-config --cflags openssl)
ifneq (,$(findstring -static,$(LDFLAGS)))
dipirist_EXTRA_LDFLAGS += $(shell pkg-config --static --libs openssl)
else
dipirist_EXTRA_LDFLAGS += $(shell pkg-config --libs openssl)
endif
else
dipirist_TLS_SRC := src/lib/net/tls_stub.c
endif

dipirist_SRCS := \
	src/dipirist/main.c \
	src/dipirist/args.c \
	src/dipirist/bridge.c \
	src/lib/log.c \
	src/lib/argutil.c \
	src/lib/uriparse.c \
	src/lib/ioutil.c \
	src/lib/signal.c \
	src/lib/net/multicast.c \
	src/lib/net/netconnect.c \
	src/lib/net/tssource.c \
	src/lib/net/tssink.c \
	src/lib/net/rist/ristout.c \
	src/lib/net/rist/ristin.c \
	src/lib/net/rist/ristlog.c \
	$(dipirist_TLS_SRC) \
	src/lib/net/httpclient/httpclient.c \
	src/lib/net/httpclient/url.c \
	src/lib/net/httpclient/read.c \
	src/lib/net/httpclient/async.c \
	src/lib/demux/rtp.c \
	src/lib/mux/rtpheader.c \
	src/lib/metrics/protocol.c \
	src/lib/metrics/export.c
else
$(warning dipirist: librist not found via pkg-config, skipping this tool entirely (RIST support is its whole purpose))
endif

ifeq ($(HAVE_SRT),yes)
TOOLS += dipisrt
dipisrt_EXTRA_CFLAGS := $(shell pkg-config --cflags srt)
ifneq (,$(findstring -static,$(LDFLAGS)))
dipisrt_EXTRA_LDFLAGS := $(shell pkg-config --static --libs srt)
else
dipisrt_EXTRA_LDFLAGS := $(shell pkg-config --libs srt)
endif

DIPISRT_BONDING_TEST_SRC := '\#include <srt/srt.h>\nint main(void){int rc;srt_startup();rc=srt_create_group(SRT_GTYPE_BROADCAST);srt_cleanup();return rc<0?1:0;}\n'
HAVE_SRT_BONDING := $(shell probe=$$(mktemp); printf $(DIPISRT_BONDING_TEST_SRC) | $(CC) -xc - $(dipisrt_EXTRA_CFLAGS) $$(pkg-config --libs srt) -o $$probe 2>/dev/null && $$probe >/dev/null 2>&1 && echo yes; rm -f $$probe)
ifeq ($(HAVE_SRT_BONDING),yes)
dipisrt_EXTRA_CFLAGS += -DDIPISRT_HAVE_BONDING
else
$(warning dipisrt: linked libsrt has no bonding support (ENABLE_BONDING=OFF), --group-mode disabled)
endif

ifeq ($(HAVE_TLS),yes)
dipisrt_TLS_SRC := src/lib/net/tls.c
dipisrt_EXTRA_CFLAGS += $(shell pkg-config --cflags openssl)
ifneq (,$(findstring -static,$(LDFLAGS)))
dipisrt_EXTRA_LDFLAGS += $(shell pkg-config --static --libs openssl)
else
dipisrt_EXTRA_LDFLAGS += $(shell pkg-config --libs openssl)
endif
else
dipisrt_TLS_SRC := src/lib/net/tls_stub.c
endif

ifeq ($(HAVE_RIST),yes)
dipisrt_RIST_SRC := src/lib/net/rist/ristin.c src/lib/net/rist/ristlog.c
dipisrt_EXTRA_CFLAGS += $(shell pkg-config --cflags librist)
ifneq (,$(findstring -static,$(LDFLAGS)))
dipisrt_EXTRA_LDFLAGS += $(shell pkg-config --static --libs librist)
else
dipisrt_EXTRA_LDFLAGS += $(shell pkg-config --libs librist)
endif
else
dipisrt_RIST_SRC := src/lib/net/rist/ristin_stub.c src/lib/net/rist/ristlog_stub.c
endif

dipisrt_SRCS := \
	src/dipisrt/main.c \
	src/dipisrt/args.c \
	src/dipisrt/bridge.c \
	src/lib/log.c \
	src/lib/argutil.c \
	src/lib/uriparse.c \
	src/lib/ioutil.c \
	src/lib/signal.c \
	src/lib/net/multicast.c \
	src/lib/net/netconnect.c \
	src/lib/net/tssource.c \
	src/lib/net/tssink.c \
	$(dipisrt_RIST_SRC) \
	src/lib/net/srt/srtcommon.c \
	src/lib/net/srt/srtout.c \
	src/lib/net/srt/srtin.c \
	$(dipisrt_TLS_SRC) \
	src/lib/net/httpclient/httpclient.c \
	src/lib/net/httpclient/url.c \
	src/lib/net/httpclient/read.c \
	src/lib/net/httpclient/async.c \
	src/lib/demux/rtp.c \
	src/lib/mux/rtpheader.c \
	src/lib/metrics/protocol.c \
	src/lib/metrics/export.c
else
$(warning dipisrt: libsrt not found via pkg-config, skipping this tool entirely (SRT support is its whole purpose))
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

# dvbipitools: multicall binary, all $(TOOLS) merged, dispatch by argv[0]/subcommand.
# separate object tree (build/dvbipitools/...): source-adjacent %.o would collide
# with standalone tools' own objects under differing -D renames below.
DVBIPITOOLS_TOOLS := $(TOOLS)

DVBIPITOOLS_SRCS := $(sort $(foreach t,$(DVBIPITOOLS_TOOLS),$($(t)_SRCS)) src/dvbipitools/main.c)

# dipiscan always stubs TLS, other tools follow global HAVE_TLS: pick 1 variant.
DVBIPITOOLS_SRCS := $(filter-out src/lib/net/tls.c src/lib/net/tls_stub.c,$(DVBIPITOOLS_SRCS))
ifeq ($(HAVE_TLS),yes)
DVBIPITOOLS_SRCS += src/lib/net/tls.c
else
DVBIPITOOLS_SRCS += src/lib/net/tls_stub.c
endif

DVBIPITOOLS_OBJS := $(patsubst src/%.c,build/dvbipitools/src/%.o,$(DVBIPITOOLS_SRCS))
ALL_OBJS += $(DVBIPITOOLS_OBJS)

DVBIPITOOLS_EXTRA_CFLAGS := $(sort $(foreach t,$(DVBIPITOOLS_TOOLS),$($(t)_EXTRA_CFLAGS)))
DVBIPITOOLS_EXTRA_LDFLAGS := $(sort $(foreach t,$(DVBIPITOOLS_TOOLS),$($(t)_EXTRA_LDFLAGS)))
$(DVBIPITOOLS_OBJS): CFLAGS += $(DVBIPITOOLS_EXTRA_CFLAGS)

# main()/args_parse() collide across all 13 tools once linked together, plus a
# few same-named helpers between 2 unrelated tools (mcast_describe, cas_*, ...).
# renamed per tool, scoped to that tool's own build/dvbipitools/src/<tool>/
# objects only: differing renames of one name across tools must not both land
# on a shared src/lib/ object.
dvbipitools_dipibcg_DEFS := -Dmain=dipibcg_main -Dargs_parse=dipibcg_args_parse
dvbipitools_dipibim_DEFS := -Dmain=dipibim_main -Dargs_parse=dipibim_args_parse
dvbipitools_dipicam378_DEFS := -Dmain=dipicam378_main -Dargs_parse=dipicam378_args_parse \
	-Daccept_main=dipicam378_accept_main
dvbipitools_dipidescramble_DEFS := -Dmain=dipidescramble_main -Dargs_parse=dipidescramble_args_parse \
	-Ddevice_on_emm=dipidescramble_device_on_emm -Ddevice_resolve_cw=dipidescramble_device_resolve_cw \
	-Ddevice_state_free=dipidescramble_device_state_free -Ddevice_state_new=dipidescramble_device_state_new \
	-Dout_describe=dipidescramble_out_describe
dvbipitools_dipifccret_DEFS := -Dmain=dipifccret_main -Dargs_parse=dipifccret_args_parse
dvbipitools_dipimetrics_DEFS := -Dmain=dipimetrics_main -Dargs_parse=dipimetrics_args_parse
dvbipitools_dipiradiohead_DEFS := -Dmain=dipiradiohead_main -Dargs_parse=dipiradiohead_args_parse \
	-Dcodec_name=dipiradiohead_codec_name -Dmcast_describe=dipiradiohead_mcast_describe
dvbipitools_dipirec_DEFS := -Dmain=dipirec_main -Dargs_parse=dipirec_args_parse \
	-Drtmp_fanout_cb=dipirec_rtmp_fanout_cb
dvbipitools_dipirist_DEFS := -Dmain=dipirist_main -Dargs_parse=dipirist_args_parse \
	-Dconfig_is_sender=dipirist_config_is_sender -Dendpoint_describe=dipirist_endpoint_describe \
	-Dbridge_run=dipirist_bridge_run
dvbipitools_dipisrt_DEFS := -Dmain=dipisrt_main -Dargs_parse=dipisrt_args_parse \
	-Dconfig_is_sender=dipisrt_config_is_sender -Dendpoint_describe=dipisrt_endpoint_describe \
	-Dbridge_run=dipisrt_bridge_run
dvbipitools_dipiscan_DEFS := -Dmain=dipiscan_main -Dargs_parse=dipiscan_args_parse
dvbipitools_dipisds_DEFS := -Dmain=dipisds_main -Dargs_parse=dipisds_args_parse \
	-Dannounce_run=dipisds_announce_run -Dlisten_run=dipisds_listen_run \
	-Dmcast_describe=dipisds_mcast_describe
dvbipitools_dipitvhead_DEFS := -Dmain=dipitvhead_main -Dargs_parse=dipitvhead_args_parse \
	-Dcas_start=dipitvhead_cas_start -Dcas_stop=dipitvhead_cas_stop -Dcas_failed=dipitvhead_cas_failed \
	-Dcas_scramble_packet=dipitvhead_cas_scramble_packet -Dcas_flush=dipitvhead_cas_flush \
	-Dcas_get_metrics=dipitvhead_cas_get_metrics -Dcas_vendor_metrics=dipitvhead_cas_vendor_metrics \
	-Dcas_vendor_super_cas_id=dipitvhead_cas_vendor_super_cas_id -Dcas_prog_desc=dipitvhead_cas_prog_desc \
	-Dcas_build_cat=dipitvhead_cas_build_cat -Dcas_vendor_count=dipitvhead_cas_vendor_count \
	-Dcas_vendor_ecm_pid=dipitvhead_cas_vendor_ecm_pid -Dcas_vendor_emm_pid=dipitvhead_cas_vendor_emm_pid \
	-Dcas_vendor_ecm_due=dipitvhead_cas_vendor_ecm_due -Dcas_vendor_next_emm=dipitvhead_cas_vendor_next_emm \
	-Dcas_reload_receivers=dipitvhead_cas_reload_receivers -Demit_metrics=dipitvhead_emit_metrics \
	-Dflush_batch=dipitvhead_flush_batch -Dpacket_cb=dipitvhead_packet_cb \
	-Dmcast_describe=dipitvhead_mcast_describe -Dsource_describe=dipitvhead_source_describe
dvbipitools_dipixmltv_DEFS := -Dmain=dipixmltv_main -Dargs_parse=dipixmltv_args_parse

define DVBIPITOOLS_TOOLDIR_template
build/dvbipitools/src/$(1)/%.o: DVBIPITOOLS_DEFS := $$(dvbipitools_$(1)_DEFS)
endef
$(foreach t,$(DVBIPITOOLS_TOOLS),$(eval $(call DVBIPITOOLS_TOOLDIR_template,$(t))))

DVBIPITOOLS_FEATURE_DEFS :=
ifneq (,$(filter dipicam378,$(DVBIPITOOLS_TOOLS)))
DVBIPITOOLS_FEATURE_DEFS += -DDVBIPITOOLS_HAVE_CAM378
endif
ifneq (,$(filter dipidescramble,$(DVBIPITOOLS_TOOLS)))
DVBIPITOOLS_FEATURE_DEFS += -DDVBIPITOOLS_HAVE_DESCRAMBLE
endif
ifneq (,$(filter dipirist,$(DVBIPITOOLS_TOOLS)))
DVBIPITOOLS_FEATURE_DEFS += -DDVBIPITOOLS_HAVE_RIST
endif
ifneq (,$(filter dipisrt,$(DVBIPITOOLS_TOOLS)))
DVBIPITOOLS_FEATURE_DEFS += -DDVBIPITOOLS_HAVE_SRT
endif
build/dvbipitools/src/dvbipitools/main.o: DVBIPITOOLS_DEFS := $(DVBIPITOOLS_FEATURE_DEFS)

build/dvbipitools/src/%.o: src/%.c config.mk
	@mkdir -p $(dir $@)
	$(CC) $(CFLAGS) $(DVBIPITOOLS_DEFS) -MMD -MP -c $< -o $@

dvbipitools: $(DVBIPITOOLS_OBJS)
	$(CC) $^ $(LDFLAGS) $(DVBIPITOOLS_EXTRA_LDFLAGS) -o $@

UNIT_TESTS := lib_demux_crc32 lib_demux_rtcp lib_demux_psi lib_demux_psi_section_asm lib_demux_bitreader lib_demux_rtp lib_demux_rtx lib_demux_tspack lib_demux_pes \
	lib_demux_mpts_probe \
	lib_mux_psi_build lib_mux_rtpheader lib_mux_rtx lib_mux_rtcp_build lib_mux_tspacket_write \
	lib_mux_ebml lib_mux_teletext lib_mux_mkv lib_mux_flv lib_mux_cadescbuild \
	lib_net_netconnect lib_net_rtmp lib_net_rtmpout lib_net_httpclient_async lib_net_tssource_async lib_net_tssource_file lib_net_retryset lib_net_dvbstp \
	lib_mux_mpts \
	lib_cas_cas_group \
	lib_bim_bitwriter lib_bim_bitreader lib_bim_strrepo lib_bim_codec \
	lib_xml_util lib_tva_timefmt lib_tva_bcg_doc lib_tva_mapping lib_tva_xmltv lib_tva_tva_xml \
	lib_bim_fragment lib_bim_accessunit \
	lib_sds_xml dipibim_args dipiscan_format dipiscan_scan dipixmltv_args dipixmltv_revmap dipixmltv_suggest \
	dipiradiohead_mpegaudio dipiradiohead_aac_adts dipiradiohead_aac_latm \
	dipiradiohead_psi dipiradiohead_id3 dipiradiohead_pes dipiradiohead_tspacketizer dipiradiohead_radiohead dipiradiohead_cas dipiradiohead_args \
	dipiradiohead_source_async dipiradiohead_inputset \
	dipitvhead_source dipitvhead_args dipitvhead_discover dipitvhead_output dipitvhead_pmtbuild dipitvhead_aitbuild dipitvhead_bitrate dipitvhead_remux \
	dipitvhead_simulcrypt_msg dipitvhead_ecmg_client dipitvhead_emmg_server dipitvhead_cas \
	dipirec_ts_filter dipirec_pace dipirec_ret_client dipirec_record dipirec_args \
	dipifccret_args dipifccret_listen dipifccret_channel dipifccret_ret_mcsend dipifccret_burst dipifccret_burst_table dipifccret_ret dipifccret_rtx_session_table dipifccret_capture \
	lib_metrics_protocol lib_metrics_export \
	dipimetrics_args dipimetrics_store dipimetrics_render dipimetrics_httpserver \
	dipibcg_container dipibcg_args dipibcg_announce dipibcg_listen \
	dipicam378_args \
	dipisds_args dipisds_input dipisds_format_out dipisds_announce dipisds_listen \
	dipirist_args dipisrt_args

ifeq ($(HAVE_OPENSSL),yes)
UNIT_TESTS += lib_scrambler_cissa
lib_scrambler_cissa_BIN := tests/unit/lib/scrambler/test_cissa
lib_scrambler_cissa_SRCS := \
	tests/unit/lib/scrambler/test_cissa.c \
	src/lib/scrambler/scrambler.c \
	src/lib/scrambler/cissa.c \
	src/lib/scrambler/csa2_stub.c \
	src/lib/log.c
lib_scrambler_cissa_EXTRA_CFLAGS := $(shell pkg-config --cflags openssl)
lib_scrambler_cissa_EXTRA_LDFLAGS := $(shell pkg-config --libs openssl)

UNIT_TESTS += lib_cas_biss
lib_cas_biss_BIN := tests/unit/lib/cas/test_biss
lib_cas_biss_SRCS := \
	tests/unit/lib/cas/test_biss.c \
	src/lib/cas/biss/biss.c \
	src/lib/cas/biss/hex.c
lib_cas_biss_EXTRA_CFLAGS := $(shell pkg-config --cflags openssl)
lib_cas_biss_EXTRA_LDFLAGS := $(shell pkg-config --libs openssl)

UNIT_TESTS += lib_cas_biss_ca
lib_cas_biss_ca_BIN := tests/unit/lib/cas/test_biss_ca
lib_cas_biss_ca_SRCS := \
	tests/unit/lib/cas/test_biss_ca.c \
	src/lib/cas/biss/ca.c
lib_cas_biss_ca_EXTRA_CFLAGS := $(shell pkg-config --cflags openssl)
lib_cas_biss_ca_EXTRA_LDFLAGS := $(shell pkg-config --libs openssl)

UNIT_TESTS += lib_cas_biss_ca_engine
lib_cas_biss_ca_engine_BIN := tests/unit/lib/cas/test_biss_ca_engine
lib_cas_biss_ca_engine_SRCS := \
	tests/unit/lib/cas/test_biss_ca_engine.c \
	src/lib/cas/biss/ca_engine.c \
	src/lib/cas/biss/ca.c \
	src/lib/cas/biss/ca_sections.c \
	src/lib/cas/cas_scramble_engine.c \
	src/lib/scrambler/scrambler.c \
	src/lib/scrambler/cissa.c \
	src/lib/scrambler/csa2_stub.c \
	src/lib/mux/cadescbuild.c \
	src/lib/mux/psi_build.c \
	src/lib/demux/crc32.c \
	src/lib/log.c
lib_cas_biss_ca_engine_EXTRA_CFLAGS := $(shell pkg-config --cflags openssl)
lib_cas_biss_ca_engine_EXTRA_LDFLAGS := $(shell pkg-config --libs openssl)
else
$(warning tests: OpenSSL not found via pkg-config, skipping lib_scrambler_cissa/lib_cas_biss/lib_cas_biss_ca/lib_cas_biss_ca_engine unit tests)
endif

UNIT_TESTS += lib_cas_biss_ca_sections
lib_cas_biss_ca_sections_BIN := tests/unit/lib/cas/test_biss_ca_sections
lib_cas_biss_ca_sections_SRCS := \
	tests/unit/lib/cas/test_biss_ca_sections.c \
	src/lib/cas/biss/ca_sections.c \
	src/lib/mux/psi_build.c \
	src/lib/demux/crc32.c

ifeq ($(HAVE_ZLIB),yes)
UNIT_TESTS += dipibcg_wrapper
dipibcg_wrapper_BIN := tests/unit/dipibcg/test_wrapper
dipibcg_wrapper_SRCS := \
	tests/unit/dipibcg/test_wrapper.c \
	src/dipibcg/wrapper.c
dipibcg_wrapper_EXTRA_CFLAGS := $(shell pkg-config --cflags zlib)
dipibcg_wrapper_EXTRA_LDFLAGS := $(shell pkg-config --libs zlib)
else
$(warning tests: zlib not found via pkg-config, skipping dipibcg_wrapper unit test)
endif

ifeq ($(HAVE_DVBCSA),yes)
UNIT_TESTS += lib_scrambler_csa2
lib_scrambler_csa2_BIN := tests/unit/lib/scrambler/test_csa2
lib_scrambler_csa2_SRCS := \
	tests/unit/lib/scrambler/test_csa2.c \
	src/lib/scrambler/scrambler.c \
	src/lib/scrambler/csa2.c \
	src/lib/scrambler/cissa_stub.c \
	src/lib/log.c
lib_scrambler_csa2_EXTRA_LDFLAGS := -ldvbcsa -ldl
else
$(warning tests: libdvbcsa not found, skipping lib_scrambler_csa2 unit test)
endif

dipicam378_args_BIN := tests/unit/dipicam378/test_args
dipicam378_args_SRCS := \
	tests/unit/dipicam378/test_args.c \
	src/dipicam378/args.c \
	src/lib/argutil.c \
	src/lib/log.c

ifeq ($(HAVE_OPENSSL),yes)
UNIT_TESTS += dipicam378_crypto
dipicam378_crypto_BIN := tests/unit/dipicam378/test_crypto
dipicam378_crypto_SRCS := \
	tests/unit/dipicam378/test_crypto.c \
	src/lib/cas/device_crypto.c \
	src/lib/secure_zero.c
dipicam378_crypto_EXTRA_CFLAGS := $(shell pkg-config --cflags openssl)
dipicam378_crypto_EXTRA_LDFLAGS := $(shell pkg-config --libs openssl)

UNIT_TESTS += dipicam378_device
dipicam378_device_BIN := tests/unit/dipicam378/test_device
dipicam378_device_SRCS := \
	tests/unit/dipicam378/test_device.c \
	src/dipicam378/device.c \
	src/lib/cas/device_crypto.c \
	src/lib/cas/device_state_core.c \
	src/lib/log.c \
	src/lib/secure_zero.c
dipicam378_device_EXTRA_CFLAGS := $(shell pkg-config --cflags openssl)
dipicam378_device_EXTRA_LDFLAGS := $(shell pkg-config --libs openssl)

UNIT_TESTS += dipicam378_cs378x
dipicam378_cs378x_BIN := tests/unit/dipicam378/test_cs378x
dipicam378_cs378x_SRCS := \
	tests/unit/dipicam378/test_cs378x.c \
	src/dipicam378/cs378x/cs378x.c \
	src/dipicam378/cs378x/crypto.c \
	src/dipicam378/cs378x/protocol.c \
	src/dipicam378/cs378x/worker.c \
	src/lib/log.c \
	src/lib/signal.c
dipicam378_cs378x_EXTRA_CFLAGS := -pthread $(shell pkg-config --cflags openssl)
dipicam378_cs378x_EXTRA_LDFLAGS := -pthread $(shell pkg-config --libs openssl)

UNIT_TESTS += dipidescramble_crypto
dipidescramble_crypto_BIN := tests/unit/dipidescramble/test_crypto
dipidescramble_crypto_SRCS := \
	tests/unit/dipidescramble/test_crypto.c \
	src/dipidescramble/crypto.c \
	src/lib/cas/device_crypto.c \
	src/lib/secure_zero.c
dipidescramble_crypto_EXTRA_CFLAGS := $(shell pkg-config --cflags openssl)
dipidescramble_crypto_EXTRA_LDFLAGS := $(shell pkg-config --libs openssl)

UNIT_TESTS += dipidescramble_device
dipidescramble_device_BIN := tests/unit/dipidescramble/test_device
dipidescramble_device_SRCS := \
	tests/unit/dipidescramble/test_device.c \
	src/dipidescramble/device.c \
	src/dipidescramble/crypto.c \
	src/dipidescramble/ecm_profile/common.c \
	src/dipidescramble/ecm_profile/parse.c \
	src/dipidescramble/ecm_profile/validate.c \
	src/dipidescramble/ecm_profile/wire.c \
	src/dipidescramble/ecm_profile/crypto.c \
	src/lib/cas/device_crypto.c \
	src/lib/cas/device_state_core.c \
	src/lib/log.c \
	src/lib/argutil.c \
	src/lib/secure_zero.c
dipidescramble_device_EXTRA_CFLAGS := $(shell pkg-config --cflags openssl)
dipidescramble_device_EXTRA_LDFLAGS := $(shell pkg-config --libs openssl)

UNIT_TESTS += dipidescramble_ecm_profile
dipidescramble_ecm_profile_BIN := tests/unit/dipidescramble/test_ecm_profile
dipidescramble_ecm_profile_SRCS := \
	tests/unit/dipidescramble/test_ecm_profile.c \
	src/dipidescramble/ecm_profile/common.c \
	src/dipidescramble/ecm_profile/parse.c \
	src/dipidescramble/ecm_profile/validate.c \
	src/dipidescramble/ecm_profile/wire.c \
	src/dipidescramble/ecm_profile/crypto.c \
	src/dipidescramble/crypto.c \
	src/lib/cas/device_crypto.c \
	src/lib/log.c \
	src/lib/argutil.c \
	src/lib/secure_zero.c
dipidescramble_ecm_profile_EXTRA_CFLAGS := $(shell pkg-config --cflags openssl)
dipidescramble_ecm_profile_EXTRA_LDFLAGS := $(shell pkg-config --libs openssl)

UNIT_TESTS += dipidescramble_biss_ca_state
dipidescramble_biss_ca_state_BIN := tests/unit/dipidescramble/test_biss_ca_state
dipidescramble_biss_ca_state_SRCS := \
	tests/unit/dipidescramble/test_biss_ca_state.c \
	src/dipidescramble/biss_ca_state.c \
	src/lib/cas/biss/ca.c \
	src/lib/cas/biss/ca_sections.c \
	src/lib/mux/psi_build.c \
	src/lib/demux/crc32.c
dipidescramble_biss_ca_state_EXTRA_CFLAGS := $(shell pkg-config --cflags openssl)
dipidescramble_biss_ca_state_EXTRA_LDFLAGS := $(shell pkg-config --libs openssl)

UNIT_TESTS += dipidescramble_pipeline
dipidescramble_pipeline_BIN := tests/unit/dipidescramble/test_pipeline
dipidescramble_pipeline_SRCS := \
	tests/unit/dipidescramble/test_pipeline.c \
	src/dipidescramble/pipeline.c \
	src/dipidescramble/device.c \
	src/dipidescramble/crypto.c \
	src/dipidescramble/ecm_profile/common.c \
	src/dipidescramble/ecm_profile/parse.c \
	src/dipidescramble/ecm_profile/validate.c \
	src/dipidescramble/ecm_profile/wire.c \
	src/dipidescramble/ecm_profile/crypto.c \
	src/dipidescramble/biss_ca_state.c \
	src/dipidescramble/emmcache.c \
	src/dipidescramble/ipiclient.c \
	src/lib/cas/device_crypto.c \
	src/lib/cas/device_state_core.c \
	src/lib/log.c \
	src/lib/argutil.c \
	src/lib/ioutil.c \
	src/lib/signal.c \
	src/lib/secure_zero.c \
	src/lib/demux/crc32.c \
	src/lib/demux/psi/psi.c \
	src/lib/demux/psi/parse.c \
	src/lib/demux/psi/descriptors.c \
	src/lib/demux/psi/section_asm.c \
	src/lib/demux/tspack.c \
	src/lib/demux/pes.c \
	src/lib/mux/ebml.c \
	src/lib/mux/teletext.c \
	src/lib/mux/psi_build.c \
	src/lib/mux/mkv/mkv.c \
	src/lib/demux/bitreader.c \
	src/lib/demux/escodec/aubuild.c \
	src/lib/demux/escodec/audio.c \
	src/lib/demux/escodec/video.c \
	src/lib/mux/mkv/video.c \
	src/lib/mux/mkv/write.c \
	src/lib/mux/mkv/feed.c \
	src/lib/mux/amf.c \
	src/lib/mux/flv/flv.c \
	src/lib/mux/flv/feed.c \
	src/lib/mux/flv/write.c \
	src/lib/net/rtmp/handshake.c \
	src/lib/net/rtmp/chunk.c \
	src/lib/net/rtmp/session.c \
	src/lib/net/rtmp/command.c \
	src/lib/net/rtmp/rtmp.c \
	src/lib/net/rtmp/rtmpout.c \
	src/lib/net/rtmp/auth.c \
	src/lib/scrambler/scrambler.c \
	src/lib/scrambler/cissa.c \
	src/lib/scrambler/csa2_stub.c \
	src/lib/cas/biss/biss.c \
	src/lib/cas/biss/hex.c \
	src/lib/cas/biss/ca.c \
	src/lib/cas/biss/ca_sections.c \
	src/lib/net/tls.c \
	src/lib/net/netconnect.c \
	src/lib/net/httpclient/httpclient.c \
	src/lib/net/httpclient/url.c \
	src/lib/net/httpclient/read.c \
	src/lib/net/httpclient/async.c
dipidescramble_pipeline_EXTRA_CFLAGS := $(shell pkg-config --cflags openssl)
dipidescramble_pipeline_EXTRA_LDFLAGS := $(shell pkg-config --libs openssl)

UNIT_TESTS += dipidescramble_args
dipidescramble_args_BIN := tests/unit/dipidescramble/test_args
dipidescramble_args_SRCS := \
	tests/unit/dipidescramble/test_args.c \
	src/dipidescramble/args.c \
	src/dipidescramble/ecm_profile/common.c \
	src/dipidescramble/ecm_profile/parse.c \
	src/dipidescramble/ecm_profile/validate.c \
	src/dipidescramble/ecm_profile/wire.c \
	src/dipidescramble/ecm_profile/crypto.c \
	src/dipidescramble/crypto.c \
	src/lib/cas/device_crypto.c \
	src/lib/cas/biss/hex.c \
	src/lib/uriparse.c \
	src/lib/log.c \
	src/lib/argutil.c \
	src/lib/ioutil.c \
	src/lib/secure_zero.c
dipidescramble_args_EXTRA_CFLAGS := $(shell pkg-config --cflags openssl)
dipidescramble_args_EXTRA_LDFLAGS := $(shell pkg-config --libs openssl)
else
$(warning tests: OpenSSL not found via pkg-config, skipping dipicam378_crypto/dipicam378_device/dipicam378_cs378x/dipidescramble_crypto/dipidescramble_device/dipidescramble_ecm_profile/dipidescramble_biss_ca_state/dipidescramble_pipeline/dipidescramble_args unit tests)
endif

lib_metrics_protocol_BIN := tests/unit/lib/metrics/test_protocol
lib_metrics_protocol_SRCS := \
	tests/unit/lib/metrics/test_protocol.c \
	src/lib/metrics/protocol.c \
	src/lib/log.c \
	src/lib/ioutil.c

lib_metrics_export_BIN := tests/unit/lib/metrics/test_export
lib_metrics_export_SRCS := \
	tests/unit/lib/metrics/test_export.c \
	src/lib/metrics/export.c \
	src/lib/metrics/protocol.c \
	src/lib/net/netconnect.c \
	src/lib/signal.c \
	src/lib/log.c \
	src/lib/ioutil.c

dipimetrics_args_BIN := tests/unit/dipimetrics/test_args
dipimetrics_args_SRCS := \
	tests/unit/dipimetrics/test_args.c \
	src/dipimetrics/args.c \
	src/lib/argutil.c \
	src/lib/ioutil.c \
	src/lib/log.c

dipimetrics_store_BIN := tests/unit/dipimetrics/test_store
dipimetrics_store_SRCS := \
	tests/unit/dipimetrics/test_store.c \
	src/dipimetrics/store.c \
	src/lib/metrics/protocol.c \
	src/lib/log.c \
	src/lib/ioutil.c

dipimetrics_render_BIN := tests/unit/dipimetrics/test_render
dipimetrics_render_SRCS := \
	tests/unit/dipimetrics/test_render.c \
	src/dipimetrics/render.c \
	src/dipimetrics/store.c \
	src/lib/metrics/protocol.c \
	src/lib/log.c \
	src/lib/ioutil.c

dipisds_args_BIN := tests/unit/dipisds/test_args
dipisds_args_SRCS := \
	tests/unit/dipisds/test_args.c \
	src/dipisds/args.c \
	src/lib/argutil.c \
	src/lib/uriparse.c \
	src/lib/ioutil.c \
	src/lib/log.c

dipirist_args_BIN := tests/unit/dipirist/test_args
dipirist_args_SRCS := \
	tests/unit/dipirist/test_args.c \
	src/dipirist/args.c \
	src/lib/argutil.c \
	src/lib/uriparse.c \
	src/lib/net/httpclient/url.c \
	src/lib/ioutil.c \
	src/lib/log.c

ifeq ($(HAVE_RIST),yes)
UNIT_TESTS += dipirist_bridge
dipirist_bridge_BIN := tests/unit/dipirist/test_bridge
dipirist_bridge_EXTRA_CFLAGS := $(shell pkg-config --cflags librist)
dipirist_bridge_EXTRA_LDFLAGS := $(shell pkg-config --libs librist)
dipirist_bridge_SRCS := \
	tests/unit/dipirist/test_bridge.c \
	src/dipirist/bridge.c \
	src/dipirist/args.c \
	src/lib/argutil.c \
	src/lib/uriparse.c \
	src/lib/ioutil.c \
	src/lib/log.c \
	src/lib/signal.c \
	src/lib/net/multicast.c \
	src/lib/net/netconnect.c \
	src/lib/net/tssource.c \
	src/lib/net/tssink.c \
	src/lib/net/rist/ristout.c \
	src/lib/net/rist/ristin.c \
	src/lib/net/rist/ristlog.c \
	src/lib/net/tls_stub.c \
	src/lib/net/httpclient/httpclient.c \
	src/lib/net/httpclient/url.c \
	src/lib/net/httpclient/read.c \
	src/lib/net/httpclient/async.c \
	src/lib/demux/rtp.c \
	src/lib/mux/rtpheader.c \
	src/lib/metrics/protocol.c \
	src/lib/metrics/export.c

UNIT_TESTS += lib_net_rist_ristlog
lib_net_rist_ristlog_BIN := tests/unit/lib/net/rist/test_ristlog
lib_net_rist_ristlog_EXTRA_CFLAGS := $(shell pkg-config --cflags librist)
lib_net_rist_ristlog_EXTRA_LDFLAGS := $(shell pkg-config --libs librist)
lib_net_rist_ristlog_SRCS := \
	tests/unit/lib/net/rist/test_ristlog.c \
	src/lib/net/rist/ristlog.c \
	src/lib/log.c

UNIT_TESTS += lib_net_rist_ristin
lib_net_rist_ristin_BIN := tests/unit/lib/net/rist/test_ristin
lib_net_rist_ristin_EXTRA_CFLAGS := $(shell pkg-config --cflags librist)
lib_net_rist_ristin_EXTRA_LDFLAGS := $(shell pkg-config --libs librist)
lib_net_rist_ristin_SRCS := \
	tests/unit/lib/net/rist/test_ristin.c \
	src/lib/net/rist/ristin.c \
	src/lib/net/rist/ristlog.c \
	src/lib/log.c \
	src/lib/ioutil.c \
	src/lib/signal.c \
	src/lib/net/netconnect.c \
	src/lib/metrics/protocol.c \
	src/lib/metrics/export.c
endif

dipisrt_args_BIN := tests/unit/dipisrt/test_args
dipisrt_args_SRCS := \
	tests/unit/dipisrt/test_args.c \
	src/dipisrt/args.c \
	src/lib/argutil.c \
	src/lib/uriparse.c \
	src/lib/net/httpclient/url.c \
	src/lib/ioutil.c \
	src/lib/log.c

ifeq ($(HAVE_SRT),yes)
UNIT_TESTS += dipisrt_bridge lib_net_srt_srtcommon
dipisrt_bridge_BIN := tests/unit/dipisrt/test_bridge
dipisrt_bridge_EXTRA_CFLAGS := $(shell pkg-config --cflags srt)
dipisrt_bridge_EXTRA_LDFLAGS := $(shell pkg-config --libs srt)
dipisrt_bridge_SRCS := \
	tests/unit/dipisrt/test_bridge.c \
	src/dipisrt/bridge.c \
	src/dipisrt/args.c \
	src/lib/argutil.c \
	src/lib/uriparse.c \
	src/lib/ioutil.c \
	src/lib/log.c \
	src/lib/signal.c \
	src/lib/net/multicast.c \
	src/lib/net/netconnect.c \
	src/lib/net/tssource.c \
	src/lib/net/tssink.c \
	src/lib/net/rist/ristin_stub.c \
	src/lib/net/rist/ristlog_stub.c \
	src/lib/net/srt/srtcommon.c \
	src/lib/net/srt/srtout.c \
	src/lib/net/srt/srtin.c \
	src/lib/net/tls_stub.c \
	src/lib/net/httpclient/httpclient.c \
	src/lib/net/httpclient/url.c \
	src/lib/net/httpclient/read.c \
	src/lib/net/httpclient/async.c \
	src/lib/demux/rtp.c \
	src/lib/mux/rtpheader.c \
	src/lib/metrics/protocol.c \
	src/lib/metrics/export.c

lib_net_srt_srtcommon_BIN := tests/unit/lib/net/srt/test_srtcommon
lib_net_srt_srtcommon_EXTRA_CFLAGS := $(shell pkg-config --cflags srt)
lib_net_srt_srtcommon_EXTRA_LDFLAGS := $(shell pkg-config --libs srt)
lib_net_srt_srtcommon_SRCS := \
	tests/unit/lib/net/srt/test_srtcommon.c \
	src/lib/net/srt/srtcommon.c \
	src/lib/ioutil.c \
	src/lib/log.c
endif

dipisds_input_BIN := tests/unit/dipisds/test_input
dipisds_input_SRCS := \
	tests/unit/dipisds/test_input.c \
	src/dipisds/input.c \
	src/lib/ioutil.c \
	src/lib/xml_util.c

dipisds_format_out_BIN := tests/unit/dipisds/test_format_out
dipisds_format_out_SRCS := \
	tests/unit/dipisds/test_format_out.c \
	src/dipisds/format_out.c \
	src/lib/playlist_out.c \
	src/lib/xml_util.c

dipisds_listen_BIN := tests/unit/dipisds/test_listen
dipisds_listen_SRCS := \
	tests/unit/dipisds/test_listen.c \
	src/dipisds/listen.c \
	src/dipisds/args.c \
	src/lib/argutil.c \
	src/lib/uriparse.c \
	src/dipisds/format_out.c \
	src/lib/playlist_out.c \
	src/lib/sds_xml.c \
	src/lib/xml_util.c \
	src/lib/net/dvbstp.c \
	src/lib/net/multicast.c \
	src/lib/ioutil.c \
	src/lib/net/netconnect.c \
	src/lib/demux/crc32.c \
	src/lib/signal.c \
	src/lib/log.c

dipisds_announce_BIN := tests/unit/dipisds/test_announce
dipisds_announce_SRCS := \
	tests/unit/dipisds/test_announce.c \
	src/dipisds/announce.c \
	src/dipisds/input.c \
	src/lib/sds_xml.c \
	src/lib/xml_util.c \
	src/lib/ioutil.c \
	src/lib/metrics/export.c \
	src/lib/metrics/protocol.c \
	src/lib/net/dvbstp.c \
	src/lib/net/multicast.c \
	src/lib/net/netconnect.c \
	src/lib/demux/crc32.c \
	src/lib/signal.c \
	src/lib/log.c

dipimetrics_httpserver_BIN := tests/unit/dipimetrics/test_httpserver
dipimetrics_httpserver_SRCS := \
	tests/unit/dipimetrics/test_httpserver.c \
	src/dipimetrics/httpserver.c \
	src/dipimetrics/render.c \
	src/dipimetrics/store.c \
	src/lib/metrics/protocol.c \
	src/lib/signal.c \
	src/lib/log.c \
	src/lib/ioutil.c

dipibcg_container_BIN := tests/unit/dipibcg/test_container
dipibcg_container_SRCS := \
	tests/unit/dipibcg/test_container.c \
	src/dipibcg/container.c

dipibcg_args_BIN := tests/unit/dipibcg/test_args
dipibcg_args_SRCS := \
	tests/unit/dipibcg/test_args.c \
	src/dipibcg/args.c \
	src/lib/argutil.c \
	src/lib/uriparse.c \
	src/lib/ioutil.c \
	src/lib/log.c

dipibcg_announce_BIN := tests/unit/dipibcg/test_announce
dipibcg_announce_SRCS := \
	tests/unit/dipibcg/test_announce.c \
	src/dipibcg/announce.c \
	src/dipibcg/args.c \
	src/lib/argutil.c \
	src/lib/uriparse.c \
	src/lib/tva/bcg_doc.c \
	src/lib/tva/xmltv.c \
	src/lib/tva/timefmt.c \
	src/lib/tva/mapping.c \
	src/lib/tva/tva_xml.c \
	src/lib/xml_util.c \
	src/lib/ioutil.c \
	src/lib/bim/accessunit.c \
	src/lib/bim/bitwriter.c \
	src/lib/bim/bitreader.c \
	src/lib/bim/strrepo.c \
	src/lib/bim/fragment.c \
	src/lib/bim/codec.c \
	src/lib/demux/crc32.c \
	src/dipibcg/container.c \
	src/dipibcg/wrapper_stub.c \
	src/lib/metrics/export.c \
	src/lib/metrics/protocol.c \
	src/lib/net/dvbstp.c \
	src/lib/net/multicast.c \
	src/lib/net/netconnect.c \
	src/lib/signal.c \
	src/lib/log.c

dipibcg_listen_BIN := tests/unit/dipibcg/test_listen
dipibcg_listen_SRCS := \
	tests/unit/dipibcg/test_listen.c \
	src/dipibcg/listen.c \
	src/dipibcg/args.c \
	src/lib/argutil.c \
	src/lib/uriparse.c \
	src/dipibcg/container.c \
	src/dipibcg/wrapper_stub.c \
	src/lib/tva/bcg_doc.c \
	src/lib/tva/xmltv.c \
	src/lib/tva/timefmt.c \
	src/lib/xml_util.c \
	src/lib/ioutil.c \
	src/lib/bim/accessunit.c \
	src/lib/bim/bitwriter.c \
	src/lib/bim/bitreader.c \
	src/lib/bim/strrepo.c \
	src/lib/bim/fragment.c \
	src/lib/bim/codec.c \
	src/lib/tva/tva_xml.c \
	src/lib/demux/crc32.c \
	src/lib/net/dvbstp.c \
	src/lib/net/multicast.c \
	src/lib/net/netconnect.c \
	src/lib/signal.c \
	src/lib/log.c

lib_demux_crc32_BIN := tests/unit/lib/demux/test_crc32
lib_demux_crc32_SRCS := \
	tests/unit/lib/demux/test_crc32.c \
	src/lib/demux/crc32.c

lib_demux_rtcp_BIN := tests/unit/lib/demux/test_rtcp
lib_demux_rtcp_SRCS := \
	tests/unit/lib/demux/test_rtcp.c \
	src/lib/demux/rtcp.c \
	src/lib/mux/rtcp_build.c

lib_demux_psi_BIN := tests/unit/lib/demux/test_psi
lib_demux_psi_SRCS := \
	tests/unit/lib/demux/test_psi.c \
	src/lib/demux/psi/psi.c \
	src/lib/demux/psi/parse.c \
	src/lib/demux/psi/descriptors.c \
	src/lib/demux/tspack.c \
	src/lib/demux/psi/section_asm.c \
	src/lib/demux/crc32.c \
	src/lib/log.c

lib_demux_psi_section_asm_BIN := tests/unit/lib/demux/test_psi_section_asm
lib_demux_psi_section_asm_SRCS := \
	tests/unit/lib/demux/test_psi_section_asm.c \
	src/lib/demux/psi/section_asm.c \
	src/lib/demux/tspack.c

lib_demux_mpts_probe_BIN := tests/unit/lib/demux/test_mpts_probe
lib_demux_mpts_probe_SRCS := \
	tests/unit/lib/demux/test_mpts_probe.c \
	src/lib/demux/mpts_probe.c \
	src/lib/demux/psi/psi.c \
	src/lib/demux/psi/parse.c \
	src/lib/demux/psi/descriptors.c \
	src/lib/demux/psi/section_asm.c \
	src/lib/demux/tspack.c \
	src/lib/demux/crc32.c \
	src/lib/mux/psi_build.c \
	src/lib/net/tssource.c \
	src/lib/net/rist/ristin_stub.c \
	src/lib/net/rist/ristlog_stub.c \
	src/lib/net/httpclient/httpclient.c \
	src/lib/net/httpclient/url.c \
	src/lib/net/httpclient/read.c \
	src/lib/net/httpclient/async.c \
	src/lib/ioutil.c \
	src/lib/net/netconnect.c \
	src/lib/net/tls_stub.c \
	src/lib/net/multicast.c \
	src/lib/demux/rtp.c \
	src/lib/signal.c \
	src/lib/log.c

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
	src/lib/demux/pes.c \
	src/lib/demux/tspack.c

lib_mux_psi_build_BIN := tests/unit/lib/mux/test_psi_build
lib_mux_psi_build_SRCS := \
	tests/unit/lib/mux/test_psi_build.c \
	src/lib/mux/psi_build.c \
	src/lib/demux/psi/psi.c \
	src/lib/demux/psi/parse.c \
	src/lib/demux/psi/descriptors.c \
	src/lib/demux/tspack.c \
	src/lib/demux/psi/section_asm.c \
	src/lib/demux/crc32.c \
	src/lib/log.c

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

lib_mux_ebml_BIN := tests/unit/lib/mux/test_ebml
lib_mux_ebml_SRCS := \
	tests/unit/lib/mux/test_ebml.c \
	src/lib/mux/ebml.c

lib_mux_teletext_BIN := tests/unit/lib/mux/test_teletext
lib_mux_teletext_SRCS := \
	tests/unit/lib/mux/test_teletext.c \
	src/lib/mux/teletext.c \
	src/lib/demux/pes.c \
	src/lib/demux/tspack.c

lib_mux_mkv_BIN := tests/unit/lib/mux/test_mkv
lib_mux_mkv_SRCS := \
	tests/unit/lib/mux/test_mkv.c \
	src/lib/mux/mkv/mkv.c \
	src/lib/demux/bitreader.c \
	src/lib/demux/escodec/aubuild.c \
	src/lib/demux/escodec/audio.c \
	src/lib/demux/escodec/video.c \
	src/lib/mux/mkv/video.c \
	src/lib/mux/mkv/write.c \
	src/lib/mux/mkv/feed.c \
	src/lib/ioutil.c \
	src/lib/mux/ebml.c \
	src/lib/mux/teletext.c \
	src/lib/mux/psi_build.c \
	src/lib/demux/pes.c \
	src/lib/demux/psi/psi.c \
	src/lib/demux/psi/parse.c \
	src/lib/demux/psi/descriptors.c \
	src/lib/demux/tspack.c \
	src/lib/demux/psi/section_asm.c \
	src/lib/demux/crc32.c \
	src/lib/log.c

lib_mux_flv_BIN := tests/unit/lib/mux/test_flv
lib_mux_flv_SRCS := \
	tests/unit/lib/mux/test_flv.c \
	src/lib/mux/flv/flv.c \
	src/lib/mux/flv/write.c \
	src/lib/mux/flv/feed.c \
	src/lib/demux/bitreader.c \
	src/lib/demux/escodec/aubuild.c \
	src/lib/demux/escodec/audio.c \
	src/lib/demux/escodec/video.c \
	src/lib/mux/amf.c \
	src/lib/mux/ebml.c \
	src/lib/mux/psi_build.c \
	src/lib/demux/pes.c \
	src/lib/demux/psi/psi.c \
	src/lib/demux/psi/parse.c \
	src/lib/demux/psi/descriptors.c \
	src/lib/demux/tspack.c \
	src/lib/demux/psi/section_asm.c \
	src/lib/demux/crc32.c \
	src/lib/log.c

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
	src/lib/bim/strrepo.c \
	src/lib/ioutil.c

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
	src/lib/tva/timefmt.c \
	src/lib/ioutil.c

lib_tva_bcg_doc_BIN := tests/unit/lib/tva/test_bcg_doc
lib_tva_bcg_doc_SRCS := \
	tests/unit/lib/tva/test_bcg_doc.c \
	src/lib/tva/bcg_doc.c \
	src/lib/ioutil.c \
	src/lib/log.c

lib_tva_mapping_BIN := tests/unit/lib/tva/test_mapping
lib_tva_mapping_SRCS := \
	tests/unit/lib/tva/test_mapping.c \
	src/lib/tva/mapping.c \
	src/lib/ioutil.c

lib_tva_xmltv_BIN := tests/unit/lib/tva/test_xmltv
lib_tva_xmltv_SRCS := \
	tests/unit/lib/tva/test_xmltv.c \
	src/lib/tva/xmltv.c \
	src/lib/tva/timefmt.c \
	src/lib/tva/bcg_doc.c \
	src/lib/xml_util.c \
	src/lib/ioutil.c \
	src/lib/log.c

lib_tva_tva_xml_BIN := tests/unit/lib/tva/test_tva_xml
lib_tva_tva_xml_SRCS := \
	tests/unit/lib/tva/test_tva_xml.c \
	src/lib/tva/tva_xml.c \
	src/lib/tva/bcg_doc.c \
	src/lib/xml_util.c \
	src/lib/ioutil.c \
	src/lib/log.c

BIM_FRAGMENT_DEPS := \
	src/lib/bim/fragment.c \
	src/lib/bim/codec.c \
	src/lib/bim/bitwriter.c \
	src/lib/bim/bitreader.c \
	src/lib/bim/strrepo.c \
	src/lib/tva/tva_xml.c \
	src/lib/tva/bcg_doc.c \
	src/lib/xml_util.c \
	src/lib/ioutil.c \
	src/lib/log.c

lib_bim_fragment_BIN := tests/unit/lib/bim/test_fragment
lib_bim_fragment_SRCS := \
	tests/unit/lib/bim/test_fragment.c \
	$(BIM_FRAGMENT_DEPS)

lib_bim_accessunit_BIN := tests/unit/lib/bim/test_accessunit
lib_bim_accessunit_SRCS := \
	tests/unit/lib/bim/test_accessunit.c \
	src/lib/bim/accessunit.c \
	$(BIM_FRAGMENT_DEPS)

dipibim_args_BIN := tests/unit/dipibim/test_args
dipibim_args_SRCS := \
	tests/unit/dipibim/test_args.c \
	src/dipibim/args.c \
	src/lib/argutil.c \
	src/lib/log.c

dipiscan_format_BIN := tests/unit/dipiscan/test_format
dipiscan_format_SRCS := \
	tests/unit/dipiscan/test_format.c \
	src/dipiscan/format.c \
	src/lib/playlist_out.c \
	src/lib/sds_xml.c \
	src/lib/xml_util.c \
	src/lib/ioutil.c

dipiscan_scan_BIN := tests/unit/dipiscan/test_scan
dipiscan_scan_SRCS := \
	tests/unit/dipiscan/test_scan.c \
	src/dipiscan/scan.c \
	src/dipiscan/args.c \
	src/lib/argutil.c \
	src/dipiscan/format.c \
	src/lib/playlist_out.c \
	src/lib/sds_xml.c \
	src/lib/xml_util.c \
	src/lib/demux/psi/psi.c \
	src/lib/demux/psi/parse.c \
	src/lib/demux/psi/descriptors.c \
	src/lib/demux/psi/section_asm.c \
	src/lib/demux/tspack.c \
	src/lib/demux/rtp.c \
	src/lib/demux/crc32.c \
	src/lib/mux/psi_build.c \
	src/lib/net/multicast.c \
	src/lib/net/netconnect.c \
	src/lib/net/httpclient/httpclient.c \
	src/lib/net/httpclient/url.c \
	src/lib/net/httpclient/read.c \
	src/lib/net/httpclient/async.c \
	src/lib/net/tls_stub.c \
	src/lib/ioutil.c \
	src/lib/signal.c \
	src/lib/log.c

dipixmltv_args_BIN := tests/unit/dipixmltv/test_args
dipixmltv_args_SRCS := \
	tests/unit/dipixmltv/test_args.c \
	src/dipixmltv/args.c \
	src/lib/argutil.c \
	src/lib/log.c

dipixmltv_revmap_BIN := tests/unit/dipixmltv/test_revmap
dipixmltv_revmap_SRCS := \
	tests/unit/dipixmltv/test_revmap.c \
	src/dipixmltv/revmap.c \
	src/lib/ioutil.c

dipixmltv_suggest_BIN := tests/unit/dipixmltv/test_suggest
dipixmltv_suggest_SRCS := \
	tests/unit/dipixmltv/test_suggest.c \
	src/dipixmltv/suggest.c \
	src/lib/tva/xmltv.c \
	src/lib/tva/timefmt.c \
	src/lib/tva/bcg_doc.c \
	src/lib/xml_util.c \
	src/lib/ioutil.c \
	src/lib/log.c

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
	src/lib/demux/psi/psi.c \
	src/lib/demux/psi/parse.c \
	src/lib/demux/psi/descriptors.c \
	src/lib/demux/tspack.c \
	src/lib/demux/psi/section_asm.c \
	src/lib/demux/crc32.c \
	src/lib/log.c

dipiradiohead_id3_BIN := tests/unit/dipiradiohead/test_id3
dipiradiohead_id3_SRCS := \
	tests/unit/dipiradiohead/test_id3.c \
	src/dipiradiohead/input/id3.c \
	src/lib/ioutil.c

dipiradiohead_pes_BIN := tests/unit/dipiradiohead/test_pes
dipiradiohead_pes_SRCS := \
	tests/unit/dipiradiohead/test_pes.c \
	src/dipiradiohead/mux/pes.c \
	src/lib/demux/pes.c \
	src/lib/demux/tspack.c

dipiradiohead_tspacketizer_BIN := tests/unit/dipiradiohead/test_tspacketizer
dipiradiohead_tspacketizer_SRCS := \
	tests/unit/dipiradiohead/test_tspacketizer.c \
	src/dipiradiohead/mux/tspacketizer.c \
	src/lib/ioutil.c \
	src/dipiradiohead/mux/psi.c \
	src/dipiradiohead/mux/pes.c \
	src/dipiradiohead/cas/cas.c \
	src/lib/mux/cadescbuild.c \
	src/lib/cas/ecmg_client/ecmg_client.c \
	src/lib/cas/ecmg_client/protocol.c \
	src/lib/cas/ecmg_client/connect.c \
	src/lib/cas/ecmg_client/run.c \
	src/lib/cas/emmg_server/emmg_server.c \
	src/lib/cas/emmg_server/protocol.c \
	src/lib/cas/emmg_server/worker.c \
	src/lib/cas/simulcrypt_msg.c \
	src/lib/cas/cas_group.c \
	src/lib/cas/cas_scramble_engine.c \
	src/lib/cas/cas_core.c \
	src/lib/mux/psi_build.c \
	src/lib/mux/tspacket_write.c \
	src/lib/demux/psi/psi.c \
	src/lib/demux/psi/parse.c \
	src/lib/demux/psi/descriptors.c \
	src/lib/demux/tspack.c \
	src/lib/demux/psi/section_asm.c \
	src/lib/demux/crc32.c \
	src/lib/scrambler/scrambler.c \
	src/lib/scrambler/cissa_stub.c \
	src/lib/scrambler/csa2_stub.c \
	src/lib/cas/biss/stub.c \
	src/lib/cas/biss/hex.c \
	src/lib/cas/biss/ca_stub.c \
	src/lib/cas/biss/ca_sections.c \
	src/lib/cas/biss/ca_engine.c \
	src/lib/log.c \
	src/lib/signal.c

dipiradiohead_radiohead_BIN := tests/unit/dipiradiohead/test_radiohead
dipiradiohead_radiohead_SRCS := \
	tests/unit/dipiradiohead/test_radiohead.c \
	src/dipiradiohead/radiohead/radiohead.c \
	src/dipiradiohead/radiohead/mpts.c \
	src/dipiradiohead/radiohead/metrics.c \
	src/dipiradiohead/mux/tspacketizer.c \
	src/dipiradiohead/mux/psi.c \
	src/dipiradiohead/mux/pes.c \
	src/dipiradiohead/cas/cas.c \
	src/lib/mux/cadescbuild.c \
	src/lib/mux/rtpheader.c \
	src/lib/mux/psi_build.c \
	src/lib/mux/tspacket_write.c \
	src/lib/mux/mpts.c \
	src/lib/cas/ecmg_client/ecmg_client.c \
	src/lib/cas/ecmg_client/protocol.c \
	src/lib/cas/ecmg_client/connect.c \
	src/lib/cas/ecmg_client/run.c \
	src/lib/cas/emmg_server/emmg_server.c \
	src/lib/cas/emmg_server/protocol.c \
	src/lib/cas/emmg_server/worker.c \
	src/lib/cas/simulcrypt_msg.c \
	src/lib/cas/cas_group.c \
	src/lib/cas/cas_scramble_engine.c \
	src/lib/cas/cas_core.c \
	src/lib/demux/rtp.c \
	src/lib/demux/crc32.c \
	src/lib/scrambler/scrambler.c \
	src/lib/scrambler/cissa_stub.c \
	src/lib/scrambler/csa2_stub.c \
	src/lib/cas/biss/stub.c \
	src/lib/cas/biss/hex.c \
	src/lib/cas/biss/ca_stub.c \
	src/lib/cas/biss/ca_sections.c \
	src/lib/cas/biss/ca_engine.c \
	src/lib/net/multicast.c \
	src/lib/net/retryset.c \
	src/lib/net/rist/ristout_stub.c \
	src/dipiradiohead/input/source/open.c \
	src/dipiradiohead/input/source/open_async.c \
	src/dipiradiohead/input/source/frame.c \
	src/dipiradiohead/input/inputset.c \
	src/dipiradiohead/input/playlist.c \
	src/dipiradiohead/input/icy.c \
	src/dipiradiohead/input/id3.c \
	src/dipiradiohead/framer/mpegaudio.c \
	src/dipiradiohead/framer/aac_adts.c \
	src/dipiradiohead/framer/aac_latm.c \
	src/lib/demux/bitreader.c \
	src/lib/net/httpclient/httpclient.c \
	src/lib/net/httpclient/url.c \
	src/lib/net/httpclient/read.c \
	src/lib/net/httpclient/async.c \
	src/lib/net/netconnect.c \
	src/lib/net/tls_stub.c \
	src/lib/ioutil.c \
	src/lib/metrics/export.c \
	src/lib/metrics/protocol.c \
	src/lib/log.c \
	src/lib/signal.c

dipiradiohead_cas_BIN := tests/unit/dipiradiohead/test_cas
dipiradiohead_cas_SRCS := \
	tests/unit/dipiradiohead/test_cas.c \
	src/dipiradiohead/cas/cas.c \
	src/lib/mux/cadescbuild.c \
	src/lib/ioutil.c \
	src/lib/cas/ecmg_client/ecmg_client.c \
	src/lib/cas/ecmg_client/protocol.c \
	src/lib/cas/ecmg_client/connect.c \
	src/lib/cas/ecmg_client/run.c \
	src/lib/cas/emmg_server/emmg_server.c \
	src/lib/cas/emmg_server/protocol.c \
	src/lib/cas/emmg_server/worker.c \
	src/lib/cas/simulcrypt_msg.c \
	src/lib/cas/cas_group.c \
	src/lib/cas/cas_scramble_engine.c \
	src/lib/cas/cas_core.c \
	src/lib/mux/psi_build.c \
	src/lib/demux/psi/psi.c \
	src/lib/demux/psi/parse.c \
	src/lib/demux/psi/descriptors.c \
	src/lib/demux/tspack.c \
	src/lib/demux/psi/section_asm.c \
	src/lib/demux/crc32.c \
	src/lib/scrambler/scrambler.c \
	src/lib/scrambler/cissa_stub.c \
	src/lib/scrambler/csa2_stub.c \
	src/lib/cas/biss/stub.c \
	src/lib/cas/biss/hex.c \
	src/lib/cas/biss/ca_stub.c \
	src/lib/cas/biss/ca_sections.c \
	src/lib/cas/biss/ca_engine.c \
	src/lib/log.c \
	src/lib/signal.c

dipiradiohead_args_BIN := tests/unit/dipiradiohead/test_args
dipiradiohead_args_SRCS := \
	tests/unit/dipiradiohead/test_args.c \
	src/dipiradiohead/args.c \
	src/lib/argutil.c \
	src/lib/uriparse.c \
	src/lib/ioutil.c \
	src/lib/cas/cas_args.c \
	src/lib/cas/biss/stub.c \
	src/lib/cas/biss/hex.c \
	src/lib/cas/biss/ca_stub.c \
	src/lib/log.c

dipiradiohead_source_async_BIN := tests/unit/dipiradiohead/input/test_source_async
dipiradiohead_source_async_SRCS := \
	tests/unit/dipiradiohead/input/test_source_async.c \
	src/dipiradiohead/input/source/open.c \
	src/dipiradiohead/input/source/open_async.c \
	src/dipiradiohead/input/source/frame.c \
	src/dipiradiohead/input/playlist.c \
	src/dipiradiohead/input/icy.c \
	src/dipiradiohead/input/id3.c \
	src/dipiradiohead/framer/mpegaudio.c \
	src/dipiradiohead/framer/aac_adts.c \
	src/dipiradiohead/framer/aac_latm.c \
	src/lib/demux/bitreader.c \
	src/lib/net/httpclient/httpclient.c \
	src/lib/net/httpclient/url.c \
	src/lib/net/httpclient/read.c \
	src/lib/net/httpclient/async.c \
	src/lib/ioutil.c \
	src/lib/net/netconnect.c \
	src/lib/net/tls_stub.c \
	src/lib/signal.c \
	src/lib/log.c

dipiradiohead_inputset_BIN := tests/unit/dipiradiohead/input/test_inputset
dipiradiohead_inputset_SRCS := \
	tests/unit/dipiradiohead/input/test_inputset.c \
	src/dipiradiohead/input/inputset.c \
	src/lib/net/retryset.c \
	src/dipiradiohead/input/source/open.c \
	src/dipiradiohead/input/source/open_async.c \
	src/dipiradiohead/input/source/frame.c \
	src/dipiradiohead/input/playlist.c \
	src/dipiradiohead/input/icy.c \
	src/dipiradiohead/input/id3.c \
	src/dipiradiohead/framer/mpegaudio.c \
	src/dipiradiohead/framer/aac_adts.c \
	src/dipiradiohead/framer/aac_latm.c \
	src/lib/demux/bitreader.c \
	src/lib/net/httpclient/httpclient.c \
	src/lib/net/httpclient/url.c \
	src/lib/net/httpclient/read.c \
	src/lib/net/httpclient/async.c \
	src/lib/ioutil.c \
	src/lib/net/netconnect.c \
	src/lib/net/tls_stub.c \
	src/lib/signal.c \
	src/lib/log.c

lib_mux_mpts_BIN := tests/unit/lib/mux/test_mpts
lib_mux_mpts_SRCS := \
	tests/unit/lib/mux/test_mpts.c \
	src/lib/mux/mpts.c \
	src/lib/mux/psi_build.c \
	src/lib/mux/tspacket_write.c \
	src/lib/demux/psi/psi.c \
	src/lib/demux/psi/parse.c \
	src/lib/demux/psi/descriptors.c \
	src/lib/demux/tspack.c \
	src/lib/demux/psi/section_asm.c \
	src/lib/demux/crc32.c \
	src/lib/log.c

dipitvhead_source_BIN := tests/unit/dipitvhead/input/test_source
dipitvhead_source_SRCS := \
	tests/unit/dipitvhead/input/test_source.c \
	src/dipitvhead/input/source.c \
	src/lib/net/tssource.c \
	src/lib/net/rist/ristin_stub.c \
	src/lib/net/rist/ristlog_stub.c \
	src/lib/net/multicast.c \
	src/lib/net/netconnect.c \
	src/lib/net/httpclient/httpclient.c \
	src/lib/net/httpclient/url.c \
	src/lib/net/httpclient/read.c \
	src/lib/net/httpclient/async.c \
	src/lib/net/tls_stub.c \
	src/lib/demux/rtp.c \
	src/lib/ioutil.c \
	src/lib/signal.c \
	src/lib/log.c

dipitvhead_args_BIN := tests/unit/dipitvhead/test_args
dipitvhead_args_SRCS := \
	tests/unit/dipitvhead/test_args.c \
	src/dipitvhead/args.c \
	src/lib/argutil.c \
	src/lib/uriparse.c \
	src/lib/cas/cas_args.c \
	src/lib/cas/biss/stub.c \
	src/lib/cas/biss/hex.c \
	src/lib/cas/biss/ca_stub.c \
	src/lib/net/httpclient/httpclient.c \
	src/lib/net/httpclient/url.c \
	src/lib/net/httpclient/read.c \
	src/lib/net/httpclient/async.c \
	src/lib/ioutil.c \
	src/lib/net/netconnect.c \
	src/lib/net/tls_stub.c \
	src/lib/log.c \
	src/lib/signal.c

dipitvhead_discover_BIN := tests/unit/dipitvhead/test_discover
dipitvhead_discover_SRCS := \
	tests/unit/dipitvhead/test_discover.c \
	src/dipitvhead/tvhead/discover.c \
	src/dipitvhead/input/source.c \
	src/lib/net/tssource.c \
	src/lib/net/rist/ristin_stub.c \
	src/lib/net/rist/ristlog_stub.c \
	src/lib/net/multicast.c \
	src/lib/net/netconnect.c \
	src/lib/net/httpclient/httpclient.c \
	src/lib/net/httpclient/url.c \
	src/lib/net/httpclient/read.c \
	src/lib/net/httpclient/async.c \
	src/lib/net/tls_stub.c \
	src/lib/demux/rtp.c \
	src/lib/demux/tspack.c \
	src/lib/demux/psi/psi.c \
	src/lib/demux/psi/parse.c \
	src/lib/demux/psi/descriptors.c \
	src/lib/demux/psi/section_asm.c \
	src/lib/demux/crc32.c \
	src/lib/mux/psi_build.c \
	src/lib/metrics/export.c \
	src/lib/metrics/protocol.c \
	src/lib/ioutil.c \
	src/lib/signal.c \
	src/lib/log.c

dipitvhead_pmtbuild_BIN := tests/unit/dipitvhead/test_pmtbuild
dipitvhead_pmtbuild_SRCS := \
	tests/unit/dipitvhead/test_pmtbuild.c \
	src/dipitvhead/mux/pmtbuild.c \
	src/lib/mux/psi_build.c \
	src/lib/demux/psi/psi.c \
	src/lib/demux/psi/parse.c \
	src/lib/demux/psi/descriptors.c \
	src/lib/demux/tspack.c \
	src/lib/demux/psi/section_asm.c \
	src/lib/demux/crc32.c \
	src/lib/log.c

dipitvhead_aitbuild_BIN := tests/unit/dipitvhead/test_aitbuild
dipitvhead_aitbuild_SRCS := \
	tests/unit/dipitvhead/test_aitbuild.c \
	src/dipitvhead/mux/aitbuild.c \
	src/lib/mux/psi_build.c \
	src/lib/demux/crc32.c

dipitvhead_bitrate_BIN := tests/unit/dipitvhead/test_bitrate
dipitvhead_bitrate_SRCS := \
	tests/unit/dipitvhead/test_bitrate.c \
	src/dipitvhead/mux/bitrate.c \
	src/lib/log.c \
	src/lib/signal.c

dipitvhead_remux_BIN := tests/unit/dipitvhead/test_remux
dipitvhead_remux_SRCS := \
	tests/unit/dipitvhead/test_remux.c \
	src/dipitvhead/mux/remux/lifecycle.c \
	src/dipitvhead/mux/remux/psi.c \
	src/dipitvhead/mux/remux/eit.c \
	src/dipitvhead/mux/remux/feed.c \
	src/lib/ioutil.c \
	src/dipitvhead/mux/pmtbuild.c \
	src/dipitvhead/mux/aitbuild.c \
	src/dipitvhead/cas/cas.c \
	src/lib/mux/cadescbuild.c \
	src/lib/cas/ecmg_client/ecmg_client.c \
	src/lib/cas/ecmg_client/protocol.c \
	src/lib/cas/ecmg_client/connect.c \
	src/lib/cas/ecmg_client/run.c \
	src/lib/cas/emmg_server/emmg_server.c \
	src/lib/cas/emmg_server/protocol.c \
	src/lib/cas/emmg_server/worker.c \
	src/lib/cas/simulcrypt_msg.c \
	src/lib/cas/cas_group.c \
	src/lib/cas/cas_scramble_engine.c \
	src/lib/cas/cas_core.c \
	src/lib/mux/psi_build.c \
	src/lib/mux/tspacket_write.c \
	src/lib/demux/psi/psi.c \
	src/lib/demux/psi/parse.c \
	src/lib/demux/psi/descriptors.c \
	src/lib/demux/tspack.c \
	src/lib/demux/psi/section_asm.c \
	src/lib/demux/crc32.c \
	src/lib/scrambler/scrambler.c \
	src/lib/scrambler/cissa_stub.c \
	src/lib/scrambler/csa2_stub.c \
	src/lib/cas/biss/stub.c \
	src/lib/cas/biss/hex.c \
	src/lib/cas/biss/ca_stub.c \
	src/lib/cas/biss/ca_sections.c \
	src/lib/cas/biss/ca_engine.c \
	src/lib/log.c \
	src/lib/signal.c

dipitvhead_output_BIN := tests/unit/dipitvhead/test_output
dipitvhead_output_SRCS := \
	tests/unit/dipitvhead/test_output.c \
	src/dipitvhead/tvhead/output.c \
	src/dipitvhead/mux/remux/lifecycle.c \
	src/dipitvhead/mux/remux/psi.c \
	src/dipitvhead/mux/remux/eit.c \
	src/dipitvhead/mux/remux/feed.c \
	src/dipitvhead/mux/pmtbuild.c \
	src/dipitvhead/mux/aitbuild.c \
	src/dipitvhead/mux/bitrate.c \
	src/dipitvhead/cas/cas.c \
	src/lib/mux/cadescbuild.c \
	src/lib/mux/rtpheader.c \
	src/lib/demux/rtp.c \
	src/lib/cas/ecmg_client/ecmg_client.c \
	src/lib/cas/ecmg_client/protocol.c \
	src/lib/cas/ecmg_client/connect.c \
	src/lib/cas/ecmg_client/run.c \
	src/lib/cas/emmg_server/emmg_server.c \
	src/lib/cas/emmg_server/protocol.c \
	src/lib/cas/emmg_server/worker.c \
	src/lib/cas/simulcrypt_msg.c \
	src/lib/cas/cas_group.c \
	src/lib/cas/cas_scramble_engine.c \
	src/lib/cas/cas_core.c \
	src/lib/mux/psi_build.c \
	src/lib/mux/tspacket_write.c \
	src/lib/demux/psi/psi.c \
	src/lib/demux/psi/parse.c \
	src/lib/demux/psi/descriptors.c \
	src/lib/demux/tspack.c \
	src/lib/demux/psi/section_asm.c \
	src/lib/demux/crc32.c \
	src/lib/scrambler/scrambler.c \
	src/lib/scrambler/cissa_stub.c \
	src/lib/scrambler/csa2_stub.c \
	src/lib/cas/biss/stub.c \
	src/lib/cas/biss/hex.c \
	src/lib/cas/biss/ca_stub.c \
	src/lib/cas/biss/ca_sections.c \
	src/lib/cas/biss/ca_engine.c \
	src/lib/net/multicast.c \
	src/lib/net/netconnect.c \
	src/lib/net/rist/ristout_stub.c \
	src/lib/net/rist/ristin_stub.c \
	src/lib/net/rist/ristlog_stub.c \
	src/dipitvhead/input/source.c \
	src/lib/net/tssource.c \
	src/lib/net/httpclient/httpclient.c \
	src/lib/net/httpclient/url.c \
	src/lib/net/httpclient/read.c \
	src/lib/net/httpclient/async.c \
	src/lib/net/tls_stub.c \
	src/lib/ioutil.c \
	src/lib/argutil.c \
	src/lib/metrics/export.c \
	src/lib/metrics/protocol.c \
	src/lib/log.c \
	src/lib/signal.c

dipitvhead_ecmg_client_BIN := tests/unit/dipitvhead/test_ecmg_client
dipitvhead_ecmg_client_SRCS := \
	tests/unit/dipitvhead/test_ecmg_client.c \
	src/lib/ioutil.c \
	src/lib/cas/ecmg_client/ecmg_client.c \
	src/lib/cas/ecmg_client/protocol.c \
	src/lib/cas/ecmg_client/connect.c \
	src/lib/cas/ecmg_client/run.c \
	src/lib/cas/simulcrypt_msg.c \
	src/lib/mux/psi_build.c \
	src/lib/demux/psi/psi.c \
	src/lib/demux/psi/parse.c \
	src/lib/demux/psi/descriptors.c \
	src/lib/demux/tspack.c \
	src/lib/demux/psi/section_asm.c \
	src/lib/demux/crc32.c \
	src/lib/scrambler/scrambler.c \
	src/lib/scrambler/cissa_stub.c \
	src/lib/scrambler/csa2_stub.c \
	src/lib/log.c \
	src/lib/signal.c

dipitvhead_emmg_server_BIN := tests/unit/dipitvhead/test_emmg_server
dipitvhead_emmg_server_SRCS := \
	tests/unit/dipitvhead/test_emmg_server.c \
	src/lib/cas/emmg_server/emmg_server.c \
	src/lib/cas/emmg_server/protocol.c \
	src/lib/cas/emmg_server/worker.c \
	src/lib/cas/simulcrypt_msg.c \
	src/lib/mux/psi_build.c \
	src/lib/demux/psi/psi.c \
	src/lib/demux/psi/parse.c \
	src/lib/demux/psi/descriptors.c \
	src/lib/demux/tspack.c \
	src/lib/demux/psi/section_asm.c \
	src/lib/demux/crc32.c \
	src/lib/log.c \
	src/lib/signal.c

dipitvhead_simulcrypt_msg_BIN := tests/unit/dipitvhead/test_simulcrypt_msg
dipitvhead_simulcrypt_msg_SRCS := \
	tests/unit/dipitvhead/test_simulcrypt_msg.c \
	src/lib/cas/simulcrypt_msg.c \
	src/lib/mux/psi_build.c \
	src/lib/demux/psi/psi.c \
	src/lib/demux/psi/parse.c \
	src/lib/demux/psi/descriptors.c \
	src/lib/demux/tspack.c \
	src/lib/demux/psi/section_asm.c \
	src/lib/demux/crc32.c \
	src/lib/log.c \
	src/lib/signal.c

dipitvhead_cas_BIN := tests/unit/dipitvhead/test_cas
dipitvhead_cas_SRCS := \
	tests/unit/dipitvhead/test_cas.c \
	src/dipitvhead/cas/cas.c \
	src/lib/ioutil.c \
	src/lib/cas/ecmg_client/ecmg_client.c \
	src/lib/cas/ecmg_client/protocol.c \
	src/lib/cas/ecmg_client/connect.c \
	src/lib/cas/ecmg_client/run.c \
	src/lib/cas/emmg_server/emmg_server.c \
	src/lib/cas/emmg_server/protocol.c \
	src/lib/cas/emmg_server/worker.c \
	src/lib/cas/simulcrypt_msg.c \
	src/lib/cas/cas_group.c \
	src/lib/cas/cas_scramble_engine.c \
	src/lib/cas/cas_core.c \
	src/lib/mux/cadescbuild.c \
	src/lib/mux/psi_build.c \
	src/lib/demux/psi/psi.c \
	src/lib/demux/psi/parse.c \
	src/lib/demux/psi/descriptors.c \
	src/lib/demux/tspack.c \
	src/lib/demux/psi/section_asm.c \
	src/lib/demux/crc32.c \
	src/lib/scrambler/scrambler.c \
	src/lib/scrambler/cissa_stub.c \
	src/lib/scrambler/csa2_stub.c \
	src/lib/cas/biss/stub.c \
	src/lib/cas/biss/hex.c \
	src/lib/cas/biss/ca_stub.c \
	src/lib/cas/biss/ca_sections.c \
	src/lib/cas/biss/ca_engine.c \
	src/lib/log.c \
	src/lib/signal.c

lib_mux_cadescbuild_BIN := tests/unit/lib/mux/test_cadescbuild
lib_mux_cadescbuild_SRCS := \
	tests/unit/lib/mux/test_cadescbuild.c \
	src/lib/mux/cadescbuild.c \
	src/lib/mux/psi_build.c \
	src/lib/demux/crc32.c

lib_net_netconnect_BIN := tests/unit/lib/net/test_netconnect
lib_net_netconnect_SRCS := \
	tests/unit/lib/net/test_netconnect.c \
	src/lib/ioutil.c \
	src/lib/net/netconnect.c \
	src/lib/signal.c \
	src/lib/log.c

lib_net_rtmp_BIN := tests/unit/lib/net/test_rtmp
lib_net_rtmp_SRCS := \
	tests/unit/lib/net/test_rtmp.c \
	src/lib/net/rtmp/rtmp.c \
	src/lib/net/rtmp/session.c \
	src/lib/net/rtmp/command.c \
	src/lib/net/rtmp/chunk.c \
	src/lib/net/rtmp/handshake.c \
	src/lib/net/rtmp/auth_stub.c \
	src/lib/mux/amf.c \
	src/lib/mux/ebml.c \
	src/lib/ioutil.c \
	src/lib/log.c

lib_net_rtmpout_BIN := tests/unit/lib/net/rtmp/test_rtmpout
lib_net_rtmpout_SRCS := \
	tests/unit/lib/net/rtmp/test_rtmpout.c \
	src/lib/net/rtmp/rtmpout.c \
	src/lib/net/rtmp/rtmp.c \
	src/lib/net/rtmp/session.c \
	src/lib/net/rtmp/command.c \
	src/lib/net/rtmp/chunk.c \
	src/lib/net/rtmp/handshake.c \
	src/lib/net/rtmp/auth_stub.c \
	src/lib/mux/amf.c \
	src/lib/mux/ebml.c \
	src/lib/net/netconnect.c \
	src/lib/net/tls_stub.c \
	src/lib/ioutil.c \
	src/lib/signal.c \
	src/lib/log.c

lib_net_httpclient_async_BIN := tests/unit/lib/net/test_httpclient_async
lib_net_httpclient_async_SRCS := \
	tests/unit/lib/net/test_httpclient_async.c \
	src/lib/net/httpclient/httpclient.c \
	src/lib/net/httpclient/url.c \
	src/lib/net/httpclient/read.c \
	src/lib/net/httpclient/async.c \
	src/lib/ioutil.c \
	src/lib/net/netconnect.c \
	src/lib/net/tls_stub.c \
	src/lib/signal.c \
	src/lib/log.c

lib_net_tssource_async_BIN := tests/unit/lib/net/test_tssource_async
lib_net_tssource_async_SRCS := \
	tests/unit/lib/net/test_tssource_async.c \
	src/lib/net/tssource.c \
	src/lib/net/rist/ristin_stub.c \
	src/lib/net/rist/ristlog_stub.c \
	src/lib/net/httpclient/httpclient.c \
	src/lib/net/httpclient/url.c \
	src/lib/net/httpclient/read.c \
	src/lib/net/httpclient/async.c \
	src/lib/ioutil.c \
	src/lib/net/netconnect.c \
	src/lib/net/tls_stub.c \
	src/lib/net/multicast.c \
	src/lib/demux/rtp.c \
	src/lib/signal.c \
	src/lib/log.c

lib_net_tssource_file_BIN := tests/unit/lib/net/test_tssource_file
lib_net_tssource_file_SRCS := \
	tests/unit/lib/net/test_tssource_file.c \
	src/lib/net/tssource.c \
	src/lib/net/rist/ristin_stub.c \
	src/lib/net/rist/ristlog_stub.c \
	src/lib/net/httpclient/httpclient.c \
	src/lib/net/httpclient/url.c \
	src/lib/net/httpclient/read.c \
	src/lib/net/httpclient/async.c \
	src/lib/ioutil.c \
	src/lib/net/netconnect.c \
	src/lib/net/tls_stub.c \
	src/lib/net/multicast.c \
	src/lib/demux/rtp.c \
	src/lib/signal.c \
	src/lib/log.c

lib_net_retryset_BIN := tests/unit/lib/net/test_retryset
lib_net_retryset_SRCS := \
	tests/unit/lib/net/test_retryset.c \
	src/lib/net/retryset.c \
	src/lib/log.c

lib_net_dvbstp_BIN := tests/unit/lib/net/test_dvbstp
lib_net_dvbstp_SRCS := \
	tests/unit/lib/net/test_dvbstp.c \
	src/lib/net/dvbstp.c \
	src/lib/net/multicast.c \
	src/lib/ioutil.c \
	src/lib/net/netconnect.c \
	src/lib/demux/crc32.c \
	src/lib/signal.c \
	src/lib/log.c

lib_cas_cas_group_BIN := tests/unit/lib/cas/test_cas_group
lib_cas_cas_group_SRCS := \
	tests/unit/lib/cas/test_cas_group.c \
	src/lib/cas/cas_group.c \
	src/lib/ioutil.c \
	src/lib/cas/ecmg_client/ecmg_client.c \
	src/lib/cas/ecmg_client/protocol.c \
	src/lib/cas/ecmg_client/connect.c \
	src/lib/cas/ecmg_client/run.c \
	src/lib/cas/emmg_server/emmg_server.c \
	src/lib/cas/emmg_server/protocol.c \
	src/lib/cas/emmg_server/worker.c \
	src/lib/cas/simulcrypt_msg.c \
	src/lib/cas/cas_scramble_engine.c \
	src/lib/mux/cadescbuild.c \
	src/lib/mux/psi_build.c \
	src/lib/demux/psi/psi.c \
	src/lib/demux/psi/parse.c \
	src/lib/demux/psi/descriptors.c \
	src/lib/demux/tspack.c \
	src/lib/demux/psi/section_asm.c \
	src/lib/demux/crc32.c \
	src/lib/scrambler/scrambler.c \
	src/lib/scrambler/cissa_stub.c \
	src/lib/scrambler/csa2_stub.c \
	src/lib/log.c \
	src/lib/signal.c

dipirec_ts_filter_BIN := tests/unit/dipirec/test_ts_filter
dipirec_ts_filter_SRCS := \
	tests/unit/dipirec/test_ts_filter.c \
	src/dipirec/filter/ts.c \
	src/lib/demux/psi/psi.c \
	src/lib/demux/psi/parse.c \
	src/lib/demux/psi/descriptors.c \
	src/lib/demux/tspack.c \
	src/lib/demux/psi/section_asm.c \
	src/lib/demux/crc32.c \
	src/lib/log.c

dipirec_pace_BIN := tests/unit/dipirec/test_pace
dipirec_pace_SRCS := \
	tests/unit/dipirec/test_pace.c \
	src/dipirec/filter/pace.c \
	src/lib/signal.c

dipirec_record_BIN := tests/unit/dipirec/test_record
dipirec_record_SRCS := \
	tests/unit/dipirec/test_record.c \
	src/dipirec/record.c \
	src/dipirec/record/sink.c \
	src/dipirec/record/rtmp_fanout.c \
	src/dipirec/record/stats.c \
	src/dipirec/record/run.c \
	src/dipirec/ret_client.c \
	src/dipirec/args.c \
	src/lib/metrics/protocol.c \
	src/lib/metrics/export.c \
	src/lib/log.c \
	src/lib/argutil.c \
	src/lib/uriparse.c \
	src/lib/signal.c \
	src/lib/net/multicast.c \
	src/lib/net/netconnect.c \
	src/lib/net/tssource.c \
	src/lib/net/tssink.c \
	src/lib/net/tls_stub.c \
	src/lib/net/rtmp/auth_stub.c \
	src/lib/net/rist/ristout_stub.c \
	src/lib/net/rist/ristin_stub.c \
	src/lib/net/rist/ristlog_stub.c \
	src/lib/net/httpclient/httpclient.c \
	src/lib/net/httpclient/url.c \
	src/lib/net/httpclient/read.c \
	src/lib/net/httpclient/async.c \
	src/lib/ioutil.c \
	src/lib/demux/rtp.c \
	src/lib/demux/rtx.c \
	src/lib/demux/rtcp.c \
	src/lib/demux/crc32.c \
	src/lib/demux/psi/psi.c \
	src/lib/demux/psi/parse.c \
	src/lib/demux/psi/descriptors.c \
	src/lib/demux/psi/section_asm.c \
	src/lib/demux/tspack.c \
	src/lib/demux/pes.c \
	src/lib/demux/mpts_probe.c \
	src/lib/mux/rtx.c \
	src/lib/mux/rtcp_build.c \
	src/lib/mux/rtpheader.c \
	src/lib/mux/ebml.c \
	src/lib/mux/mkv/mkv.c \
	src/lib/demux/bitreader.c \
	src/lib/demux/escodec/aubuild.c \
	src/lib/demux/escodec/audio.c \
	src/lib/demux/escodec/video.c \
	src/lib/mux/mkv/video.c \
	src/lib/mux/mkv/write.c \
	src/lib/mux/mkv/feed.c \
	src/lib/mux/teletext.c \
	src/lib/mux/amf.c \
	src/lib/mux/flv/flv.c \
	src/lib/mux/flv/feed.c \
	src/lib/mux/flv/write.c \
	src/lib/net/rtmp/handshake.c \
	src/lib/net/rtmp/chunk.c \
	src/lib/net/rtmp/session.c \
	src/lib/net/rtmp/command.c \
	src/lib/net/rtmp/rtmp.c \
	src/lib/net/rtmp/rtmpout.c \
	src/dipirec/filter/ts.c \
	src/dipirec/filter/pace.c

dipirec_ret_client_BIN := tests/unit/dipirec/test_ret_client
dipirec_ret_client_SRCS := \
	tests/unit/dipirec/test_ret_client.c \
	src/dipirec/ret_client.c \
	src/lib/demux/rtp.c \
	src/lib/demux/rtx.c \
	src/lib/demux/rtcp.c \
	src/lib/mux/rtx.c \
	src/lib/mux/rtcp_build.c \
	src/lib/net/multicast.c \
	src/lib/ioutil.c \
	src/lib/net/netconnect.c \
	src/lib/signal.c \
	src/lib/log.c

dipirec_args_BIN := tests/unit/dipirec/test_args
dipirec_args_SRCS := \
	tests/unit/dipirec/test_args.c \
	src/dipirec/args.c \
	src/lib/argutil.c \
	src/lib/uriparse.c \
	src/lib/net/httpclient/url.c \
	src/lib/ioutil.c \
	src/lib/log.c

dipifccret_args_BIN := tests/unit/dipifccret/test_args
dipifccret_args_SRCS := \
	tests/unit/dipifccret/test_args.c \
	src/dipifccret/args.c \
	src/dipifccret/capture/ranges.c \
	src/lib/argutil.c \
	src/lib/log.c

dipifccret_listen_BIN := tests/unit/dipifccret/test_listen
dipifccret_listen_SRCS := \
	tests/unit/dipifccret/test_listen.c \
	src/dipifccret/listen.c \
	src/lib/ioutil.c \
	src/lib/net/netconnect.c \
	src/lib/signal.c \
	src/lib/log.c
dipifccret_listen_EXTRA_LDFLAGS := -pthread

dipifccret_channel_BIN := tests/unit/dipifccret/test_channel
dipifccret_channel_SRCS := \
	tests/unit/dipifccret/test_channel.c \
	src/dipifccret/channel/channel.c \
	src/dipifccret/channel/hash.c \
	src/dipifccret/channel/ring.c \
	src/lib/mux/psi_build.c \
	src/lib/demux/psi/psi.c \
	src/lib/demux/psi/parse.c \
	src/lib/demux/psi/descriptors.c \
	src/lib/demux/psi/section_asm.c \
	src/lib/demux/tspack.c \
	src/lib/demux/crc32.c \
	src/lib/log.c

dipifccret_ret_mcsend_BIN := tests/unit/dipifccret/ret/test_mcsend
dipifccret_ret_mcsend_SRCS := \
	tests/unit/dipifccret/ret/test_mcsend.c \
	src/dipifccret/ret/mcsend.c \
	src/lib/net/multicast.c \
	src/lib/ioutil.c \
	src/lib/net/netconnect.c \
	src/lib/signal.c \
	src/lib/log.c

dipifccret_burst_table_BIN := tests/unit/dipifccret/test_burst_table
dipifccret_burst_table_SRCS := \
	tests/unit/dipifccret/test_burst_table.c \
	src/dipifccret/fcc/burst_table.c \
	src/lib/net/sockaddr_index.c \
	src/dipifccret/fcc/burst.c \
	src/dipifccret/channel/channel.c \
	src/dipifccret/channel/hash.c \
	src/dipifccret/channel/ring.c \
	src/lib/mux/rtx.c \
	src/lib/demux/rtx.c \
	src/lib/demux/rtp.c \
	src/lib/mux/psi_build.c \
	src/lib/demux/psi/psi.c \
	src/lib/demux/psi/parse.c \
	src/lib/demux/psi/descriptors.c \
	src/lib/demux/psi/section_asm.c \
	src/lib/demux/tspack.c \
	src/lib/demux/crc32.c \
	src/lib/log.c

dipifccret_burst_BIN := tests/unit/dipifccret/test_burst
dipifccret_burst_SRCS := \
	tests/unit/dipifccret/test_burst.c \
	src/dipifccret/fcc/burst.c \
	src/dipifccret/channel/channel.c \
	src/dipifccret/channel/hash.c \
	src/dipifccret/channel/ring.c \
	src/lib/mux/rtx.c \
	src/lib/demux/rtx.c \
	src/lib/demux/rtp.c \
	src/lib/mux/psi_build.c \
	src/lib/demux/psi/psi.c \
	src/lib/demux/psi/parse.c \
	src/lib/demux/psi/descriptors.c \
	src/lib/demux/psi/section_asm.c \
	src/lib/demux/tspack.c \
	src/lib/demux/crc32.c \
	src/lib/log.c

dipifccret_ret_BIN := tests/unit/dipifccret/test_ret
dipifccret_ret_SRCS := \
	tests/unit/dipifccret/test_ret.c \
	src/dipifccret/ret/ret.c \
	src/dipifccret/ret/rtx_session_table.c \
	src/lib/net/sockaddr_index.c \
	src/dipifccret/channel/channel.c \
	src/dipifccret/channel/hash.c \
	src/dipifccret/channel/ring.c \
	src/lib/demux/rtcp.c \
	src/lib/mux/rtcp_build.c \
	src/lib/mux/rtx.c \
	src/lib/demux/rtx.c \
	src/lib/demux/rtp.c \
	src/lib/mux/psi_build.c \
	src/lib/demux/psi/psi.c \
	src/lib/demux/psi/parse.c \
	src/lib/demux/psi/descriptors.c \
	src/lib/demux/psi/section_asm.c \
	src/lib/demux/tspack.c \
	src/lib/demux/crc32.c \
	src/lib/log.c

dipifccret_rtx_session_table_BIN := tests/unit/dipifccret/test_rtx_session_table
dipifccret_rtx_session_table_SRCS := \
	tests/unit/dipifccret/test_rtx_session_table.c \
	src/dipifccret/ret/rtx_session_table.c \
	src/lib/net/sockaddr_index.c

dipifccret_capture_BIN := tests/unit/dipifccret/test_capture
dipifccret_capture_SRCS := \
	tests/unit/dipifccret/test_capture.c \
	src/dipifccret/capture/capture.c \
	src/dipifccret/capture/ranges.c \
	src/dipifccret/capture/bpf.c \
	src/dipifccret/capture/frame.c \
	src/lib/demux/rtp.c \
	src/lib/signal.c

# _BIN/_SRCS/TEST_BINS stay unconditional (unlike TESTS=yes gate below),
# 'make clean' finds these paths regardless of current config.mk state -
# a prior '--tests' build's artifacts must clean up even after
# reconfiguring without --tests.
define UNIT_TEST_template
$(1)_OBJS := $$($(1)_SRCS:.c=.o)
ALL_OBJS += $$($(1)_OBJS)
$$($(1)_OBJS): CFLAGS += $(CHECK_CFLAGS) $$($(1)_EXTRA_CFLAGS)
$$($(1)_BIN): $$($(1)_OBJS)
	$$(CC) $$^ $$(LDFLAGS) $(CHECK_LIBS) -pthread $$($(1)_EXTRA_LDFLAGS) -o $$@
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
INTEGRATION_DIPITVHEAD_SCRIPTS := $(wildcard tests/integration/dipitvhead/*.sh)
INTEGRATION_DIPIRADIOHEAD_SCRIPTS := $(wildcard tests/integration/dipiradiohead/*.sh)
INTEGRATION_DIPIREC_SCRIPTS := $(wildcard tests/integration/dipirec/*.sh)
INTEGRATION_DIPIDESCRAMBLE_SCRIPTS := $(wildcard tests/integration/dipidescramble/*.sh)
INTEGRATION_DIPIMETRICS_SCRIPTS := $(wildcard tests/integration/dipimetrics/*.sh)
INTEGRATION_DIPICAM378_SCRIPTS := $(wildcard tests/integration/dipicam378/*.sh)
INTEGRATION_DIPIFCCRET_SCRIPTS := $(wildcard tests/integration/dipifccret/*.sh)
INTEGRATION_DIPIRIST_SCRIPTS := $(filter-out tests/integration/dipirist/bonding_common.sh,$(wildcard tests/integration/dipirist/*.sh))
INTEGRATION_DIPISRT_SCRIPTS := $(filter-out tests/integration/dipisrt/bonding_common.sh,$(wildcard tests/integration/dipisrt/*.sh))

INTEGRATION_TEST_DEPS := dipibim dipixmltv dipitvhead dipiradiohead dipirec dipidescramble dipisds dipibcg dipimetrics dipifccret
ifeq ($(HAVE_OPENSSL),yes)
INTEGRATION_TEST_DEPS += dipicam378
endif
ifeq ($(HAVE_RIST),yes)
INTEGRATION_TEST_DEPS += dipirist
endif
ifeq ($(HAVE_SRT),yes)
INTEGRATION_TEST_DEPS += dipisrt
endif

.PHONY: integration-test
integration-test: $(INTEGRATION_TEST_DEPS)
	@set -e; \
	for s in $(INTEGRATION_DIPIBIM_SCRIPTS); do echo "running $$s"; sh $$s ./dipibim; done; \
	for s in $(INTEGRATION_DIPIXMLTV_SCRIPTS); do echo "running $$s"; sh $$s ./dipixmltv; done; \
	for s in $(INTEGRATION_DIPITVHEAD_SCRIPTS); do echo "running $$s"; sh $$s ./dipitvhead; done; \
	for s in $(INTEGRATION_DIPIRADIOHEAD_SCRIPTS); do echo "running $$s"; sh $$s ./dipiradiohead; done; \
	for s in $(INTEGRATION_DIPIREC_SCRIPTS); do echo "running $$s"; sh $$s ./dipirec; done; \
	for s in $(INTEGRATION_DIPIDESCRAMBLE_SCRIPTS); do echo "running $$s"; sh $$s ./dipidescramble; done; \
	for s in $(INTEGRATION_DIPIMETRICS_SCRIPTS); do echo "running $$s"; sh $$s ./dipimetrics; done; \
	for s in $(INTEGRATION_DIPIFCCRET_SCRIPTS); do echo "running $$s"; sh $$s ./dipifccret; done
ifeq ($(HAVE_OPENSSL),yes)
	@set -e; for s in $(INTEGRATION_DIPICAM378_SCRIPTS); do echo "running $$s"; sh $$s ./dipicam378; done
else
	@echo "integration-test: OpenSSL not found, skipped dipicam378 integration tests" >&2
endif
ifeq ($(HAVE_RIST),yes)
	@set -e; for s in $(INTEGRATION_DIPIRIST_SCRIPTS); do echo "running $$s"; sh $$s ./dipirist ./dipirec; done
else
	@echo "integration-test: librist not found, skipped dipirist integration tests" >&2
endif
ifeq ($(HAVE_SRT),yes)
	@set -e; for s in $(INTEGRATION_DIPISRT_SCRIPTS); do echo "running $$s"; sh $$s ./dipisrt; done
else
	@echo "integration-test: libsrt not found, skipped dipisrt integration tests" >&2
endif

FUZZ_BIM_DEPS := \
	src/lib/bim/accessunit.c \
	src/lib/bim/fragment.c \
	src/lib/bim/codec.c \
	src/lib/bim/bitwriter.c \
	src/lib/bim/bitreader.c \
	src/lib/bim/strrepo.c \
	src/lib/tva/tva_xml.c \
	src/lib/tva/bcg_doc.c \
	src/lib/xml_util.c \
	src/lib/ioutil.c \
	src/lib/log.c

# _BIN/_SRCS and FUZZ_BINS stay unconditional (unlike FUZZING=yes gate
# below), 'make clean' finds these paths regardless of current config.mk
# state - a prior '--fuzz' build's artifacts must clean up even after
# reconfiguring without --fuzz.
FUZZ_HARNESSES := fuzz_psi fuzz_bim_accessunit fuzz_sds_xml fuzz_rtcp fuzz_simulcrypt_msg fuzz_ecmg_channel_status fuzz_emmg_datagrams fuzz_dvbstp

fuzz_psi_BIN := tests/fuzz/fuzz_psi
fuzz_psi_SRCS := \
	tests/fuzz/fuzz_psi.c \
	src/lib/demux/psi/psi.c \
	src/lib/demux/psi/parse.c \
	src/lib/demux/psi/descriptors.c \
	src/lib/demux/psi/section_asm.c \
	src/lib/demux/tspack.c \
	src/lib/demux/crc32.c \
	src/lib/log.c

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

fuzz_simulcrypt_msg_BIN := tests/fuzz/fuzz_simulcrypt_msg
fuzz_simulcrypt_msg_SRCS := \
	tests/fuzz/fuzz_simulcrypt_msg.c \
	src/lib/cas/simulcrypt_msg.c \
	src/lib/mux/psi_build.c \
	src/lib/demux/crc32.c \
	src/lib/signal.c

# ecmg_client.c/emmg_server.c pull in real pthread usage even though the fuzzed functions
# themselves are pure - link pthread on both like dipitvhead itself does
fuzz_ecmg_channel_status_BIN := tests/fuzz/fuzz_ecmg_channel_status
fuzz_ecmg_channel_status_SRCS := \
	tests/fuzz/fuzz_ecmg_channel_status.c \
	src/lib/ioutil.c \
	src/lib/cas/ecmg_client/ecmg_client.c \
	src/lib/cas/ecmg_client/protocol.c \
	src/lib/cas/ecmg_client/connect.c \
	src/lib/cas/ecmg_client/run.c \
	src/lib/cas/simulcrypt_msg.c \
	src/lib/mux/psi_build.c \
	src/lib/demux/crc32.c \
	src/lib/scrambler/scrambler.c \
	src/lib/scrambler/cissa_stub.c \
	src/lib/scrambler/csa2_stub.c \
	src/lib/log.c \
	src/lib/signal.c
fuzz_ecmg_channel_status_EXTRA_LDFLAGS := -pthread

fuzz_emmg_datagrams_BIN := tests/fuzz/fuzz_emmg_datagrams
fuzz_emmg_datagrams_SRCS := \
	tests/fuzz/fuzz_emmg_datagrams.c \
	src/lib/cas/emmg_server/emmg_server.c \
	src/lib/cas/emmg_server/protocol.c \
	src/lib/cas/emmg_server/worker.c \
	src/lib/cas/simulcrypt_msg.c \
	src/lib/mux/psi_build.c \
	src/lib/demux/crc32.c \
	src/lib/log.c \
	src/lib/signal.c
fuzz_emmg_datagrams_EXTRA_LDFLAGS := -pthread

fuzz_dvbstp_BIN := tests/fuzz/fuzz_dvbstp
fuzz_dvbstp_SRCS := \
	tests/fuzz/fuzz_dvbstp.c \
	src/lib/net/dvbstp.c \
	src/lib/net/multicast.c \
	src/lib/ioutil.c \
	src/lib/net/netconnect.c \
	src/lib/demux/crc32.c \
	src/lib/signal.c \
	src/lib/log.c

fuzz_gen_seeds_BIN := tests/fuzz/gen_seeds
fuzz_gen_seeds_SRCS := \
	tests/fuzz/gen_seeds.c \
	$(FUZZ_BIM_DEPS) \
	src/lib/mux/psi_build.c \
	src/lib/mux/rtcp_build.c \
	src/lib/demux/rtcp.c \
	src/lib/demux/crc32.c \
	src/lib/sds_xml.c \
	src/lib/cas/simulcrypt_msg.c \
	src/lib/net/dvbstp.c \
	src/lib/net/multicast.c \
	src/lib/ioutil.c \
	src/lib/net/netconnect.c \
	src/lib/signal.c

define FUZZ_TARGET_template
$(1)_OBJS := $$($(1)_SRCS:.c=.o)
ALL_OBJS += $$($(1)_OBJS)
$$($(1)_BIN): $$($(1)_OBJS)
	$$(CC) $$^ $$(LDFLAGS) $$($(1)_EXTRA_LDFLAGS) -o $$@
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

MAN_PAGES := $(foreach t,$(TOOLS),src/$(t)/$(t).1) src/dvbipitools/dvbipitools.1

.PHONY: all clean install
all: $(TOOLS) dvbipitools

install: $(TOOLS) dvbipitools
	install -d $(DESTDIR)$(BINDIR)
	install -m 0755 $(TOOLS) dvbipitools $(DESTDIR)$(BINDIR)/
	install -d $(DESTDIR)$(MANDIR)
	install -m 0644 $(MAN_PAGES) $(DESTDIR)$(MANDIR)/

%.o: %.c config.mk
	$(CC) $(CFLAGS) -MMD -MP -c $< -o $@

-include $(ALL_OBJS:.o=.d)

TLS_VARIANTS := src/lib/net/tls.o src/lib/net/tls_stub.o

clean:
	rm -f $(ALL_OBJS) $(ALL_OBJS:.o=.d) $(TLS_VARIANTS) $(TLS_VARIANTS:.o=.d) $(TOOLS) dvbipitools $(TEST_BINS) $(FUZZ_BINS)
	rm -rf build/dvbipitools
