# Copyright 2026 dvbipitools authors. Licensed under GPL-3.0-or-later.
# See NOTICE and LICENSE for details and authorship information.

# keep in sync with CMakeLists/Makefile
add_executable(dipiscan
    main.c
    args.c
    format.c
    scan.c
    ../lib/log.c
    ../lib/signal.c
    ../lib/net/multicast.c
    ../lib/net/udpxy.c
    ../lib/demux/rtp.c
    ../lib/demux/crc32.c
    ../lib/demux/psi.c
    ../lib/demux/tspack.c)
