# Copyright 2026 dvbipitools authors. Licensed under GPL-3.0-or-later.
# See NOTICE and LICENSE for details and authorship information.

find_package(PkgConfig)
if (PkgConfig_FOUND)
    pkg_check_modules(PCAP libpcap)
endif ()

if (PCAP_FOUND)
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
        ../lib/signal.c
        ../lib/net/multicast.c
        ../lib/demux/rtp.c
        ../lib/demux/rtcp.c
        ../lib/demux/psi.c
        ../lib/demux/crc32.c
        ../lib/mux/rtcp_build.c
        ../lib/mux/rtx.c)

    target_include_directories(dipifccret PRIVATE ${PCAP_INCLUDE_DIRS})
    target_compile_options(dipifccret PRIVATE ${PCAP_CFLAGS_OTHER})
    target_link_libraries(dipifccret PRIVATE ${PCAP_LIBRARIES} Threads::Threads)

    if (DVBIPITOOLS_STATIC)
        # this libpcap is built --with-dbus; static pulls dbus/systemd/libcap for a capture backend dipifccret never uses - keep this one dynamic
        if (CMAKE_BUILD_TYPE STREQUAL "Release")
            set_target_properties(dipifccret PROPERTIES LINK_OPTIONS "-s")
        else ()
            set_target_properties(dipifccret PROPERTIES LINK_OPTIONS "")
        endif ()
    endif ()
else ()
    message(WARNING "dipifccret: libpcap not found via pkg-config, skipping this tool entirely (capture is its purpose)")
endif ()
