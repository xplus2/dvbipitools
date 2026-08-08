# Copyright 2026 dvbipitools authors. Licensed under GPL-3.0-or-later.
# See NOTICE and LICENSE for details and authorship information.

set(THREADS_PREFER_PTHREAD_FLAG ON)
find_package(Threads REQUIRED)

# keep in sync with CMakeLists/Makefile
add_executable(dipifccret
    main.c
    args.c
    capture.c
    channel.c
    listen.c
    ret/ret.c
    ret/mcsend.c
    fcc/burst.c
    ../lib/log.c
    ../lib/argutil.c
    ../lib/signal.c
    ../lib/net/multicast.c
    ../lib/net/netconnect.c
    ../lib/demux/rtp.c
    ../lib/demux/rtcp.c
    ../lib/demux/psi.c
    ../lib/demux/psi_section_asm.c
    ../lib/demux/tspack.c
    ../lib/demux/crc32.c
    ../lib/mux/rtcp_build.c
    ../lib/mux/rtx.c)

target_link_libraries(dipifccret PRIVATE Threads::Threads)

# channel.c's C11 64-bit atomics need libatomic on archs without native 8-byte atomics (32-bit ARM).
# target_link_libraries not link_options: must land after objects, static link order matters.
target_link_libraries(dipifccret PRIVATE atomic)
