# Copyright 2026 dvbipitools authors. Licensed under GPL-3.0-or-later.
# See NOTICE and LICENSE for details and authorship information.

if (DVBIPITOOLS_STATIC)
    set(OPENSSL_USE_STATIC_LIBS TRUE)
endif ()
find_package(OpenSSL)

option(DIPIDESCRAMBLE_CSA2 "build dipidescramble with DVB-CSA2 support (requires libdvbcsa)" ON)

set(DIPIDESCRAMBLE_HAVE_CSA2 FALSE)
if (DIPIDESCRAMBLE_CSA2)
    find_path(DVBCSA_INCLUDE_DIR dvbcsa/dvbcsa.h)
    find_library(DVBCSA_LIBRARY dvbcsa)
    if (DVBCSA_INCLUDE_DIR AND DVBCSA_LIBRARY)
        set(DIPIDESCRAMBLE_HAVE_CSA2 TRUE)
    else ()
        message(WARNING "dipidescramble: libdvbcsa not found, building without CSA2 support")
    endif ()
endif ()

if (DIPIDESCRAMBLE_HAVE_CSA2)
    set(CSA2_SRC ../lib/scrambler/csa2.c)
else ()
    set(CSA2_SRC ../lib/scrambler/csa2_stub.c)
endif ()

if (OpenSSL_FOUND)
    # keep in sync with CMakeLists/Makefile
    add_executable(dipidescramble
        main.c
        pipeline.c
        args.c
        crypto.c
        device.c
        biss_ca_state.c
        emmcache.c
        ipiclient.c
        ../lib/log.c
        ../lib/argutil.c
        ../lib/signal.c
        ../lib/net/multicast.c
        ../lib/net/netconnect.c
        ../lib/net/udpxy.c
        ../lib/net/tssource.c
        ../lib/net/tls.c
        ../lib/net/httpclient/httpclient.c
        ../lib/net/httpclient/url.c
        ../lib/net/httpclient/read.c
        ../lib/net/httpclient/async.c
        ../lib/ioutil.c
        ../lib/demux/crc32.c
        ../lib/demux/psi/psi.c
        ../lib/demux/psi/parse.c
        ../lib/demux/psi/descriptors.c
        ../lib/demux/psi/section_asm.c
        ../lib/demux/rtp.c
        ../lib/demux/tspack.c
        ../lib/demux/pes.c
        ../lib/demux/mpts_probe.c
        ../lib/mux/ebml.c
        ../lib/mux/mkv/mkv.c
        ../lib/mux/mkv/bitreader.c
        ../lib/mux/mkv/audio.c
        ../lib/mux/mkv/video.c
        ../lib/mux/mkv/write.c
        ../lib/mux/mkv/feed.c
        ../lib/mux/teletext.c
        ../lib/mux/psi_build.c
        ../lib/scrambler/scrambler.c
        ../lib/scrambler/cissa.c
        ../lib/cas/biss/biss.c
        ../lib/cas/biss/ca.c
        ../lib/cas/biss/ca_sections.c
        ${CSA2_SRC})

    target_link_libraries(dipidescramble PRIVATE OpenSSL::SSL OpenSSL::Crypto)

    if (DIPIDESCRAMBLE_HAVE_CSA2)
        target_include_directories(dipidescramble PRIVATE ${DVBCSA_INCLUDE_DIR})
        target_link_libraries(dipidescramble PRIVATE ${DVBCSA_LIBRARY})
    endif ()
else ()
    message(WARNING "dipidescramble: OpenSSL not found, skipping this tool entirely (RSA/AES crypto is its whole purpose)")
endif ()
