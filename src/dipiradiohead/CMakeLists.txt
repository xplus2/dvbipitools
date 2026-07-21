# Copyright 2026 dvbipitools authors. Licensed under GPL-3.0-or-later.
# See NOTICE and LICENSE for details and authorship information.

option(DIPIRADIOHEAD_TLS "build dipiradiohead with HTTPS/TLS support (requires OpenSSL)" ON)

set(DIPIRADIOHEAD_HAVE_TLS FALSE)
if (DIPIRADIOHEAD_TLS)
    if (DVBIPITOOLS_STATIC)
        set(OPENSSL_USE_STATIC_LIBS TRUE)
    endif ()
    find_package(OpenSSL)
    if (OpenSSL_FOUND)
        set(DIPIRADIOHEAD_HAVE_TLS TRUE)
    else ()
        message(WARNING "dipiradiohead: OpenSSL not found, building without HTTPS support")
    endif ()
endif ()

if (DIPIRADIOHEAD_HAVE_TLS)
    set(TLS_SRC ../lib/net/tls.c)
else ()
    set(TLS_SRC ../lib/net/tls_stub.c)
endif ()

# keep in sync with CMakeLists/Makefile
add_executable(dipiradiohead
    main.c
    args.c
    radiohead.c
    ../lib/log.c
    ../lib/signal.c
    ../lib/net/multicast.c
    ${TLS_SRC}
    ../lib/net/httpclient.c
    ../lib/demux/crc32.c
    ../lib/demux/bitreader.c
    input/playlist.c
    input/icy.c
    input/id3.c
    input/source.c
    framer/mpegaudio.c
    framer/aac_adts.c
    framer/aac_latm.c
    mux/psi.c
    mux/pes.c
    mux/tspacketizer.c
    mux/rtpheader.c)

if (DIPIRADIOHEAD_HAVE_TLS)
    target_link_libraries(dipiradiohead PRIVATE OpenSSL::SSL OpenSSL::Crypto)
endif ()
