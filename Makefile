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
	src/lib/log.c \
	src/lib/signal.c \
	src/lib/net/multicast.c \
	src/lib/net/udpxy.c \
	src/lib/demux/rtp.c \
	src/lib/demux/crc32.c \
	src/lib/demux/psi.c \
	src/lib/demux/tspack.c \
	src/lib/demux/pes.c \
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

ALL_OBJS :=

define TOOL_template
$(1)_OBJS := $$($(1)_SRCS:.c=.o)
ALL_OBJS += $$($(1)_OBJS)
$$($(1)_OBJS): CFLAGS += $$($(1)_EXTRA_CFLAGS)
$(1): $$($(1)_OBJS)
	$$(CC) $$^ $$(LDFLAGS) $$($(1)_EXTRA_LDFLAGS) -o $$@
endef

$(foreach t,$(TOOLS),$(eval $(call TOOL_template,$(t))))

.PHONY: all clean
all: $(TOOLS)

%.o: %.c config.mk
	$(CC) $(CFLAGS) -MMD -MP -c $< -o $@

-include $(ALL_OBJS:.o=.d)

TLS_VARIANTS := src/lib/net/tls.o src/lib/net/tls_stub.o

clean:
	rm -f $(ALL_OBJS) $(ALL_OBJS:.o=.d) $(TLS_VARIANTS) $(TLS_VARIANTS:.o=.d) $(TOOLS)
