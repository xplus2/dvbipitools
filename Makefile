# Copyright 2026 dvbipitools authors. Licensed under GPL-3.0-or-later.
# See NOTICE and LICENSE for details and authorship information.

# run ./cofigure first for config.mk

include config.mk

.DEFAULT_GOAL := all

TOOLS := dipirec dipiscan dipiradiohead

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
	src/lib/net/multicast.c \
	src/lib/net/udpxy.c \
	src/lib/demux/rtp.c \
	src/lib/demux/crc32.c \
	src/lib/demux/psi.c \
	src/lib/demux/tspack.c

dipiradiohead_SRCS := \
	src/dipiradiohead/main.c \
	src/dipiradiohead/args.c \
	src/dipiradiohead/radiohead.c \
	src/lib/log.c \
	src/lib/signal.c \
	src/lib/net/multicast.c \
	src/lib/net/tls.c \
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
	src/dipiradiohead/mux/rtpheader.c

dipiradiohead_EXTRA_CFLAGS := $(shell pkg-config --cflags openssl)
ifneq (,$(findstring -static,$(LDFLAGS)))
dipiradiohead_EXTRA_LDFLAGS := $(shell pkg-config --static --libs openssl)
else
dipiradiohead_EXTRA_LDFLAGS := $(shell pkg-config --libs openssl)
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

.PHONY: all clean
all: $(TOOLS)

%.o: %.c config.mk
	$(CC) $(CFLAGS) -c $< -o $@

clean:
	rm -f $(ALL_OBJS) $(TOOLS)
