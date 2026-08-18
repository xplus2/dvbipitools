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
    set(BISS_SRC ../lib/cas/biss/biss.c ../lib/cas/biss/hex.c)
    set(BISS_CA_SRC ../lib/cas/biss/ca.c)
else ()
    set(CISSA_SRC ../lib/scrambler/cissa_stub.c)
    set(BISS_SRC ../lib/cas/biss/stub.c ../lib/cas/biss/hex.c)
    set(BISS_CA_SRC ../lib/cas/biss/ca_stub.c)
endif ()

option(DIPITVHEAD_CSA "build dipitvhead with DVB-CSA (CSA1/CSA2/BISS1) support" ON)

if (DIPITVHEAD_CSA)
    set(CSA2_SRC ../lib/scrambler/csa2.c)
else ()
    set(CSA2_SRC ../lib/scrambler/csa2_stub.c)
endif ()

if (DVBIPITOOLS_HAVE_RIST)
    set(RIST_SRC ../lib/net/ristout.c)
else ()
    set(RIST_SRC ../lib/net/ristout_stub.c)
endif ()

# keep in sync with CMakeLists/Makefile
add_executable(dipitvhead
    main.c
    args.c
    tvhead/tvhead.c
    tvhead/discover.c
    tvhead/output.c
    tvhead/single.c
    tvhead/mpts.c
    input/source.c
    mux/pmtbuild.c
    mux/aitbuild.c
    mux/remux.c
    mux/bitrate.c
    cas/cas.c
    ../lib/mux/cadescbuild.c
    ../lib/cas/cas_args.c
    ../lib/cas/simulcrypt_msg.c
    ../lib/cas/ecmg_client/ecmg_client.c
    ../lib/cas/ecmg_client/protocol.c
    ../lib/cas/ecmg_client/connect.c
    ../lib/cas/ecmg_client/run.c
    ../lib/cas/emmg_server/emmg_server.c
    ../lib/cas/emmg_server/protocol.c
    ../lib/cas/emmg_server/worker.c
    ../lib/cas/cas_group.c
    ../lib/cas/cas_scramble_engine.c
    ../lib/log.c
    ../lib/argutil.c
    ../lib/uriparse.c
    ../lib/signal.c
    ../lib/metrics/protocol.c
    ../lib/metrics/export.c
    ../lib/net/multicast.c
    ../lib/net/netconnect.c
    ../lib/net/udpxy.c
    ../lib/net/tssource.c
    ../lib/net/retryset.c
    ${TLS_SRC}
    ${RIST_SRC}
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
    ../lib/demux/tspack.c
    ../lib/demux/rtp.c
    ../lib/mux/rtpheader.c
    ../lib/mux/psi_build.c
    ../lib/mux/mpts.c
    ../lib/mux/tspacket_write.c
    ../lib/scrambler/scrambler.c
    ${CISSA_SRC}
    ${CSA2_SRC}
    ${BISS_SRC}
    ${BISS_CA_SRC}
    ../lib/cas/biss/ca_sections.c
    ../lib/cas/biss/ca_engine.c)

if (DIPITVHEAD_HAVE_TLS)
    target_link_libraries(dipitvhead PRIVATE OpenSSL::SSL OpenSSL::Crypto)
endif ()

if (DIPITVHEAD_HAVE_CISSA AND NOT DIPITVHEAD_HAVE_TLS)
    target_link_libraries(dipitvhead PRIVATE OpenSSL::Crypto)
endif ()

if (DIPITVHEAD_CSA)
    target_link_libraries(dipitvhead PRIVATE ${CMAKE_DL_LIBS})
endif ()

if (DVBIPITOOLS_HAVE_RIST)
    target_link_libraries(dipitvhead PRIVATE PkgConfig::RIST)
endif ()

# ECMG client thread: pthread is a hard dependency from here on, not optional
set(THREADS_PREFER_PTHREAD_FLAG ON)
find_package(Threads REQUIRED)
target_link_libraries(dipitvhead PRIVATE Threads::Threads)
