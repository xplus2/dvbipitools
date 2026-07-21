# Copyright 2026 dvbipitools authors. Licensed under GPL-3.0-or-later.
# See NOTICE and LICENSE for details and authorship information.

# keep in sync with CMakeLists/Makefile
add_executable(dipisds
    main.c
    args.c
    input.c
    sds_xml.c
    format_out.c
    announce.c
    listen.c
    ../lib/log.c
    ../lib/signal.c
    ../lib/net/multicast.c
    ../lib/net/dvbstp.c
    ../lib/demux/crc32.c)
