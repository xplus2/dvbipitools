# Copyright 2026 dvbipitools authors. Licensed under GPL-3.0-or-later.
# See NOTICE and LICENSE for details and authorship information.

option(DIPIREC_TLS "build dipirec with HTTPS/TLS support (requires OpenSSL)" ON)

set(DIPIREC_HAVE_TLS FALSE)
if (DIPIREC_TLS)
    if (DVBIPITOOLS_STATIC)
        set(OPENSSL_USE_STATIC_LIBS TRUE)
    endif ()
    find_package(OpenSSL)
    if (OpenSSL_FOUND)
        set(DIPIREC_HAVE_TLS TRUE)
    else ()
        message(WARNING "dipirec: OpenSSL not found, building without HTTPS support")
    endif ()
endif ()

if (DIPIREC_HAVE_TLS)
    set(TLS_SRC ../lib/net/tls.c)
else ()
    set(TLS_SRC ../lib/net/tls_stub.c)
endif ()

# keep in sync with CMakeLists/Makefile
add_executable(dipirec
    main.c
    args.c
    record.c
    ret_client.c
    ../lib/log.c
    ../lib/argutil.c
    ../lib/signal.c
    ../lib/net/multicast.c
    ../lib/net/netconnect.c
    ../lib/net/udpxy.c
    ../lib/net/tssource.c
    ${TLS_SRC}
    ../lib/net/httpclient/httpclient.c
    ../lib/net/httpclient/url.c
    ../lib/net/httpclient/read.c
    ../lib/net/httpclient/async.c
    ../lib/ioutil.c
    ../lib/demux/rtp.c
    ../lib/demux/rtx.c
    ../lib/demux/crc32.c
    ../lib/demux/psi.c
    ../lib/demux/psi_section_asm.c
    ../lib/demux/tspack.c
    ../lib/demux/pes.c
    ../lib/demux/mpts_probe.c
    ../lib/mux/rtcp_build.c
    ../lib/mux/ebml.c
    ../lib/mux/mkv/mkv.c
    ../lib/mux/mkv/bitreader.c
    ../lib/mux/mkv/audio.c
    ../lib/mux/mkv/video.c
    ../lib/mux/mkv/write.c
    ../lib/mux/mkv/feed.c
    ../lib/mux/teletext.c
    filter/ts.c
    filter/pace.c)

if (DIPIREC_HAVE_TLS)
    target_link_libraries(dipirec PRIVATE OpenSSL::SSL OpenSSL::Crypto)
endif ()
