# Copyright 2026 dvbipitools authors. Licensed under GPL-3.0-or-later.
# See NOTICE and LICENSE for details and authorship information.

option(DIPIRIST_ENABLED "build dipirist (requires librist)" ON)

set(DIPIRIST_HAVE_RIST FALSE)
if (DIPIRIST_ENABLED AND DVBIPITOOLS_HAVE_RIST)
    set(DIPIRIST_HAVE_RIST TRUE)
endif ()

option(DIPIRIST_TLS "build dipirist with HTTPS/TLS support for -i https:// sources (requires OpenSSL)" ON)

set(DIPIRIST_HAVE_TLS FALSE)
if (DIPIRIST_HAVE_RIST AND DIPIRIST_TLS)
    if (DVBIPITOOLS_STATIC)
        set(OPENSSL_USE_STATIC_LIBS TRUE)
    endif ()
    find_package(OpenSSL)
    if (OpenSSL_FOUND)
        set(DIPIRIST_HAVE_TLS TRUE)
    else ()
        message(WARNING "dipirist: OpenSSL not found, building without HTTPS support")
    endif ()
endif ()

if (DIPIRIST_HAVE_TLS)
    set(TLS_SRC ../lib/net/tls.c)
else ()
    set(TLS_SRC ../lib/net/tls_stub.c)
endif ()

if (DIPIRIST_HAVE_RIST)
    # keep in sync with CMakeLists/Makefile
    add_executable(dipirist
        main.c
        args.c
        bridge.c
        ../lib/log.c
        ../lib/argutil.c
        ../lib/uriparse.c
        ../lib/ioutil.c
        ../lib/signal.c
        ../lib/net/multicast.c
        ../lib/net/netconnect.c
        ../lib/net/tssource.c
        ../lib/net/tssink.c
        ../lib/net/ristout.c
        ${TLS_SRC}
        ../lib/net/httpclient/httpclient.c
        ../lib/net/httpclient/url.c
        ../lib/net/httpclient/read.c
        ../lib/net/httpclient/async.c
        ../lib/demux/rtp.c
        ../lib/mux/rtpheader.c
        ../lib/metrics/protocol.c
        ../lib/metrics/export.c)

    target_link_libraries(dipirist PRIVATE PkgConfig::RIST)
    if (DIPIRIST_HAVE_TLS)
        target_link_libraries(dipirist PRIVATE OpenSSL::SSL OpenSSL::Crypto)
    endif ()
endif ()
