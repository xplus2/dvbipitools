# Copyright 2026 dvbipitools authors. Licensed under GPL-3.0-or-later.
# See NOTICE and LICENSE for details and authorship information.

if (DVBIPITOOLS_STATIC)
    set(OPENSSL_USE_STATIC_LIBS TRUE)
endif ()
find_package(OpenSSL REQUIRED)

# keep in sync with CMakeLists/Makefile
add_executable(dipiradiohead
    main.c
    args.c
    radiohead.c
    ../lib/log.c
    ../lib/signal.c
    ../lib/net/multicast.c
    ../lib/net/tls.c
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

target_link_libraries(dipiradiohead PRIVATE OpenSSL::SSL OpenSSL::Crypto)
