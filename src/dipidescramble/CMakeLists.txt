# Copyright 2026 dvbipitools authors. Licensed under GPL-3.0-or-later.
# See NOTICE and LICENSE for details and authorship information.

if (DVBIPITOOLS_STATIC)
    set(OPENSSL_USE_STATIC_LIBS TRUE)
    set(ATOMIC_LIB atomic)
endif ()
find_package(OpenSSL)

option(DIPIDESCRAMBLE_CSA "build dipidescramble with DVB-CSA (CSA1/CSA2/BISS1) support" ON)

if (DIPIDESCRAMBLE_CSA)
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
        ecm_profile.c
        biss_ca_state.c
        emmcache.c
        ipiclient.c
        ../lib/cas/device_crypto.c
        ../lib/cas/device_state_core.c
        ../lib/metrics/protocol.c
        ../lib/metrics/export.c
        ../lib/log.c
        ../lib/secure_zero.c
        ../lib/argutil.c
        ../lib/uriparse.c
        ../lib/signal.c
        ../lib/net/multicast.c
        ../lib/net/netconnect.c
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
        ../lib/demux/bitreader.c
        ../lib/demux/escodec/aubuild.c
        ../lib/demux/escodec/audio.c
        ../lib/mux/mkv/video.c
        ../lib/demux/escodec/video.c
        ../lib/mux/mkv/write.c
        ../lib/mux/mkv/feed.c
        ../lib/mux/teletext.c
        ../lib/mux/psi_build.c
        ../lib/mux/amf.c
        ../lib/mux/flv/flv.c
        ../lib/mux/flv/feed.c
        ../lib/mux/flv/write.c
        ../lib/net/rtmp/handshake.c
        ../lib/net/rtmp/chunk.c
        ../lib/net/rtmp/session.c
        ../lib/net/rtmp/command.c
        ../lib/net/rtmp/rtmp.c
        ../lib/net/rtmpout.c
        ../lib/net/rtmp/auth.c
        ../lib/scrambler/scrambler.c
        ../lib/scrambler/cissa.c
        ../lib/cas/biss/biss.c
        ../lib/cas/biss/hex.c
        ../lib/cas/biss/ca.c
        ../lib/cas/biss/ca_sections.c
        ${CSA2_SRC})

    target_link_libraries(dipidescramble PRIVATE OpenSSL::SSL OpenSSL::Crypto ${ATOMIC_LIB})

    if (DIPIDESCRAMBLE_CSA)
        target_link_libraries(dipidescramble PRIVATE ${CMAKE_DL_LIBS})
    endif ()
else ()
    message(WARNING "dipidescramble: OpenSSL not found, skipping this tool entirely (RSA/AES crypto is its whole purpose)")
endif ()
