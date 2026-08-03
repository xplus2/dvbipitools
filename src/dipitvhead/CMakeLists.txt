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

option(DIPITVHEAD_CAS "build dipitvhead with CAS/scrambler support (CISSA requires OpenSSL)" ON)

set(DIPITVHEAD_HAVE_CISSA FALSE)
if (DIPITVHEAD_CAS)
    if (DVBIPITOOLS_STATIC)
        set(OPENSSL_USE_STATIC_LIBS TRUE)
    endif ()
    find_package(OpenSSL)
    if (OpenSSL_FOUND)
        set(DIPITVHEAD_HAVE_CISSA TRUE)
    else ()
        message(WARNING "dipitvhead: OpenSSL not found, building without CISSA support")
    endif ()
endif ()

if (DIPITVHEAD_HAVE_CISSA)
    set(CISSA_SRC ../lib/scrambler/cissa.c)
else ()
    set(CISSA_SRC ../lib/scrambler/cissa_stub.c)
endif ()

option(DIPITVHEAD_CSA2 "build dipitvhead with DVB-CSA2 support (requires libdvbcsa)" ON)

set(DIPITVHEAD_HAVE_CSA2 FALSE)
if (DIPITVHEAD_CSA2)
    find_path(DVBCSA_INCLUDE_DIR dvbcsa/dvbcsa.h)
    find_library(DVBCSA_LIBRARY dvbcsa)
    if (DVBCSA_INCLUDE_DIR AND DVBCSA_LIBRARY)
        set(DIPITVHEAD_HAVE_CSA2 TRUE)
    else ()
        message(WARNING "dipitvhead: libdvbcsa not found, building without CSA2 support")
    endif ()
endif ()

if (DIPITVHEAD_HAVE_CSA2)
    set(CSA2_SRC ../lib/scrambler/csa2.c)
else ()
    set(CSA2_SRC ../lib/scrambler/csa2_stub.c)
endif ()

# keep in sync with CMakeLists/Makefile
add_executable(dipitvhead
    main.c
    args.c
    tvhead.c
    input/source.c
    mux/pmtbuild.c
    mux/aitbuild.c
    mux/cadescbuild.c
    mux/remux.c
    mux/bitrate.c
    cas/simulcrypt_msg.c
    cas/ecmg_client.c
    cas/emmg_server.c
    cas/cas.c
    ../lib/log.c
    ../lib/signal.c
    ../lib/net/multicast.c
    ../lib/net/udpxy.c
    ../lib/net/tssource.c
    ${TLS_SRC}
    ../lib/net/httpclient.c
    ../lib/demux/crc32.c
    ../lib/demux/psi.c
    ../lib/demux/tspack.c
    ../lib/demux/rtp.c
    ../lib/mux/rtpheader.c
    ../lib/mux/psi_build.c
    ../lib/mux/tspacket_write.c
    ../lib/scrambler/scrambler.c
    ${CISSA_SRC}
    ${CSA2_SRC})

if (DIPITVHEAD_HAVE_TLS)
    target_link_libraries(dipitvhead PRIVATE OpenSSL::SSL OpenSSL::Crypto)
endif ()

if (DIPITVHEAD_HAVE_CISSA AND NOT DIPITVHEAD_HAVE_TLS)
    target_link_libraries(dipitvhead PRIVATE OpenSSL::Crypto)
endif ()

if (DIPITVHEAD_HAVE_CSA2)
    target_include_directories(dipitvhead PRIVATE ${DVBCSA_INCLUDE_DIR})
    target_link_libraries(dipitvhead PRIVATE ${DVBCSA_LIBRARY})
endif ()

# ECMG client thread: pthread is a hard dependency from here on, not optional
set(THREADS_PREFER_PTHREAD_FLAG ON)
find_package(Threads REQUIRED)
target_link_libraries(dipitvhead PRIVATE Threads::Threads)
