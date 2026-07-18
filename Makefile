# Copyright 2026 dvbipitools authors. Licensed under GPL-3.0-or-later.
# See NOTICE and LICENSE for details and authorship information.

# run ./cofigure first for config.mk

include config.mk

.DEFAULT_GOAL := all

TOOLS := dipirec dipiscan

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

ALL_OBJS :=

define TOOL_template
$(1)_OBJS := $$($(1)_SRCS:.c=.o)
ALL_OBJS += $$($(1)_OBJS)
$(1): $$($(1)_OBJS)
	$$(CC) $$^ $$(LDFLAGS) -o $$@
endef

$(foreach t,$(TOOLS),$(eval $(call TOOL_template,$(t))))

.PHONY: all clean
all: $(TOOLS)

%.o: %.c config.mk
	$(CC) $(CFLAGS) -c $< -o $@

clean:
	rm -f $(ALL_OBJS) $(TOOLS)
