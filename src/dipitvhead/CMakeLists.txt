# Copyright 2026 dvbipitools authors. Licensed under GPL-3.0-or-later.
# See NOTICE and LICENSE for details and authorship information.

option(DIPITVHEAD_TLS "build dipitvhead with HTTPS/TLS support (requires OpenSSL)" ON)

set(DIPITVHEAD_HAVE_TLS FALSE)
if (DIPITVHEAD_TLS)
    if (DVBIPITOOLS_STATIC)
        set(OPENSSL_USE_STATIC_LIBS TRUE)
    endif ()
    find_package(OpenSSL)
    if (OpenSSL_FOUND)
        set(DIPITVHEAD_HAVE_TLS TRUE)
    else ()
        message(WARNING "dipitvhead: OpenSSL not found, building without HTTPS support")
    endif ()
endif ()

if (DIPITVHEAD_HAVE_TLS)
    set(TLS_SRC ../lib/net/tls.c)
else ()
    set(TLS_SRC ../lib/net/tls_stub.c)
endif ()

# keep in sync with CMakeLists/Makefile
add_executable(dipitvhead
    main.c
    args.c
    tvhead.c
    input/source.c
    mux/pmtbuild.c
    mux/aitbuild.c
    mux/remux.c
    mux/bitrate.c
    ../lib/log.c
    ../lib/signal.c
    ../lib/net/multicast.c
    ${TLS_SRC}
    ../lib/net/httpclient.c
    ../lib/demux/crc32.c
    ../lib/demux/psi.c
    ../lib/demux/tspack.c
    ../lib/demux/rtp.c
    ../lib/mux/rtpheader.c
    ../lib/mux/psi_build.c
    ../lib/mux/tspacket_write.c)

if (DIPITVHEAD_HAVE_TLS)
    target_link_libraries(dipitvhead PRIVATE OpenSSL::SSL OpenSSL::Crypto)
endif ()
