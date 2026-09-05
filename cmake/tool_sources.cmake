# Copyright 2026 dvbipitools authors. Licensed under GPL-3.0-or-later.
# See NOTICE and LICENSE for details and authorship information.

# per-tool source resolution, shared by tool's own CMakeLists.txt and dvbipitools
# multicall build. absolute paths only: CMAKE_CURRENT_SOURCE_DIR reflects caller
# dir, not this file's dir.

function(dipibcg_resolve_sources)
    option(DIPIBCG_ZLIB "build dipibcg with zlib compression support for BCG containers" ON)
    set(DIPIBCG_HAVE_ZLIB FALSE)
    if (DIPIBCG_ZLIB)
        if (DVBIPITOOLS_STATIC)
            set(SAVED_SUFFIXES ${CMAKE_FIND_LIBRARY_SUFFIXES})
            set(CMAKE_FIND_LIBRARY_SUFFIXES .a)
        endif ()
        find_package(ZLIB)
        if (DVBIPITOOLS_STATIC)
            set(CMAKE_FIND_LIBRARY_SUFFIXES ${SAVED_SUFFIXES})
        endif ()
        if (ZLIB_FOUND)
            set(DIPIBCG_HAVE_ZLIB TRUE)
        else ()
            message(WARNING "dipibcg: zlib not found, building without BCG container compression support")
        endif ()
    endif ()
    if (DIPIBCG_HAVE_ZLIB)
        set(ZLIB_SRC ${CMAKE_SOURCE_DIR}/src/dipibcg/wrapper.c)
    else ()
        set(ZLIB_SRC ${CMAKE_SOURCE_DIR}/src/dipibcg/wrapper_stub.c)
    endif ()
    set(DIPIBCG_SRCS
            ${CMAKE_SOURCE_DIR}/src/dipibcg/main.c
            ${CMAKE_SOURCE_DIR}/src/dipibcg/args.c
            ${CMAKE_SOURCE_DIR}/src/dipibcg/announce.c
            ${CMAKE_SOURCE_DIR}/src/dipibcg/listen.c
            ${CMAKE_SOURCE_DIR}/src/dipibcg/container.c
            ${ZLIB_SRC}
            ${CMAKE_SOURCE_DIR}/src/lib/helper/log.c
            ${CMAKE_SOURCE_DIR}/src/lib/helper/argutil.c
            ${CMAKE_SOURCE_DIR}/src/lib/helper/uriparse.c
            ${CMAKE_SOURCE_DIR}/src/lib/helper/signal.c
            ${CMAKE_SOURCE_DIR}/src/lib/metrics/protocol.c
            ${CMAKE_SOURCE_DIR}/src/lib/metrics/export.c
            ${CMAKE_SOURCE_DIR}/src/lib/helper/xml_util.c
            ${CMAKE_SOURCE_DIR}/src/lib/helper/ioutil.c
            ${CMAKE_SOURCE_DIR}/src/lib/net/multicast.c
            ${CMAKE_SOURCE_DIR}/src/lib/net/netconnect.c
            ${CMAKE_SOURCE_DIR}/src/lib/net/dvbstp.c
            ${CMAKE_SOURCE_DIR}/src/lib/demux/crc32.c
            ${CMAKE_SOURCE_DIR}/src/lib/tva/bcg_doc.c
            ${CMAKE_SOURCE_DIR}/src/lib/tva/tva_xml.c
            ${CMAKE_SOURCE_DIR}/src/lib/tva/mapping.c
            ${CMAKE_SOURCE_DIR}/src/lib/tva/xmltv.c
            ${CMAKE_SOURCE_DIR}/src/lib/tva/timefmt.c
            ${CMAKE_SOURCE_DIR}/src/lib/bim/bitwriter.c
            ${CMAKE_SOURCE_DIR}/src/lib/bim/bitreader.c
            ${CMAKE_SOURCE_DIR}/src/lib/bim/strrepo.c
            ${CMAKE_SOURCE_DIR}/src/lib/bim/codec.c
            ${CMAKE_SOURCE_DIR}/src/lib/bim/fragment.c
            ${CMAKE_SOURCE_DIR}/src/lib/bim/accessunit.c)
    set(DIPIBCG_SRCS ${DIPIBCG_SRCS} PARENT_SCOPE)
    set(DIPIBCG_HAVE_ZLIB ${DIPIBCG_HAVE_ZLIB} PARENT_SCOPE)
endfunction()

function(dipibim_resolve_sources)
    set(DIPIBIM_SRCS
            ${CMAKE_SOURCE_DIR}/src/dipibim/main.c
            ${CMAKE_SOURCE_DIR}/src/dipibim/args.c
            ${CMAKE_SOURCE_DIR}/src/lib/helper/log.c
            ${CMAKE_SOURCE_DIR}/src/lib/helper/argutil.c
            ${CMAKE_SOURCE_DIR}/src/lib/helper/xml_util.c
            ${CMAKE_SOURCE_DIR}/src/lib/helper/ioutil.c
            ${CMAKE_SOURCE_DIR}/src/lib/tva/bcg_doc.c
            ${CMAKE_SOURCE_DIR}/src/lib/tva/tva_xml.c
            ${CMAKE_SOURCE_DIR}/src/lib/bim/bitwriter.c
            ${CMAKE_SOURCE_DIR}/src/lib/bim/bitreader.c
            ${CMAKE_SOURCE_DIR}/src/lib/bim/strrepo.c
            ${CMAKE_SOURCE_DIR}/src/lib/bim/codec.c
            ${CMAKE_SOURCE_DIR}/src/lib/bim/fragment.c
            ${CMAKE_SOURCE_DIR}/src/lib/bim/accessunit.c)
    set(DIPIBIM_SRCS ${DIPIBIM_SRCS} PARENT_SCOPE)
endfunction()

function(dipicam378_resolve_sources)
    if (DVBIPITOOLS_STATIC)
        set(OPENSSL_USE_STATIC_LIBS TRUE)
        set(ATOMIC_LIB atomic)
    endif ()
    find_package(OpenSSL)
    set(DIPICAM378_FOUND ${OpenSSL_FOUND} PARENT_SCOPE)
    if (NOT OpenSSL_FOUND)
        message(WARNING "dipicam378: OpenSSL not found, skipping this tool entirely (RSA/AES crypto is its whole purpose)")
        return()
    endif ()
    set(THREADS_PREFER_PTHREAD_FLAG ON)
    find_package(Threads REQUIRED)
    set(DIPICAM378_SRCS
            ${CMAKE_SOURCE_DIR}/src/dipicam378/main.c
            ${CMAKE_SOURCE_DIR}/src/dipicam378/args.c
            ${CMAKE_SOURCE_DIR}/src/dipicam378/cs378x/cs378x.c
            ${CMAKE_SOURCE_DIR}/src/dipicam378/cs378x/crypto.c
            ${CMAKE_SOURCE_DIR}/src/dipicam378/cs378x/protocol.c
            ${CMAKE_SOURCE_DIR}/src/dipicam378/cs378x/worker.c
            ${CMAKE_SOURCE_DIR}/src/dipicam378/device.c
            ${CMAKE_SOURCE_DIR}/src/lib/cas/device_crypto.c
            ${CMAKE_SOURCE_DIR}/src/lib/cas/device_state_core.c
            ${CMAKE_SOURCE_DIR}/src/lib/metrics/protocol.c
            ${CMAKE_SOURCE_DIR}/src/lib/metrics/export.c
            ${CMAKE_SOURCE_DIR}/src/lib/net/netconnect.c
            ${CMAKE_SOURCE_DIR}/src/lib/helper/log.c
            ${CMAKE_SOURCE_DIR}/src/lib/helper/argutil.c
            ${CMAKE_SOURCE_DIR}/src/lib/helper/ioutil.c
            ${CMAKE_SOURCE_DIR}/src/lib/helper/signal.c
            ${CMAKE_SOURCE_DIR}/src/lib/helper/secure_zero.c)
    set(DIPICAM378_SRCS ${DIPICAM378_SRCS} PARENT_SCOPE)
    set(DIPICAM378_ATOMIC_LIB ${ATOMIC_LIB} PARENT_SCOPE)
endfunction()

function(dipidescramble_resolve_sources)
    if (DVBIPITOOLS_STATIC)
        set(OPENSSL_USE_STATIC_LIBS TRUE)
        set(ATOMIC_LIB atomic)
    endif ()
    find_package(OpenSSL)
    set(DIPIDESCRAMBLE_FOUND ${OpenSSL_FOUND} PARENT_SCOPE)
    if (NOT OpenSSL_FOUND)
        message(WARNING "dipidescramble: OpenSSL not found, skipping this tool entirely (RSA/AES crypto is its whole purpose)")
        return()
    endif ()
    option(DIPIDESCRAMBLE_CSA "build dipidescramble with DVB-CSA (CSA1/CSA2/BISS1) support" ON)
    if (DIPIDESCRAMBLE_CSA)
        set(CSA2_SRC ${CMAKE_SOURCE_DIR}/src/lib/scrambler/csa2.c)
    else ()
        set(CSA2_SRC ${CMAKE_SOURCE_DIR}/src/lib/scrambler/csa2_stub.c)
    endif ()
    if (DVBIPITOOLS_HAVE_RIST)
        set(RIST_SRC ${CMAKE_SOURCE_DIR}/src/lib/net/rist/ristin.c
                ${CMAKE_SOURCE_DIR}/src/lib/net/rist/ristlog.c)
    else ()
        set(RIST_SRC ${CMAKE_SOURCE_DIR}/src/lib/net/rist/ristin_stub.c
                ${CMAKE_SOURCE_DIR}/src/lib/net/rist/ristlog_stub.c)
    endif ()
    if (DVBIPITOOLS_HAVE_SRT)
        set(SRT_SRC ${CMAKE_SOURCE_DIR}/src/lib/net/srt/srtsrc.c
                ${CMAKE_SOURCE_DIR}/src/lib/net/srt/srtin.c
                ${CMAKE_SOURCE_DIR}/src/lib/net/srt/srtsink.c
                ${CMAKE_SOURCE_DIR}/src/lib/net/srt/srtout.c
                ${CMAKE_SOURCE_DIR}/src/lib/net/srt/srtcommon.c)
    else ()
        set(SRT_SRC ${CMAKE_SOURCE_DIR}/src/lib/net/srt/srtsrc_stub.c
                ${CMAKE_SOURCE_DIR}/src/lib/net/srt/srtsink_stub.c)
    endif ()
    set(THREADS_PREFER_PTHREAD_FLAG ON)
    find_package(Threads REQUIRED)
    set(DIPIDESCRAMBLE_SRCS
            ${CMAKE_SOURCE_DIR}/src/dipidescramble/main.c
            ${CMAKE_SOURCE_DIR}/src/dipidescramble/pipeline.c
            ${CMAKE_SOURCE_DIR}/src/dipidescramble/args.c
            ${CMAKE_SOURCE_DIR}/src/dipidescramble/crypto.c
            ${CMAKE_SOURCE_DIR}/src/dipidescramble/device.c
            ${CMAKE_SOURCE_DIR}/src/dipidescramble/ecm_profile/common.c
            ${CMAKE_SOURCE_DIR}/src/dipidescramble/ecm_profile/parse.c
            ${CMAKE_SOURCE_DIR}/src/dipidescramble/ecm_profile/validate.c
            ${CMAKE_SOURCE_DIR}/src/dipidescramble/ecm_profile/wire.c
            ${CMAKE_SOURCE_DIR}/src/dipidescramble/ecm_profile/crypto.c
            ${CMAKE_SOURCE_DIR}/src/dipidescramble/biss_ca_state.c
            ${CMAKE_SOURCE_DIR}/src/dipidescramble/emmcache.c
            ${CMAKE_SOURCE_DIR}/src/dipidescramble/ipiclient.c
            ${CMAKE_SOURCE_DIR}/src/lib/cas/device_crypto.c
            ${CMAKE_SOURCE_DIR}/src/lib/cas/device_state_core.c
            ${CMAKE_SOURCE_DIR}/src/lib/metrics/protocol.c
            ${CMAKE_SOURCE_DIR}/src/lib/metrics/export.c
            ${CMAKE_SOURCE_DIR}/src/lib/helper/log.c
            ${CMAKE_SOURCE_DIR}/src/lib/helper/secure_zero.c
            ${CMAKE_SOURCE_DIR}/src/lib/helper/argutil.c
            ${CMAKE_SOURCE_DIR}/src/lib/helper/uriparse.c
            ${CMAKE_SOURCE_DIR}/src/lib/helper/signal.c
            ${CMAKE_SOURCE_DIR}/src/lib/net/multicast.c
            ${CMAKE_SOURCE_DIR}/src/lib/net/netconnect.c
            ${CMAKE_SOURCE_DIR}/src/lib/net/tssource.c
            ${CMAKE_SOURCE_DIR}/src/lib/net/tls.c
            ${CMAKE_SOURCE_DIR}/src/lib/net/httpclient/httpclient.c
            ${CMAKE_SOURCE_DIR}/src/lib/net/httpclient/url.c
            ${CMAKE_SOURCE_DIR}/src/lib/net/httpclient/read.c
            ${CMAKE_SOURCE_DIR}/src/lib/vendor/picohttpparser/picohttpparser.c
            ${CMAKE_SOURCE_DIR}/src/lib/net/httpclient/async.c
            ${CMAKE_SOURCE_DIR}/src/lib/helper/ioutil.c
            ${CMAKE_SOURCE_DIR}/src/lib/demux/crc32.c
            ${CMAKE_SOURCE_DIR}/src/lib/demux/psi/psi.c
            ${CMAKE_SOURCE_DIR}/src/lib/demux/psi/parse.c
            ${CMAKE_SOURCE_DIR}/src/lib/demux/psi/descriptors.c
            ${CMAKE_SOURCE_DIR}/src/lib/demux/psi/section_asm.c
            ${CMAKE_SOURCE_DIR}/src/lib/demux/rtp.c
            ${CMAKE_SOURCE_DIR}/src/lib/demux/tspack.c
            ${CMAKE_SOURCE_DIR}/src/lib/demux/pes.c
            ${CMAKE_SOURCE_DIR}/src/lib/demux/mpts_probe.c
            ${CMAKE_SOURCE_DIR}/src/lib/mux/ebml.c
            ${CMAKE_SOURCE_DIR}/src/lib/mux/mkv/mkv.c
            ${CMAKE_SOURCE_DIR}/src/lib/demux/bitreader.c
            ${CMAKE_SOURCE_DIR}/src/lib/demux/escodec/aubuild.c
            ${CMAKE_SOURCE_DIR}/src/lib/demux/escodec/audio.c
            ${CMAKE_SOURCE_DIR}/src/lib/mux/mkv/video.c
            ${CMAKE_SOURCE_DIR}/src/lib/demux/escodec/video.c
            ${CMAKE_SOURCE_DIR}/src/lib/mux/mkv/write.c
            ${CMAKE_SOURCE_DIR}/src/lib/mux/mkv/feed.c
            ${CMAKE_SOURCE_DIR}/src/lib/mux/teletext.c
            ${CMAKE_SOURCE_DIR}/src/lib/mux/psi_build.c
            ${CMAKE_SOURCE_DIR}/src/lib/mux/amf.c
            ${CMAKE_SOURCE_DIR}/src/lib/mux/flv/flv.c
            ${CMAKE_SOURCE_DIR}/src/lib/mux/flv/feed.c
            ${CMAKE_SOURCE_DIR}/src/lib/mux/flv/write.c
            ${CMAKE_SOURCE_DIR}/src/lib/net/rtmp/handshake.c
            ${CMAKE_SOURCE_DIR}/src/lib/net/rtmp/chunk.c
            ${CMAKE_SOURCE_DIR}/src/lib/net/rtmp/session.c
            ${CMAKE_SOURCE_DIR}/src/lib/net/rtmp/command.c
            ${CMAKE_SOURCE_DIR}/src/lib/net/rtmp/rtmp.c
            ${CMAKE_SOURCE_DIR}/src/lib/net/rtmp/rtmpout.c
            ${CMAKE_SOURCE_DIR}/src/lib/net/rtmp/auth.c
            ${CMAKE_SOURCE_DIR}/src/lib/scrambler/scrambler.c
            ${CMAKE_SOURCE_DIR}/src/lib/scrambler/cissa.c
            ${CMAKE_SOURCE_DIR}/src/lib/cas/biss/biss.c
            ${CMAKE_SOURCE_DIR}/src/lib/cas/biss/hex.c
            ${CMAKE_SOURCE_DIR}/src/lib/cas/biss/ca.c
            ${CMAKE_SOURCE_DIR}/src/lib/cas/biss/ca_sections.c
            ${CSA2_SRC}
            ${RIST_SRC}
            ${SRT_SRC})
    set(DIPIDESCRAMBLE_SRCS ${DIPIDESCRAMBLE_SRCS} PARENT_SCOPE)
    set(DIPIDESCRAMBLE_CSA ${DIPIDESCRAMBLE_CSA} PARENT_SCOPE)
    set(DIPIDESCRAMBLE_ATOMIC_LIB ${ATOMIC_LIB} PARENT_SCOPE)
endfunction()

function(dipifccret_resolve_sources)
    set(THREADS_PREFER_PTHREAD_FLAG ON)
    find_package(Threads REQUIRED)
    set(DIPIFCCRET_SRCS
            ${CMAKE_SOURCE_DIR}/src/dipifccret/main.c
            ${CMAKE_SOURCE_DIR}/src/dipifccret/run/dispatch.c
            ${CMAKE_SOURCE_DIR}/src/dipifccret/run/pacer.c
            ${CMAKE_SOURCE_DIR}/src/dipifccret/run/rsi.c
            ${CMAKE_SOURCE_DIR}/src/dipifccret/run/metrics.c
            ${CMAKE_SOURCE_DIR}/src/dipifccret/args.c
            ${CMAKE_SOURCE_DIR}/src/dipifccret/capture/capture.c
            ${CMAKE_SOURCE_DIR}/src/dipifccret/capture/ranges.c
            ${CMAKE_SOURCE_DIR}/src/dipifccret/capture/bpf.c
            ${CMAKE_SOURCE_DIR}/src/dipifccret/capture/frame.c
            ${CMAKE_SOURCE_DIR}/src/dipifccret/channel/channel.c
            ${CMAKE_SOURCE_DIR}/src/dipifccret/channel/hash.c
            ${CMAKE_SOURCE_DIR}/src/dipifccret/channel/ring.c
            ${CMAKE_SOURCE_DIR}/src/dipifccret/listen.c
            ${CMAKE_SOURCE_DIR}/src/dipifccret/ret/ret.c
            ${CMAKE_SOURCE_DIR}/src/dipifccret/ret/rtx_session_table.c
            ${CMAKE_SOURCE_DIR}/src/dipifccret/ret/mcsend.c
            ${CMAKE_SOURCE_DIR}/src/dipifccret/fcc/burst.c
            ${CMAKE_SOURCE_DIR}/src/dipifccret/fcc/burst_table.c
            ${CMAKE_SOURCE_DIR}/src/lib/metrics/protocol.c
            ${CMAKE_SOURCE_DIR}/src/lib/metrics/export.c
            ${CMAKE_SOURCE_DIR}/src/lib/helper/log.c
            ${CMAKE_SOURCE_DIR}/src/lib/helper/argutil.c
            ${CMAKE_SOURCE_DIR}/src/lib/helper/signal.c
            ${CMAKE_SOURCE_DIR}/src/lib/helper/ioutil.c
            ${CMAKE_SOURCE_DIR}/src/lib/net/sockaddr_index.c
            ${CMAKE_SOURCE_DIR}/src/lib/net/multicast.c
            ${CMAKE_SOURCE_DIR}/src/lib/net/netconnect.c
            ${CMAKE_SOURCE_DIR}/src/lib/demux/rtp.c
            ${CMAKE_SOURCE_DIR}/src/lib/demux/rtcp.c
            ${CMAKE_SOURCE_DIR}/src/lib/demux/psi/psi.c
            ${CMAKE_SOURCE_DIR}/src/lib/demux/psi/parse.c
            ${CMAKE_SOURCE_DIR}/src/lib/demux/psi/descriptors.c
            ${CMAKE_SOURCE_DIR}/src/lib/demux/psi/section_asm.c
            ${CMAKE_SOURCE_DIR}/src/lib/demux/tspack.c
            ${CMAKE_SOURCE_DIR}/src/lib/demux/crc32.c
            ${CMAKE_SOURCE_DIR}/src/lib/mux/rtcp_build.c
            ${CMAKE_SOURCE_DIR}/src/lib/mux/rtx.c)
    set(DIPIFCCRET_SRCS ${DIPIFCCRET_SRCS} PARENT_SCOPE)
endfunction()

function(dipimetrics_resolve_sources)
    set(DIPIMETRICS_SRCS
            ${CMAKE_SOURCE_DIR}/src/dipimetrics/main.c
            ${CMAKE_SOURCE_DIR}/src/dipimetrics/args.c
            ${CMAKE_SOURCE_DIR}/src/dipimetrics/store.c
            ${CMAKE_SOURCE_DIR}/src/dipimetrics/render.c
            ${CMAKE_SOURCE_DIR}/src/dipimetrics/httpserver.c
            ${CMAKE_SOURCE_DIR}/src/lib/vendor/picohttpparser/picohttpparser.c
            ${CMAKE_SOURCE_DIR}/src/lib/helper/log.c
            ${CMAKE_SOURCE_DIR}/src/lib/helper/argutil.c
            ${CMAKE_SOURCE_DIR}/src/lib/helper/signal.c
            ${CMAKE_SOURCE_DIR}/src/lib/helper/ioutil.c
            ${CMAKE_SOURCE_DIR}/src/lib/metrics/protocol.c)
    set(DIPIMETRICS_SRCS ${DIPIMETRICS_SRCS} PARENT_SCOPE)
endfunction()

function(dipiradiohead_resolve_sources)
    option(DIPIRADIOHEAD_TLS "build dipiradiohead with HTTPS/TLS support (requires OpenSSL)" ON)
    set(DIPIRADIOHEAD_HAVE_TLS FALSE)
    if (DIPIRADIOHEAD_TLS)
        if (DVBIPITOOLS_STATIC)
            set(OPENSSL_USE_STATIC_LIBS TRUE)
            set(ATOMIC_LIB atomic)
        endif ()
        find_package(OpenSSL)
        if (OpenSSL_FOUND)
            set(DIPIRADIOHEAD_HAVE_TLS TRUE)
        else ()
            message(WARNING "dipiradiohead: OpenSSL not found, building without HTTPS support")
        endif ()
    endif ()
    if (DIPIRADIOHEAD_HAVE_TLS)
        set(TLS_SRC ${CMAKE_SOURCE_DIR}/src/lib/net/tls.c)
    else ()
        set(TLS_SRC ${CMAKE_SOURCE_DIR}/src/lib/net/tls_stub.c)
    endif ()

    option(DIPIRADIOHEAD_CAS "build dipiradiohead with CAS/scrambler support (CISSA requires OpenSSL)" ON)
    set(DIPIRADIOHEAD_HAVE_CISSA FALSE)
    if (DIPIRADIOHEAD_CAS)
        if (DVBIPITOOLS_STATIC)
            set(OPENSSL_USE_STATIC_LIBS TRUE)
            set(ATOMIC_LIB atomic)
        endif ()
        find_package(OpenSSL)
        if (OpenSSL_FOUND)
            set(DIPIRADIOHEAD_HAVE_CISSA TRUE)
        else ()
            message(WARNING "dipiradiohead: OpenSSL not found, building without CISSA support")
        endif ()
    endif ()
    if (DIPIRADIOHEAD_HAVE_CISSA)
        set(CISSA_SRC ${CMAKE_SOURCE_DIR}/src/lib/scrambler/cissa.c)
        set(BISS_SRC ${CMAKE_SOURCE_DIR}/src/lib/cas/biss/biss.c ${CMAKE_SOURCE_DIR}/src/lib/cas/biss/hex.c)
        set(BISS_CA_SRC ${CMAKE_SOURCE_DIR}/src/lib/cas/biss/ca.c)
        set(CWENC_SRC ${CMAKE_SOURCE_DIR}/src/lib/cas/ecmg_client/cw_encryption.c)
    else ()
        set(CISSA_SRC ${CMAKE_SOURCE_DIR}/src/lib/scrambler/cissa_stub.c)
        set(BISS_SRC ${CMAKE_SOURCE_DIR}/src/lib/cas/biss/stub.c ${CMAKE_SOURCE_DIR}/src/lib/cas/biss/hex.c)
        set(BISS_CA_SRC ${CMAKE_SOURCE_DIR}/src/lib/cas/biss/ca_stub.c)
        set(CWENC_SRC ${CMAKE_SOURCE_DIR}/src/lib/cas/ecmg_client/cw_encryption_stub.c)
    endif ()

    option(DIPIRADIOHEAD_CSA "build dipiradiohead with DVB-CSA (CSA1/CSA2/BISS1) support" ON)
    if (DIPIRADIOHEAD_CSA)
        set(CSA2_SRC ${CMAKE_SOURCE_DIR}/src/lib/scrambler/csa2.c)
    else ()
        set(CSA2_SRC ${CMAKE_SOURCE_DIR}/src/lib/scrambler/csa2_stub.c)
    endif ()

    if (DVBIPITOOLS_HAVE_RIST)
        set(RIST_SRC ${CMAKE_SOURCE_DIR}/src/lib/net/rist/ristout.c
                ${CMAKE_SOURCE_DIR}/src/lib/net/rist/ristlog.c)
    else ()
        set(RIST_SRC ${CMAKE_SOURCE_DIR}/src/lib/net/rist/ristout_stub.c
                ${CMAKE_SOURCE_DIR}/src/lib/net/rist/ristlog_stub.c)
    endif ()

    if (DVBIPITOOLS_HAVE_SRT)
        set(SRT_SRC ${CMAKE_SOURCE_DIR}/src/lib/net/srt/srtsink.c
                ${CMAKE_SOURCE_DIR}/src/lib/net/srt/srtout.c
                ${CMAKE_SOURCE_DIR}/src/lib/net/srt/srtcommon.c)
    else ()
        set(SRT_SRC ${CMAKE_SOURCE_DIR}/src/lib/net/srt/srtsink_stub.c)
    endif ()

    set(DIPIRADIOHEAD_SRCS
            ${CMAKE_SOURCE_DIR}/src/dipiradiohead/main.c
            ${CMAKE_SOURCE_DIR}/src/dipiradiohead/args.c
            ${CMAKE_SOURCE_DIR}/src/dipiradiohead/radiohead/radiohead.c
            ${CMAKE_SOURCE_DIR}/src/dipiradiohead/radiohead/metrics.c
            ${CMAKE_SOURCE_DIR}/src/dipiradiohead/radiohead/mpts.c
            ${CMAKE_SOURCE_DIR}/src/lib/helper/log.c
            ${CMAKE_SOURCE_DIR}/src/lib/helper/toolmain.c
            ${CMAKE_SOURCE_DIR}/src/lib/helper/argutil.c
            ${CMAKE_SOURCE_DIR}/src/lib/helper/uriparse.c
            ${CMAKE_SOURCE_DIR}/src/lib/helper/signal.c
            ${CMAKE_SOURCE_DIR}/src/lib/metrics/protocol.c
            ${CMAKE_SOURCE_DIR}/src/lib/metrics/export.c
            ${CMAKE_SOURCE_DIR}/src/lib/net/multicast.c
            ${CMAKE_SOURCE_DIR}/src/lib/net/netconnect.c
            ${TLS_SRC}
            ${RIST_SRC}
            ${SRT_SRC}
            ${CMAKE_SOURCE_DIR}/src/lib/net/httpclient/httpclient.c
            ${CMAKE_SOURCE_DIR}/src/lib/net/httpclient/url.c
            ${CMAKE_SOURCE_DIR}/src/lib/net/httpclient/read.c
            ${CMAKE_SOURCE_DIR}/src/lib/vendor/picohttpparser/picohttpparser.c
            ${CMAKE_SOURCE_DIR}/src/lib/net/httpclient/async.c
            ${CMAKE_SOURCE_DIR}/src/lib/helper/ioutil.c
            ${CMAKE_SOURCE_DIR}/src/lib/demux/crc32.c
            ${CMAKE_SOURCE_DIR}/src/lib/demux/bitreader.c
            ${CMAKE_SOURCE_DIR}/src/dipiradiohead/input/playlist.c
            ${CMAKE_SOURCE_DIR}/src/dipiradiohead/input/icy.c
            ${CMAKE_SOURCE_DIR}/src/dipiradiohead/input/id3.c
            ${CMAKE_SOURCE_DIR}/src/dipiradiohead/input/source/open.c
            ${CMAKE_SOURCE_DIR}/src/dipiradiohead/input/source/open_async.c
            ${CMAKE_SOURCE_DIR}/src/dipiradiohead/input/source/frame.c
            ${CMAKE_SOURCE_DIR}/src/dipiradiohead/input/inputset.c
            ${CMAKE_SOURCE_DIR}/src/lib/net/retryset.c
            ${CMAKE_SOURCE_DIR}/src/dipiradiohead/framer/mpegaudio.c
            ${CMAKE_SOURCE_DIR}/src/dipiradiohead/framer/aac_adts.c
            ${CMAKE_SOURCE_DIR}/src/dipiradiohead/framer/aac_latm.c
            ${CMAKE_SOURCE_DIR}/src/dipiradiohead/mux/psi.c
            ${CMAKE_SOURCE_DIR}/src/dipiradiohead/mux/pes.c
            ${CMAKE_SOURCE_DIR}/src/dipiradiohead/mux/tspacketizer.c
            ${CMAKE_SOURCE_DIR}/src/dipiradiohead/cas/cas.c
            ${CMAKE_SOURCE_DIR}/src/lib/mux/cadescbuild.c
            ${CMAKE_SOURCE_DIR}/src/lib/cas/cas_args.c
            ${CMAKE_SOURCE_DIR}/src/lib/cas/simulcrypt_msg.c
            ${CMAKE_SOURCE_DIR}/src/lib/cas/ecmg_client/ecmg_client.c
            ${CMAKE_SOURCE_DIR}/src/lib/cas/ecmg_client/protocol.c
            ${CMAKE_SOURCE_DIR}/src/lib/cas/ecmg_client/connect.c
            ${CMAKE_SOURCE_DIR}/src/lib/cas/ecmg_client/run.c
            ${CWENC_SRC}
            ${CMAKE_SOURCE_DIR}/src/lib/helper/secure_zero.c
            ${CMAKE_SOURCE_DIR}/src/lib/cas/emmg_server/emmg_server.c
            ${CMAKE_SOURCE_DIR}/src/lib/cas/emmg_server/protocol.c
            ${CMAKE_SOURCE_DIR}/src/lib/cas/emmg_server/worker.c
            ${CMAKE_SOURCE_DIR}/src/lib/cas/cas_group.c
            ${CMAKE_SOURCE_DIR}/src/lib/cas/cas_scramble_engine.c
            ${CMAKE_SOURCE_DIR}/src/lib/cas/cas_core.c
            ${CMAKE_SOURCE_DIR}/src/lib/mux/rtpheader.c
            ${CMAKE_SOURCE_DIR}/src/lib/mux/psi_build.c
            ${CMAKE_SOURCE_DIR}/src/lib/mux/mpts.c
            ${CMAKE_SOURCE_DIR}/src/lib/mux/tspacket_write.c
            ${CMAKE_SOURCE_DIR}/src/lib/scrambler/scrambler.c
            ${CISSA_SRC}
            ${CSA2_SRC}
            ${BISS_SRC}
            ${BISS_CA_SRC}
            ${CMAKE_SOURCE_DIR}/src/lib/cas/biss/ca_sections.c
            ${CMAKE_SOURCE_DIR}/src/lib/cas/biss/ca_engine.c)
    set(DIPIRADIOHEAD_SRCS ${DIPIRADIOHEAD_SRCS} PARENT_SCOPE)
    set(DIPIRADIOHEAD_HAVE_TLS ${DIPIRADIOHEAD_HAVE_TLS} PARENT_SCOPE)
    set(DIPIRADIOHEAD_HAVE_CISSA ${DIPIRADIOHEAD_HAVE_CISSA} PARENT_SCOPE)
    set(DIPIRADIOHEAD_CSA ${DIPIRADIOHEAD_CSA} PARENT_SCOPE)
    set(DIPIRADIOHEAD_ATOMIC_LIB ${ATOMIC_LIB} PARENT_SCOPE)
endfunction()

function(dipirec_resolve_sources)
    option(DIPIREC_TLS "build dipirec with HTTPS/TLS support (requires OpenSSL)" ON)
    set(DIPIREC_HAVE_TLS FALSE)
    if (DIPIREC_TLS)
        if (DVBIPITOOLS_STATIC)
            set(OPENSSL_USE_STATIC_LIBS TRUE)
            set(ATOMIC_LIB atomic)
        endif ()
        find_package(OpenSSL)
        if (OpenSSL_FOUND)
            set(DIPIREC_HAVE_TLS TRUE)
        else ()
            message(WARNING "dipirec: OpenSSL not found, building without HTTPS support")
        endif ()
    endif ()
    if (DIPIREC_HAVE_TLS)
        set(TLS_SRC ${CMAKE_SOURCE_DIR}/src/lib/net/tls.c)
        set(RTMP_AUTH_SRC ${CMAKE_SOURCE_DIR}/src/lib/net/rtmp/auth.c)
    else ()
        set(TLS_SRC ${CMAKE_SOURCE_DIR}/src/lib/net/tls_stub.c)
        set(RTMP_AUTH_SRC ${CMAKE_SOURCE_DIR}/src/lib/net/rtmp/auth_stub.c)
    endif ()
    if (DVBIPITOOLS_HAVE_RIST)
        set(RIST_SRC ${CMAKE_SOURCE_DIR}/src/lib/net/rist/ristout.c
                ${CMAKE_SOURCE_DIR}/src/lib/net/rist/ristin.c
                ${CMAKE_SOURCE_DIR}/src/lib/net/rist/ristlog.c)
    else ()
        set(RIST_SRC ${CMAKE_SOURCE_DIR}/src/lib/net/rist/ristout_stub.c
                ${CMAKE_SOURCE_DIR}/src/lib/net/rist/ristin_stub.c
                ${CMAKE_SOURCE_DIR}/src/lib/net/rist/ristlog_stub.c)
    endif ()
    if (DVBIPITOOLS_HAVE_SRT)
        set(SRT_SRC ${CMAKE_SOURCE_DIR}/src/lib/net/srt/srtsrc.c
                ${CMAKE_SOURCE_DIR}/src/lib/net/srt/srtin.c
                ${CMAKE_SOURCE_DIR}/src/lib/net/srt/srtsink.c
                ${CMAKE_SOURCE_DIR}/src/lib/net/srt/srtout.c
                ${CMAKE_SOURCE_DIR}/src/lib/net/srt/srtcommon.c)
    else ()
        set(SRT_SRC ${CMAKE_SOURCE_DIR}/src/lib/net/srt/srtsrc_stub.c
                ${CMAKE_SOURCE_DIR}/src/lib/net/srt/srtsink_stub.c)
    endif ()
    set(DIPIREC_SRCS
            ${CMAKE_SOURCE_DIR}/src/dipirec/main.c
            ${CMAKE_SOURCE_DIR}/src/dipirec/args.c
            ${CMAKE_SOURCE_DIR}/src/dipirec/record.c
            ${CMAKE_SOURCE_DIR}/src/dipirec/record/sink.c
            ${CMAKE_SOURCE_DIR}/src/dipirec/record/rtmp_fanout.c
            ${CMAKE_SOURCE_DIR}/src/dipirec/record/stats.c
            ${CMAKE_SOURCE_DIR}/src/dipirec/record/run.c
            ${CMAKE_SOURCE_DIR}/src/lib/fccret/ret_client.c
            ${CMAKE_SOURCE_DIR}/src/lib/helper/log.c
            ${CMAKE_SOURCE_DIR}/src/lib/helper/argutil.c
            ${CMAKE_SOURCE_DIR}/src/lib/helper/uriparse.c
            ${CMAKE_SOURCE_DIR}/src/lib/helper/signal.c
            ${CMAKE_SOURCE_DIR}/src/lib/net/multicast.c
            ${CMAKE_SOURCE_DIR}/src/lib/net/netconnect.c
            ${CMAKE_SOURCE_DIR}/src/lib/net/tssource.c
            ${CMAKE_SOURCE_DIR}/src/lib/net/tssink.c
            ${CMAKE_SOURCE_DIR}/src/lib/metrics/protocol.c
            ${CMAKE_SOURCE_DIR}/src/lib/metrics/export.c
            ${TLS_SRC}
            ${RIST_SRC}
            ${CMAKE_SOURCE_DIR}/src/lib/net/httpclient/httpclient.c
            ${CMAKE_SOURCE_DIR}/src/lib/net/httpclient/url.c
            ${CMAKE_SOURCE_DIR}/src/lib/net/httpclient/read.c
            ${CMAKE_SOURCE_DIR}/src/lib/vendor/picohttpparser/picohttpparser.c
            ${CMAKE_SOURCE_DIR}/src/lib/net/httpclient/async.c
            ${CMAKE_SOURCE_DIR}/src/lib/helper/ioutil.c
            ${CMAKE_SOURCE_DIR}/src/lib/demux/rtp.c
            ${CMAKE_SOURCE_DIR}/src/lib/demux/rtx.c
            ${CMAKE_SOURCE_DIR}/src/lib/demux/crc32.c
            ${CMAKE_SOURCE_DIR}/src/lib/demux/psi/psi.c
            ${CMAKE_SOURCE_DIR}/src/lib/demux/psi/parse.c
            ${CMAKE_SOURCE_DIR}/src/lib/demux/psi/descriptors.c
            ${CMAKE_SOURCE_DIR}/src/lib/demux/psi/section_asm.c
            ${CMAKE_SOURCE_DIR}/src/lib/demux/tspack.c
            ${CMAKE_SOURCE_DIR}/src/lib/demux/pes.c
            ${CMAKE_SOURCE_DIR}/src/lib/demux/mpts_probe.c
            ${CMAKE_SOURCE_DIR}/src/lib/mux/rtcp_build.c
            ${CMAKE_SOURCE_DIR}/src/lib/mux/rtpheader.c
            ${CMAKE_SOURCE_DIR}/src/lib/mux/ebml.c
            ${CMAKE_SOURCE_DIR}/src/lib/mux/mkv/mkv.c
            ${CMAKE_SOURCE_DIR}/src/lib/demux/bitreader.c
            ${CMAKE_SOURCE_DIR}/src/lib/demux/escodec/aubuild.c
            ${CMAKE_SOURCE_DIR}/src/lib/demux/escodec/audio.c
            ${CMAKE_SOURCE_DIR}/src/lib/mux/mkv/video.c
            ${CMAKE_SOURCE_DIR}/src/lib/demux/escodec/video.c
            ${CMAKE_SOURCE_DIR}/src/lib/mux/mkv/write.c
            ${CMAKE_SOURCE_DIR}/src/lib/mux/mkv/feed.c
            ${CMAKE_SOURCE_DIR}/src/lib/mux/teletext.c
            ${CMAKE_SOURCE_DIR}/src/lib/mux/amf.c
            ${CMAKE_SOURCE_DIR}/src/lib/mux/flv/flv.c
            ${CMAKE_SOURCE_DIR}/src/lib/mux/flv/feed.c
            ${CMAKE_SOURCE_DIR}/src/lib/mux/flv/write.c
            ${CMAKE_SOURCE_DIR}/src/lib/net/rtmp/handshake.c
            ${CMAKE_SOURCE_DIR}/src/lib/net/rtmp/chunk.c
            ${CMAKE_SOURCE_DIR}/src/lib/net/rtmp/session.c
            ${CMAKE_SOURCE_DIR}/src/lib/net/rtmp/command.c
            ${CMAKE_SOURCE_DIR}/src/lib/net/rtmp/rtmp.c
            ${CMAKE_SOURCE_DIR}/src/lib/net/rtmp/rtmpout.c
            ${RTMP_AUTH_SRC}
            ${CMAKE_SOURCE_DIR}/src/dipirec/filter/ts.c
            ${CMAKE_SOURCE_DIR}/src/dipirec/filter/pace.c
            ${SRT_SRC})
    set(DIPIREC_SRCS ${DIPIREC_SRCS} PARENT_SCOPE)
    set(DIPIREC_HAVE_TLS ${DIPIREC_HAVE_TLS} PARENT_SCOPE)
    set(DIPIREC_ATOMIC_LIB ${ATOMIC_LIB} PARENT_SCOPE)
endfunction()

function(dipirist_resolve_sources)
    set(DIPIRIST_HAVE_RIST FALSE)
    option(DIPIRIST_ENABLED "build dipirist (requires librist)" ON)
    if (DIPIRIST_ENABLED AND DVBIPITOOLS_HAVE_RIST)
        set(DIPIRIST_HAVE_RIST TRUE)
    endif ()
    set(DIPIRIST_FOUND ${DIPIRIST_HAVE_RIST} PARENT_SCOPE)
    if (NOT DIPIRIST_HAVE_RIST)
        return()
    endif ()

    option(DIPIRIST_TLS "build dipirist with HTTPS/TLS support for -i https:// sources (requires OpenSSL)" ON)
    set(DIPIRIST_HAVE_TLS FALSE)
    if (DIPIRIST_TLS)
        if (DVBIPITOOLS_STATIC)
            set(OPENSSL_USE_STATIC_LIBS TRUE)
            set(ATOMIC_LIB atomic)
        endif ()
        find_package(OpenSSL)
        if (OpenSSL_FOUND)
            set(DIPIRIST_HAVE_TLS TRUE)
        else ()
            message(WARNING "dipirist: OpenSSL not found, building without HTTPS support")
        endif ()
    endif ()
    if (DIPIRIST_HAVE_TLS)
        set(TLS_SRC ${CMAKE_SOURCE_DIR}/src/lib/net/tls.c)
    else ()
        set(TLS_SRC ${CMAKE_SOURCE_DIR}/src/lib/net/tls_stub.c)
    endif ()

    if (DVBIPITOOLS_HAVE_SRT)
        set(SRT_SRC ${CMAKE_SOURCE_DIR}/src/lib/net/srt/srtsrc.c
                ${CMAKE_SOURCE_DIR}/src/lib/net/srt/srtin.c
                ${CMAKE_SOURCE_DIR}/src/lib/net/srt/srtcommon.c)
    else ()
        set(SRT_SRC ${CMAKE_SOURCE_DIR}/src/lib/net/srt/srtsrc_stub.c)
    endif ()

    set(DIPIRIST_SRCS
            ${CMAKE_SOURCE_DIR}/src/dipirist/main.c
            ${CMAKE_SOURCE_DIR}/src/dipirist/args.c
            ${CMAKE_SOURCE_DIR}/src/dipirist/bridge.c
            ${CMAKE_SOURCE_DIR}/src/lib/helper/log.c
            ${CMAKE_SOURCE_DIR}/src/lib/helper/argutil.c
            ${CMAKE_SOURCE_DIR}/src/lib/helper/uriparse.c
            ${CMAKE_SOURCE_DIR}/src/lib/helper/ioutil.c
            ${CMAKE_SOURCE_DIR}/src/lib/helper/signal.c
            ${CMAKE_SOURCE_DIR}/src/lib/net/multicast.c
            ${CMAKE_SOURCE_DIR}/src/lib/net/netconnect.c
            ${CMAKE_SOURCE_DIR}/src/lib/net/tssource.c
            ${CMAKE_SOURCE_DIR}/src/lib/net/tssink.c
            ${CMAKE_SOURCE_DIR}/src/lib/net/rist/ristout.c
            ${CMAKE_SOURCE_DIR}/src/lib/net/rist/ristin.c
            ${CMAKE_SOURCE_DIR}/src/lib/net/rist/ristlog.c
            ${SRT_SRC}
            ${TLS_SRC}
            ${CMAKE_SOURCE_DIR}/src/lib/net/httpclient/httpclient.c
            ${CMAKE_SOURCE_DIR}/src/lib/net/httpclient/url.c
            ${CMAKE_SOURCE_DIR}/src/lib/net/httpclient/read.c
            ${CMAKE_SOURCE_DIR}/src/lib/vendor/picohttpparser/picohttpparser.c
            ${CMAKE_SOURCE_DIR}/src/lib/net/httpclient/async.c
            ${CMAKE_SOURCE_DIR}/src/lib/demux/rtp.c
            ${CMAKE_SOURCE_DIR}/src/lib/mux/rtpheader.c
            ${CMAKE_SOURCE_DIR}/src/lib/metrics/protocol.c
            ${CMAKE_SOURCE_DIR}/src/lib/metrics/export.c)
    set(DIPIRIST_SRCS ${DIPIRIST_SRCS} PARENT_SCOPE)
    set(DIPIRIST_HAVE_TLS ${DIPIRIST_HAVE_TLS} PARENT_SCOPE)
    set(DIPIRIST_ATOMIC_LIB ${ATOMIC_LIB} PARENT_SCOPE)
    set(DIPIRIST_LIBRIST_IPV6_WARN ${DVBIPITOOLS_RIST_IPV6_WARN} PARENT_SCOPE)

    include(CheckCSourceCompiles)
    set(CMAKE_REQUIRED_INCLUDES ${RIST_INCLUDE_DIRS})
    check_c_source_compiles("
#include <librist/librist.h>
int main(void) {
    struct rist_stats_receiver_flow f;
    return (int) f.avg_buffer_time;
}
" DIPIRIST_HAVE_AVG_BUFFER_TIME)
    unset(CMAKE_REQUIRED_INCLUDES)
    set(DIPIRIST_HAVE_AVG_BUFFER_TIME ${DIPIRIST_HAVE_AVG_BUFFER_TIME} PARENT_SCOPE)
endfunction()

function(dipisrt_resolve_sources)
    set(DIPISRT_HAVE_SRT FALSE)
    option(DIPISRT_ENABLED "build dipisrt (requires libsrt)" ON)
    if (DIPISRT_ENABLED AND DVBIPITOOLS_HAVE_SRT)
        set(DIPISRT_HAVE_SRT TRUE)
    endif ()
    set(DIPISRT_FOUND ${DIPISRT_HAVE_SRT} PARENT_SCOPE)
    set(DIPISRT_HAVE_BONDING ${DVBIPITOOLS_SRT_HAVE_BONDING} PARENT_SCOPE)
    if (NOT DIPISRT_HAVE_SRT)
        return()
    endif ()

    option(DIPISRT_TLS "build dipisrt with HTTPS/TLS support for -i https:// sources (requires OpenSSL)" ON)
    set(DIPISRT_HAVE_TLS FALSE)
    if (DIPISRT_TLS)
        if (DVBIPITOOLS_STATIC)
            set(OPENSSL_USE_STATIC_LIBS TRUE)
        endif ()
        find_package(OpenSSL)
        if (OpenSSL_FOUND)
            set(DIPISRT_HAVE_TLS TRUE)
        else ()
            message(WARNING "dipisrt: OpenSSL not found, building without HTTPS support")
        endif ()
    endif ()
    if (DIPISRT_HAVE_TLS)
        set(TLS_SRC ${CMAKE_SOURCE_DIR}/src/lib/net/tls.c)
    else ()
        set(TLS_SRC ${CMAKE_SOURCE_DIR}/src/lib/net/tls_stub.c)
    endif ()

    if (DVBIPITOOLS_HAVE_RIST)
        set(RIST_SRC ${CMAKE_SOURCE_DIR}/src/lib/net/rist/ristin.c
                ${CMAKE_SOURCE_DIR}/src/lib/net/rist/ristlog.c)
    else ()
        set(RIST_SRC ${CMAKE_SOURCE_DIR}/src/lib/net/rist/ristin_stub.c
                ${CMAKE_SOURCE_DIR}/src/lib/net/rist/ristlog_stub.c)
    endif ()

    set(DIPISRT_SRCS
            ${CMAKE_SOURCE_DIR}/src/dipisrt/main.c
            ${CMAKE_SOURCE_DIR}/src/dipisrt/args.c
            ${CMAKE_SOURCE_DIR}/src/dipisrt/bridge.c
            ${CMAKE_SOURCE_DIR}/src/lib/helper/log.c
            ${CMAKE_SOURCE_DIR}/src/lib/helper/argutil.c
            ${CMAKE_SOURCE_DIR}/src/lib/helper/uriparse.c
            ${CMAKE_SOURCE_DIR}/src/lib/helper/ioutil.c
            ${CMAKE_SOURCE_DIR}/src/lib/helper/signal.c
            ${CMAKE_SOURCE_DIR}/src/lib/net/multicast.c
            ${CMAKE_SOURCE_DIR}/src/lib/net/netconnect.c
            ${CMAKE_SOURCE_DIR}/src/lib/net/tssource.c
            ${CMAKE_SOURCE_DIR}/src/lib/net/tssink.c
            ${RIST_SRC}
            ${CMAKE_SOURCE_DIR}/src/lib/net/srt/srtsrc.c
            ${CMAKE_SOURCE_DIR}/src/lib/net/srt/srtcommon.c
            ${CMAKE_SOURCE_DIR}/src/lib/net/srt/srtout.c
            ${CMAKE_SOURCE_DIR}/src/lib/net/srt/srtin.c
            ${TLS_SRC}
            ${CMAKE_SOURCE_DIR}/src/lib/net/httpclient/httpclient.c
            ${CMAKE_SOURCE_DIR}/src/lib/net/httpclient/url.c
            ${CMAKE_SOURCE_DIR}/src/lib/net/httpclient/read.c
            ${CMAKE_SOURCE_DIR}/src/lib/vendor/picohttpparser/picohttpparser.c
            ${CMAKE_SOURCE_DIR}/src/lib/net/httpclient/async.c
            ${CMAKE_SOURCE_DIR}/src/lib/demux/rtp.c
            ${CMAKE_SOURCE_DIR}/src/lib/mux/rtpheader.c
            ${CMAKE_SOURCE_DIR}/src/lib/metrics/protocol.c
            ${CMAKE_SOURCE_DIR}/src/lib/metrics/export.c)
    set(DIPISRT_SRCS ${DIPISRT_SRCS} PARENT_SCOPE)
    set(DIPISRT_HAVE_TLS ${DIPISRT_HAVE_TLS} PARENT_SCOPE)
endfunction()

function(dipiscan_resolve_sources)
    set(DIPISCAN_SRCS
            ${CMAKE_SOURCE_DIR}/src/dipiscan/main.c
            ${CMAKE_SOURCE_DIR}/src/dipiscan/args.c
            ${CMAKE_SOURCE_DIR}/src/dipiscan/format.c
            ${CMAKE_SOURCE_DIR}/src/dipiscan/scan.c
            ${CMAKE_SOURCE_DIR}/src/lib/helper/playlist_out.c
            ${CMAKE_SOURCE_DIR}/src/lib/helper/log.c
            ${CMAKE_SOURCE_DIR}/src/lib/helper/argutil.c
            ${CMAKE_SOURCE_DIR}/src/lib/helper/signal.c
            ${CMAKE_SOURCE_DIR}/src/lib/helper/sds_xml.c
            ${CMAKE_SOURCE_DIR}/src/lib/helper/xml_util.c
            ${CMAKE_SOURCE_DIR}/src/lib/helper/ioutil.c
            ${CMAKE_SOURCE_DIR}/src/lib/net/multicast.c
            ${CMAKE_SOURCE_DIR}/src/lib/net/netconnect.c
            ${CMAKE_SOURCE_DIR}/src/lib/net/httpclient/httpclient.c
            ${CMAKE_SOURCE_DIR}/src/lib/net/httpclient/url.c
            ${CMAKE_SOURCE_DIR}/src/lib/net/httpclient/read.c
            ${CMAKE_SOURCE_DIR}/src/lib/vendor/picohttpparser/picohttpparser.c
            ${CMAKE_SOURCE_DIR}/src/lib/net/httpclient/async.c
            ${CMAKE_SOURCE_DIR}/src/lib/net/tls_stub.c
            ${CMAKE_SOURCE_DIR}/src/lib/demux/rtp.c
            ${CMAKE_SOURCE_DIR}/src/lib/demux/crc32.c
            ${CMAKE_SOURCE_DIR}/src/lib/demux/psi/psi.c
            ${CMAKE_SOURCE_DIR}/src/lib/demux/psi/parse.c
            ${CMAKE_SOURCE_DIR}/src/lib/demux/psi/descriptors.c
            ${CMAKE_SOURCE_DIR}/src/lib/demux/psi/section_asm.c
            ${CMAKE_SOURCE_DIR}/src/lib/demux/tspack.c)
    set(DIPISCAN_SRCS ${DIPISCAN_SRCS} PARENT_SCOPE)
endfunction()

function(dipixy_resolve_sources)
    option(DIPIXY_TLS "build dipixy with HTTPS/TLS support (requires OpenSSL)" ON)
    set(DIPIXY_HAVE_TLS FALSE)
    if (DIPIXY_TLS)
        if (DVBIPITOOLS_STATIC)
            set(OPENSSL_USE_STATIC_LIBS TRUE)
            set(ATOMIC_LIB atomic)
        endif ()
        find_package(OpenSSL)
        if (OpenSSL_FOUND)
            set(DIPIXY_HAVE_TLS TRUE)
        else ()
            message(WARNING "dipixy: OpenSSL not found, building without HTTPS support")
        endif ()
    endif ()
    if (DIPIXY_HAVE_TLS)
        set(REACTOR_TLS_SRC
                ${CMAKE_SOURCE_DIR}/src/dipixy/reactor/reactor_tls.c
                ${CMAKE_SOURCE_DIR}/src/dipixy/reactor/reactor_tls_conn.c
                ${CMAKE_SOURCE_DIR}/src/dipixy/reactor/reactor_tls_io.c
                ${CMAKE_SOURCE_DIR}/src/dipixy/reactor/reactor_tls_cert.c)
        set(DIPIXY_CLIENT_TLS_SRC ${CMAKE_SOURCE_DIR}/src/lib/net/tls.c)
    else ()
        set(REACTOR_TLS_SRC ${CMAKE_SOURCE_DIR}/src/dipixy/reactor/reactor_tls_stub.c)
        set(DIPIXY_CLIENT_TLS_SRC ${CMAKE_SOURCE_DIR}/src/lib/net/tls_stub.c)
    endif ()

    if (DVBIPITOOLS_HAVE_RIST)
        set(DIPIXY_RIST_SRC ${CMAKE_SOURCE_DIR}/src/lib/net/rist/ristin.c
                ${CMAKE_SOURCE_DIR}/src/lib/net/rist/ristlog.c)
    else ()
        set(DIPIXY_RIST_SRC ${CMAKE_SOURCE_DIR}/src/lib/net/rist/ristin_stub.c
                ${CMAKE_SOURCE_DIR}/src/lib/net/rist/ristlog_stub.c)
    endif ()

    if (DVBIPITOOLS_HAVE_SRT)
        set(DIPIXY_SRT_SRC ${CMAKE_SOURCE_DIR}/src/lib/net/srt/srtsrc.c
                ${CMAKE_SOURCE_DIR}/src/lib/net/srt/srtin.c
                ${CMAKE_SOURCE_DIR}/src/lib/net/srt/srtsink.c
                ${CMAKE_SOURCE_DIR}/src/lib/net/srt/srtout.c
                ${CMAKE_SOURCE_DIR}/src/lib/net/srt/srtcommon.c)
    else ()
        set(DIPIXY_SRT_SRC ${CMAKE_SOURCE_DIR}/src/lib/net/srt/srtsrc_stub.c
                ${CMAKE_SOURCE_DIR}/src/lib/net/srt/srtsink_stub.c)
    endif ()

    option(DIPIXY_HTTP2 "build dipixy with HTTP/2 support (requires libnghttp2)" ON)
    set(DIPIXY_HAVE_HTTP2 FALSE)
    if (DIPIXY_HTTP2)
        find_package(PkgConfig)
        if (PkgConfig_FOUND)
            pkg_check_modules(NGHTTP2 IMPORTED_TARGET libnghttp2)
            if (NGHTTP2_FOUND)
                set(DIPIXY_HAVE_HTTP2 TRUE)
                if (DVBIPITOOLS_STATIC)
                    include(CheckCSourceCompiles)
                    set(CMAKE_REQUIRED_LIBRARIES ${NGHTTP2_STATIC_LDFLAGS})
                    set(CMAKE_REQUIRED_LINK_OPTIONS -static ${NGHTTP2_STATIC_LDFLAGS_OTHER})
                    check_c_source_compiles("int main(void) { return 0; }" DIPIXY_NGHTTP2_STATIC_LINKS)
                    unset(CMAKE_REQUIRED_LIBRARIES)
                    unset(CMAKE_REQUIRED_LINK_OPTIONS)
                    if (DIPIXY_NGHTTP2_STATIC_LINKS)
                        set_target_properties(PkgConfig::NGHTTP2 PROPERTIES
                                INTERFACE_LINK_LIBRARIES "${NGHTTP2_STATIC_LDFLAGS}"
                                INTERFACE_LINK_OPTIONS "${NGHTTP2_STATIC_LDFLAGS_OTHER}")
                    else ()
                        set(DIPIXY_HAVE_HTTP2 FALSE)
                    endif ()
                endif ()
            endif ()
        endif ()
        if (NOT DIPIXY_HAVE_HTTP2)
            message(WARNING "dipixy: libnghttp2 not found (or no static libraries available), building without HTTP/2 support")
        endif ()
    endif ()
    if (DIPIXY_HAVE_HTTP2)
        set(DIPIXY_HTTP2_SRCS
                ${CMAKE_SOURCE_DIR}/src/dipixy/http2/http2.c
                ${CMAKE_SOURCE_DIR}/src/dipixy/http2/http2_tspush.c
                ${CMAKE_SOURCE_DIR}/src/dipixy/http2/http2_dashchunk.c
                ${CMAKE_SOURCE_DIR}/src/dipixy/http2/http2_mp4push.c
                ${CMAKE_SOURCE_DIR}/src/dipixy/http2/http2_hls.c
                ${CMAKE_SOURCE_DIR}/src/dipixy/http2/http2_ws.c)
    else ()
        set(DIPIXY_HTTP2_SRCS)
    endif ()

    option(DIPIXY_HTTP3 "build dipixy with HTTP/3 support (requires libngtcp2 + libngtcp2_crypto_ossl + libnghttp3 + OpenSSL >= 3.5)" ON)
    set(DIPIXY_HAVE_HTTP3 FALSE)
    if (DIPIXY_HTTP3)
        find_package(PkgConfig)
        if (PkgConfig_FOUND)
            pkg_check_modules(NGTCP2 IMPORTED_TARGET libngtcp2)
            pkg_check_modules(NGTCP2_CRYPTO_OSSL IMPORTED_TARGET libngtcp2_crypto_ossl)
            pkg_check_modules(NGHTTP3 IMPORTED_TARGET libnghttp3)
            if (NGTCP2_FOUND AND NGTCP2_CRYPTO_OSSL_FOUND AND NGHTTP3_FOUND)
                include(CheckCSourceCompiles)
                check_c_source_compiles("
                    #include <openssl/opensslv.h>
                    #if OPENSSL_VERSION_NUMBER < 0x30500000L
                    #error no quic
                    #endif
                    int main(void) { return 0; }" DIPIXY_HAVE_OPENSSL_QUIC)
                if (DIPIXY_HAVE_OPENSSL_QUIC)
                    set(DIPIXY_HAVE_HTTP3 TRUE)
                    if (DVBIPITOOLS_STATIC)
                        set(CMAKE_REQUIRED_LIBRARIES ${NGTCP2_STATIC_LDFLAGS} ${NGTCP2_CRYPTO_OSSL_STATIC_LDFLAGS} ${NGHTTP3_STATIC_LDFLAGS})
                        set(CMAKE_REQUIRED_LINK_OPTIONS -static ${NGTCP2_STATIC_LDFLAGS_OTHER} ${NGTCP2_CRYPTO_OSSL_STATIC_LDFLAGS_OTHER}
                                ${NGHTTP3_STATIC_LDFLAGS_OTHER})
                        check_c_source_compiles("int main(void) { return 0; }" DIPIXY_HTTP3_STATIC_LINKS)
                        unset(CMAKE_REQUIRED_LIBRARIES)
                        unset(CMAKE_REQUIRED_LINK_OPTIONS)
                        if (DIPIXY_HTTP3_STATIC_LINKS)
                            set_target_properties(PkgConfig::NGTCP2 PROPERTIES
                                    INTERFACE_LINK_LIBRARIES "${NGTCP2_STATIC_LDFLAGS}"
                                    INTERFACE_LINK_OPTIONS "${NGTCP2_STATIC_LDFLAGS_OTHER}")
                            set_target_properties(PkgConfig::NGTCP2_CRYPTO_OSSL PROPERTIES
                                    INTERFACE_LINK_LIBRARIES "${NGTCP2_CRYPTO_OSSL_STATIC_LDFLAGS}"
                                    INTERFACE_LINK_OPTIONS "${NGTCP2_CRYPTO_OSSL_STATIC_LDFLAGS_OTHER}")
                            set_target_properties(PkgConfig::NGHTTP3 PROPERTIES
                                    INTERFACE_LINK_LIBRARIES "${NGHTTP3_STATIC_LDFLAGS}"
                                    INTERFACE_LINK_OPTIONS "${NGHTTP3_STATIC_LDFLAGS_OTHER}")
                        else ()
                            set(DIPIXY_HAVE_HTTP3 FALSE)
                        endif ()
                    endif ()
                endif ()
            endif ()
        endif ()
        if (NOT DIPIXY_HAVE_HTTP3)
            message(WARNING "dipixy: libngtcp2/libngtcp2_crypto_ossl/libnghttp3 (+ OpenSSL >= 3.5, static libraries if DVBIPITOOLS_STATIC) not satisfied, building without HTTP/3 support")
        endif ()
    endif ()
    if (DIPIXY_HAVE_HTTP3)
        set(DIPIXY_HTTP3_SRCS
                ${CMAKE_SOURCE_DIR}/src/dipixy/http3/http3.c
                ${CMAKE_SOURCE_DIR}/src/dipixy/http3/http3_quic.c
                ${CMAKE_SOURCE_DIR}/src/dipixy/http3/http3_req.c
                ${CMAKE_SOURCE_DIR}/src/dipixy/http3/http3_resp.c
                ${CMAKE_SOURCE_DIR}/src/dipixy/http3/http3_tspush.c
                ${CMAKE_SOURCE_DIR}/src/dipixy/http3/http3_dashchunk.c
                ${CMAKE_SOURCE_DIR}/src/dipixy/http3/http3_mp4push.c
                ${CMAKE_SOURCE_DIR}/src/dipixy/http3/http3_llhls.c
                ${CMAKE_SOURCE_DIR}/src/dipixy/http3/http3_hls_cold.c
                ${CMAKE_SOURCE_DIR}/src/dipixy/http3/http3_ws.c)
    else ()
        set(DIPIXY_HTTP3_SRCS)
    endif ()

    set(DIPIXY_SRCS
            ${CMAKE_SOURCE_DIR}/src/dipixy/main.c
            ${CMAKE_SOURCE_DIR}/src/dipixy/args.c
            ${CMAKE_SOURCE_DIR}/src/dipixy/core/route.c
            ${CMAKE_SOURCE_DIR}/src/dipixy/core/playlist.c
            ${CMAKE_SOURCE_DIR}/src/dipixy/core/htdocs.c
            ${CMAKE_SOURCE_DIR}/src/dipixy/reactor/reactor.c
            ${CMAKE_SOURCE_DIR}/src/dipixy/reactor/reactor_listen.c
            ${CMAKE_SOURCE_DIR}/src/dipixy/reactor/reactor_loop.c
            ${CMAKE_SOURCE_DIR}/src/dipixy/ts/channels/channels.c
            ${CMAKE_SOURCE_DIR}/src/dipixy/ts/channels/build.c
            ${CMAKE_SOURCE_DIR}/src/dipixy/ts/channels/reload.c
            ${CMAKE_SOURCE_DIR}/src/dipixy/ts/capture/capture.c
            ${CMAKE_SOURCE_DIR}/src/dipixy/ts/capture/pump.c
            ${CMAKE_SOURCE_DIR}/src/dipixy/ts/capture/service.c
            ${CMAKE_SOURCE_DIR}/src/dipixy/ts/capture/source.c
            ${CMAKE_SOURCE_DIR}/src/lib/net/tssource.c
            ${CMAKE_SOURCE_DIR}/src/lib/net/httpclient/httpclient.c
            ${CMAKE_SOURCE_DIR}/src/lib/net/httpclient/url.c
            ${CMAKE_SOURCE_DIR}/src/lib/net/httpclient/read.c
            ${CMAKE_SOURCE_DIR}/src/lib/net/httpclient/async.c
            ${CMAKE_SOURCE_DIR}/src/lib/vendor/picohttpparser/picohttpparser.c
            ${CMAKE_SOURCE_DIR}/src/dipixy/reactor/conn.c
            ${CMAKE_SOURCE_DIR}/src/dipixy/reactor/handshake.c
            ${CMAKE_SOURCE_DIR}/src/dipixy/reactor/dispatch/dispatch.c
            ${CMAKE_SOURCE_DIR}/src/dipixy/reactor/dispatch/resp.c
            ${CMAKE_SOURCE_DIR}/src/dipixy/reactor/dispatch/content.c
            ${CMAKE_SOURCE_DIR}/src/dipixy/reactor/dispatch/route.c
            ${CMAKE_SOURCE_DIR}/src/dipixy/reactor/dispatch/waiters.c
            ${CMAKE_SOURCE_DIR}/src/dipixy/reactor/reactor_tspush.c
            ${CMAKE_SOURCE_DIR}/src/dipixy/reactor/reactor_dashchunk.c
            ${CMAKE_SOURCE_DIR}/src/dipixy/reactor/reactor_mp4push.c
            ${CMAKE_SOURCE_DIR}/src/dipixy/reactor/reactor_ws.c
            ${CMAKE_SOURCE_DIR}/src/dipixy/ts/ts_push.c
            ${CMAKE_SOURCE_DIR}/src/dipixy/ts/ts_push_feed.c
            ${CMAKE_SOURCE_DIR}/src/dipixy/ts/ts_push_flush.c
            ${CMAKE_SOURCE_DIR}/src/dipixy/ts/rawaudio.c
            ${CMAKE_SOURCE_DIR}/src/dipixy/ts/pidfilter.c
            ${CMAKE_SOURCE_DIR}/src/dipixy/ts/pmtselect.c
            ${CMAKE_SOURCE_DIR}/src/lib/mux/psi_build.c
            ${CMAKE_SOURCE_DIR}/src/lib/mux/pmt_filter.c
            ${CMAKE_SOURCE_DIR}/src/dipixy/core/status.c
            ${CMAKE_SOURCE_DIR}/src/dipixy/core/tlscert.c
            ${CMAKE_SOURCE_DIR}/src/lib/helper/toolmain.c
            ${CMAKE_SOURCE_DIR}/src/dipixy/ws/ws_frame.c
            ${CMAKE_SOURCE_DIR}/src/dipixy/ws/ws_broadcast.c
            ${CMAKE_SOURCE_DIR}/src/dipixy/ws/ws_sources.c
            ${CMAKE_SOURCE_DIR}/src/dipixy/ws/ws_clients.c
            ${CMAKE_SOURCE_DIR}/src/dipixy/ws/ws_clients_json.c
            ${CMAKE_SOURCE_DIR}/src/dipixy/ws/ws_clients_tick.c
            ${CMAKE_SOURCE_DIR}/src/dipixy/segstore.c
            ${CMAKE_SOURCE_DIR}/src/dipixy/respfmt.c
            ${CMAKE_SOURCE_DIR}/src/dipixy/hls/hls_serve.c
            ${CMAKE_SOURCE_DIR}/src/dipixy/hls/hls_llhls.c
            ${CMAKE_SOURCE_DIR}/src/dipixy/dash/dash.c
            ${CMAKE_SOURCE_DIR}/src/dipixy/dash/lldash.c
            ${CMAKE_SOURCE_DIR}/src/dipixy/hls/hls_render.c
            ${CMAKE_SOURCE_DIR}/src/dipixy/segment/segment.c
            ${CMAKE_SOURCE_DIR}/src/dipixy/segment/demux.c
            ${CMAKE_SOURCE_DIR}/src/dipixy/segment/video.c
            ${CMAKE_SOURCE_DIR}/src/dipixy/segment/audio.c
            ${CMAKE_SOURCE_DIR}/src/dipixy/segment/mux.c
            ${CMAKE_SOURCE_DIR}/src/dipixy/segment/mp4push.c
            ${CMAKE_SOURCE_DIR}/src/dipixy/dlna/ssdp.c
            ${CMAKE_SOURCE_DIR}/src/dipixy/dlna/dlna.c
            ${CMAKE_SOURCE_DIR}/src/dipixy/dlna/dlna_soap.c
            ${CMAKE_SOURCE_DIR}/src/dipixy/dlna/dlna_oid.c
            ${CMAKE_SOURCE_DIR}/src/dipixy/dlna/dlna_didl.c
            ${CMAKE_SOURCE_DIR}/src/dipixy/dlna/dlna_control.c
            ${CMAKE_SOURCE_DIR}/src/dipixy/dlna/gena.c
            ${CMAKE_SOURCE_DIR}/src/dipixy/core/metrics.c
            ${CMAKE_SOURCE_DIR}/src/lib/mux/fmp4/box.c
            ${CMAKE_SOURCE_DIR}/src/lib/mux/fmp4/fmp4.c
            ${CMAKE_SOURCE_DIR}/src/lib/mux/fmp4/fmp4_moov.c
            ${CMAKE_SOURCE_DIR}/src/lib/mux/fmp4/fmp4_frag.c
            ${CMAKE_SOURCE_DIR}/src/lib/metrics/protocol.c
            ${CMAKE_SOURCE_DIR}/src/lib/metrics/export.c
            ${CMAKE_SOURCE_DIR}/src/lib/helper/log.c
            ${CMAKE_SOURCE_DIR}/src/lib/helper/argutil.c
            ${CMAKE_SOURCE_DIR}/src/lib/helper/signal.c
            ${CMAKE_SOURCE_DIR}/src/lib/helper/ioutil.c
            ${CMAKE_SOURCE_DIR}/src/lib/helper/uriparse.c
            ${CMAKE_SOURCE_DIR}/src/lib/helper/jsonbuf.c
            ${CMAKE_SOURCE_DIR}/src/lib/helper/sha1.c
            ${CMAKE_SOURCE_DIR}/src/lib/helper/base64.c
            ${CMAKE_SOURCE_DIR}/src/lib/helper/playlist_in.c
            ${CMAKE_SOURCE_DIR}/src/lib/helper/playlist_out.c
            ${CMAKE_SOURCE_DIR}/src/lib/helper/sds_xml.c
            ${CMAKE_SOURCE_DIR}/src/lib/helper/xml_util.c
            ${CMAKE_SOURCE_DIR}/src/lib/net/multicast.c
            ${CMAKE_SOURCE_DIR}/src/lib/net/netconnect.c
            ${CMAKE_SOURCE_DIR}/src/lib/net/dvbstp.c
            ${CMAKE_SOURCE_DIR}/src/lib/demux/crc32.c
            ${CMAKE_SOURCE_DIR}/src/lib/demux/rtp.c
            ${CMAKE_SOURCE_DIR}/src/lib/demux/tspack.c
            ${CMAKE_SOURCE_DIR}/src/lib/demux/pes.c
            ${CMAKE_SOURCE_DIR}/src/lib/demux/bitreader.c
            ${CMAKE_SOURCE_DIR}/src/lib/demux/psi/psi.c
            ${CMAKE_SOURCE_DIR}/src/lib/demux/psi/parse.c
            ${CMAKE_SOURCE_DIR}/src/lib/demux/psi/descriptors.c
            ${CMAKE_SOURCE_DIR}/src/lib/demux/psi/section_asm.c
            ${CMAKE_SOURCE_DIR}/src/lib/demux/escodec/aubuild.c
            ${CMAKE_SOURCE_DIR}/src/lib/demux/escodec/audio.c
            ${CMAKE_SOURCE_DIR}/src/lib/demux/escodec/video.c
            ${CMAKE_SOURCE_DIR}/src/lib/demux/rtx.c
            ${CMAKE_SOURCE_DIR}/src/lib/demux/rtcp.c
            ${CMAKE_SOURCE_DIR}/src/lib/mux/rtx.c
            ${CMAKE_SOURCE_DIR}/src/lib/mux/rtcp_build.c
            ${CMAKE_SOURCE_DIR}/src/lib/fccret/ret_client.c
            ${CMAKE_SOURCE_DIR}/src/lib/fccret/fcc_client.c
            ${REACTOR_TLS_SRC}
            ${DIPIXY_CLIENT_TLS_SRC}
            ${DIPIXY_RIST_SRC}
            ${DIPIXY_SRT_SRC}
            ${DIPIXY_HTTP2_SRCS}
            ${DIPIXY_HTTP3_SRCS})
    set(DIPIXY_SRCS ${DIPIXY_SRCS} PARENT_SCOPE)
    set(DIPIXY_HAVE_TLS ${DIPIXY_HAVE_TLS} PARENT_SCOPE)
    set(DIPIXY_HAVE_HTTP2 ${DIPIXY_HAVE_HTTP2} PARENT_SCOPE)
    set(DIPIXY_HAVE_HTTP3 ${DIPIXY_HAVE_HTTP3} PARENT_SCOPE)
    set(DIPIXY_ATOMIC_LIB ${ATOMIC_LIB} PARENT_SCOPE)
endfunction()

function(dipisds_resolve_sources)
    set(DIPISDS_SRCS
            ${CMAKE_SOURCE_DIR}/src/dipisds/main.c
            ${CMAKE_SOURCE_DIR}/src/dipisds/args.c
            ${CMAKE_SOURCE_DIR}/src/dipisds/input.c
            ${CMAKE_SOURCE_DIR}/src/dipisds/format_out.c
            ${CMAKE_SOURCE_DIR}/src/dipisds/announce.c
            ${CMAKE_SOURCE_DIR}/src/dipisds/listen.c
            ${CMAKE_SOURCE_DIR}/src/lib/helper/playlist_out.c
            ${CMAKE_SOURCE_DIR}/src/lib/helper/log.c
            ${CMAKE_SOURCE_DIR}/src/lib/helper/argutil.c
            ${CMAKE_SOURCE_DIR}/src/lib/helper/uriparse.c
            ${CMAKE_SOURCE_DIR}/src/lib/helper/signal.c
            ${CMAKE_SOURCE_DIR}/src/lib/metrics/protocol.c
            ${CMAKE_SOURCE_DIR}/src/lib/metrics/export.c
            ${CMAKE_SOURCE_DIR}/src/lib/helper/sds_xml.c
            ${CMAKE_SOURCE_DIR}/src/lib/helper/xml_util.c
            ${CMAKE_SOURCE_DIR}/src/lib/helper/ioutil.c
            ${CMAKE_SOURCE_DIR}/src/lib/net/multicast.c
            ${CMAKE_SOURCE_DIR}/src/lib/net/netconnect.c
            ${CMAKE_SOURCE_DIR}/src/lib/net/dvbstp.c
            ${CMAKE_SOURCE_DIR}/src/lib/demux/crc32.c)
    set(DIPISDS_SRCS ${DIPISDS_SRCS} PARENT_SCOPE)
endfunction()

function(dipitvhead_resolve_sources)
    option(DIPITVHEAD_TLS "build dipitvhead with HTTPS/TLS support (requires OpenSSL)" ON)
    set(DIPITVHEAD_HAVE_TLS FALSE)
    if (DIPITVHEAD_TLS)
        if (DVBIPITOOLS_STATIC)
            set(OPENSSL_USE_STATIC_LIBS TRUE)
            set(ATOMIC_LIB atomic)
        endif ()
        find_package(OpenSSL)
        if (OpenSSL_FOUND)
            set(DIPITVHEAD_HAVE_TLS TRUE)
        else ()
            message(WARNING "dipitvhead: OpenSSL not found, building without HTTPS support")
        endif ()
    endif ()
    if (DIPITVHEAD_HAVE_TLS)
        set(TLS_SRC ${CMAKE_SOURCE_DIR}/src/lib/net/tls.c)
    else ()
        set(TLS_SRC ${CMAKE_SOURCE_DIR}/src/lib/net/tls_stub.c)
    endif ()

    option(DIPITVHEAD_CAS "build dipitvhead with CAS/scrambler support (CISSA requires OpenSSL)" ON)
    set(DIPITVHEAD_HAVE_CISSA FALSE)
    if (DIPITVHEAD_CAS)
        if (DVBIPITOOLS_STATIC)
            set(OPENSSL_USE_STATIC_LIBS TRUE)
            set(ATOMIC_LIB atomic)
        endif ()
        find_package(OpenSSL)
        if (OpenSSL_FOUND)
            set(DIPITVHEAD_HAVE_CISSA TRUE)
        else ()
            message(WARNING "dipitvhead: OpenSSL not found, building without CISSA support")
        endif ()
    endif ()
    if (DIPITVHEAD_HAVE_CISSA)
        set(CISSA_SRC ${CMAKE_SOURCE_DIR}/src/lib/scrambler/cissa.c)
        set(BISS_SRC ${CMAKE_SOURCE_DIR}/src/lib/cas/biss/biss.c ${CMAKE_SOURCE_DIR}/src/lib/cas/biss/hex.c)
        set(BISS_CA_SRC ${CMAKE_SOURCE_DIR}/src/lib/cas/biss/ca.c)
        set(CWENC_SRC ${CMAKE_SOURCE_DIR}/src/lib/cas/ecmg_client/cw_encryption.c)
    else ()
        set(CISSA_SRC ${CMAKE_SOURCE_DIR}/src/lib/scrambler/cissa_stub.c)
        set(BISS_SRC ${CMAKE_SOURCE_DIR}/src/lib/cas/biss/stub.c ${CMAKE_SOURCE_DIR}/src/lib/cas/biss/hex.c)
        set(BISS_CA_SRC ${CMAKE_SOURCE_DIR}/src/lib/cas/biss/ca_stub.c)
        set(CWENC_SRC ${CMAKE_SOURCE_DIR}/src/lib/cas/ecmg_client/cw_encryption_stub.c)
    endif ()

    option(DIPITVHEAD_CSA "build dipitvhead with DVB-CSA (CSA1/CSA2/BISS1) support" ON)
    if (DIPITVHEAD_CSA)
        set(CSA2_SRC ${CMAKE_SOURCE_DIR}/src/lib/scrambler/csa2.c)
    else ()
        set(CSA2_SRC ${CMAKE_SOURCE_DIR}/src/lib/scrambler/csa2_stub.c)
    endif ()

    if (DVBIPITOOLS_HAVE_RIST)
        set(RIST_SRC ${CMAKE_SOURCE_DIR}/src/lib/net/rist/ristout.c
                ${CMAKE_SOURCE_DIR}/src/lib/net/rist/ristin.c
                ${CMAKE_SOURCE_DIR}/src/lib/net/rist/ristlog.c)
    else ()
        set(RIST_SRC ${CMAKE_SOURCE_DIR}/src/lib/net/rist/ristout_stub.c
                ${CMAKE_SOURCE_DIR}/src/lib/net/rist/ristin_stub.c
                ${CMAKE_SOURCE_DIR}/src/lib/net/rist/ristlog_stub.c)
    endif ()

    if (DVBIPITOOLS_HAVE_SRT)
        set(SRT_SRC ${CMAKE_SOURCE_DIR}/src/lib/net/srt/srtsrc.c
                ${CMAKE_SOURCE_DIR}/src/lib/net/srt/srtin.c
                ${CMAKE_SOURCE_DIR}/src/lib/net/srt/srtsink.c
                ${CMAKE_SOURCE_DIR}/src/lib/net/srt/srtout.c
                ${CMAKE_SOURCE_DIR}/src/lib/net/srt/srtcommon.c)
    else ()
        set(SRT_SRC ${CMAKE_SOURCE_DIR}/src/lib/net/srt/srtsrc_stub.c
                ${CMAKE_SOURCE_DIR}/src/lib/net/srt/srtsink_stub.c)
    endif ()

    set(DIPITVHEAD_SRCS
            ${CMAKE_SOURCE_DIR}/src/dipitvhead/main.c
            ${CMAKE_SOURCE_DIR}/src/dipitvhead/args.c
            ${CMAKE_SOURCE_DIR}/src/dipitvhead/tvhead/tvhead.c
            ${CMAKE_SOURCE_DIR}/src/dipitvhead/tvhead/discover.c
            ${CMAKE_SOURCE_DIR}/src/dipitvhead/tvhead/output.c
            ${CMAKE_SOURCE_DIR}/src/dipitvhead/tvhead/single.c
            ${CMAKE_SOURCE_DIR}/src/dipitvhead/tvhead/mpts.c
            ${CMAKE_SOURCE_DIR}/src/dipitvhead/tvhead/mpts/retryset_adapter.c
            ${CMAKE_SOURCE_DIR}/src/dipitvhead/tvhead/mpts/discover_feed.c
            ${CMAKE_SOURCE_DIR}/src/dipitvhead/tvhead/mpts/cas_adapter.c
            ${CMAKE_SOURCE_DIR}/src/dipitvhead/input/source.c
            ${CMAKE_SOURCE_DIR}/src/dipitvhead/mux/pmtbuild.c
            ${CMAKE_SOURCE_DIR}/src/dipitvhead/mux/aitbuild.c
            ${CMAKE_SOURCE_DIR}/src/dipitvhead/mux/remux/lifecycle.c
            ${CMAKE_SOURCE_DIR}/src/dipitvhead/mux/remux/psi.c
            ${CMAKE_SOURCE_DIR}/src/dipitvhead/mux/remux/eit.c
            ${CMAKE_SOURCE_DIR}/src/dipitvhead/mux/remux/feed.c
            ${CMAKE_SOURCE_DIR}/src/dipitvhead/mux/bitrate.c
            ${CMAKE_SOURCE_DIR}/src/dipitvhead/cas/cas.c
            ${CMAKE_SOURCE_DIR}/src/lib/mux/cadescbuild.c
            ${CMAKE_SOURCE_DIR}/src/lib/cas/cas_args.c
            ${CMAKE_SOURCE_DIR}/src/lib/cas/simulcrypt_msg.c
            ${CMAKE_SOURCE_DIR}/src/lib/cas/ecmg_client/ecmg_client.c
            ${CMAKE_SOURCE_DIR}/src/lib/cas/ecmg_client/protocol.c
            ${CMAKE_SOURCE_DIR}/src/lib/cas/ecmg_client/connect.c
            ${CMAKE_SOURCE_DIR}/src/lib/cas/ecmg_client/run.c
            ${CWENC_SRC}
            ${CMAKE_SOURCE_DIR}/src/lib/helper/secure_zero.c
            ${CMAKE_SOURCE_DIR}/src/lib/cas/emmg_server/emmg_server.c
            ${CMAKE_SOURCE_DIR}/src/lib/cas/emmg_server/protocol.c
            ${CMAKE_SOURCE_DIR}/src/lib/cas/emmg_server/worker.c
            ${CMAKE_SOURCE_DIR}/src/lib/cas/cas_group.c
            ${CMAKE_SOURCE_DIR}/src/lib/cas/cas_scramble_engine.c
            ${CMAKE_SOURCE_DIR}/src/lib/cas/cas_core.c
            ${CMAKE_SOURCE_DIR}/src/lib/helper/log.c
            ${CMAKE_SOURCE_DIR}/src/lib/helper/toolmain.c
            ${CMAKE_SOURCE_DIR}/src/lib/helper/argutil.c
            ${CMAKE_SOURCE_DIR}/src/lib/helper/uriparse.c
            ${CMAKE_SOURCE_DIR}/src/lib/helper/signal.c
            ${CMAKE_SOURCE_DIR}/src/lib/metrics/protocol.c
            ${CMAKE_SOURCE_DIR}/src/lib/metrics/export.c
            ${CMAKE_SOURCE_DIR}/src/lib/net/multicast.c
            ${CMAKE_SOURCE_DIR}/src/lib/net/netconnect.c
            ${CMAKE_SOURCE_DIR}/src/lib/net/tssource.c
            ${CMAKE_SOURCE_DIR}/src/lib/net/retryset.c
            ${TLS_SRC}
            ${RIST_SRC}
            ${SRT_SRC}
            ${CMAKE_SOURCE_DIR}/src/lib/net/httpclient/httpclient.c
            ${CMAKE_SOURCE_DIR}/src/lib/net/httpclient/url.c
            ${CMAKE_SOURCE_DIR}/src/lib/net/httpclient/read.c
            ${CMAKE_SOURCE_DIR}/src/lib/vendor/picohttpparser/picohttpparser.c
            ${CMAKE_SOURCE_DIR}/src/lib/net/httpclient/async.c
            ${CMAKE_SOURCE_DIR}/src/lib/helper/ioutil.c
            ${CMAKE_SOURCE_DIR}/src/lib/demux/crc32.c
            ${CMAKE_SOURCE_DIR}/src/lib/demux/psi/psi.c
            ${CMAKE_SOURCE_DIR}/src/lib/demux/psi/parse.c
            ${CMAKE_SOURCE_DIR}/src/lib/demux/psi/descriptors.c
            ${CMAKE_SOURCE_DIR}/src/lib/demux/psi/section_asm.c
            ${CMAKE_SOURCE_DIR}/src/lib/demux/tspack.c
            ${CMAKE_SOURCE_DIR}/src/lib/demux/rtp.c
            ${CMAKE_SOURCE_DIR}/src/lib/mux/rtpheader.c
            ${CMAKE_SOURCE_DIR}/src/lib/mux/psi_build.c
            ${CMAKE_SOURCE_DIR}/src/lib/mux/mpts.c
            ${CMAKE_SOURCE_DIR}/src/lib/mux/tspacket_write.c
            ${CMAKE_SOURCE_DIR}/src/lib/scrambler/scrambler.c
            ${CISSA_SRC}
            ${CSA2_SRC}
            ${BISS_SRC}
            ${BISS_CA_SRC}
            ${CMAKE_SOURCE_DIR}/src/lib/cas/biss/ca_sections.c
            ${CMAKE_SOURCE_DIR}/src/lib/cas/biss/ca_engine.c)
    set(DIPITVHEAD_SRCS ${DIPITVHEAD_SRCS} PARENT_SCOPE)
    set(DIPITVHEAD_HAVE_TLS ${DIPITVHEAD_HAVE_TLS} PARENT_SCOPE)
    set(DIPITVHEAD_HAVE_CISSA ${DIPITVHEAD_HAVE_CISSA} PARENT_SCOPE)
    set(DIPITVHEAD_CSA ${DIPITVHEAD_CSA} PARENT_SCOPE)
    set(DIPITVHEAD_ATOMIC_LIB ${ATOMIC_LIB} PARENT_SCOPE)
endfunction()

function(dipixmltv_resolve_sources)
    set(DIPIXMLTV_SRCS
            ${CMAKE_SOURCE_DIR}/src/dipixmltv/main.c
            ${CMAKE_SOURCE_DIR}/src/dipixmltv/args.c
            ${CMAKE_SOURCE_DIR}/src/dipixmltv/revmap.c
            ${CMAKE_SOURCE_DIR}/src/dipixmltv/suggest.c
            ${CMAKE_SOURCE_DIR}/src/lib/helper/log.c
            ${CMAKE_SOURCE_DIR}/src/lib/helper/argutil.c
            ${CMAKE_SOURCE_DIR}/src/lib/helper/xml_util.c
            ${CMAKE_SOURCE_DIR}/src/lib/helper/ioutil.c
            ${CMAKE_SOURCE_DIR}/src/lib/tva/bcg_doc.c
            ${CMAKE_SOURCE_DIR}/src/lib/tva/tva_xml.c
            ${CMAKE_SOURCE_DIR}/src/lib/tva/mapping.c
            ${CMAKE_SOURCE_DIR}/src/lib/tva/xmltv.c
            ${CMAKE_SOURCE_DIR}/src/lib/tva/timefmt.c)
    set(DIPIXMLTV_SRCS ${DIPIXMLTV_SRCS} PARENT_SCOPE)
endfunction()
