# Copyright 2026 dvbipitools authors. Licensed under GPL-3.0-or-later.
# See NOTICE and LICENSE for details and authorship information.

find_package(PkgConfig REQUIRED)
pkg_check_modules(CHECK REQUIRED IMPORTED_TARGET check)
set(THREADS_PREFER_PTHREAD_FLAG ON)
find_package(Threads REQUIRED)

function(dvbipitools_add_unit_test name)
    add_executable(test_${name} ${ARGN})
    target_link_libraries(test_${name} PRIVATE PkgConfig::CHECK Threads::Threads)
    add_test(NAME ${name} COMMAND test_${name})
endfunction()

dvbipitools_add_unit_test(lib_demux_crc32
    unit/lib/demux/test_crc32.c
    ${CMAKE_SOURCE_DIR}/src/lib/demux/crc32.c)

dvbipitools_add_unit_test(lib_demux_psi
    unit/lib/demux/test_psi.c
    ${CMAKE_SOURCE_DIR}/src/lib/demux/psi.c
    ${CMAKE_SOURCE_DIR}/src/lib/demux/tspack.c
    ${CMAKE_SOURCE_DIR}/src/lib/demux/psi_section_asm.c
    ${CMAKE_SOURCE_DIR}/src/lib/demux/crc32.c
    ${CMAKE_SOURCE_DIR}/src/lib/log.c)

dvbipitools_add_unit_test(lib_demux_psi_section_asm
    unit/lib/demux/test_psi_section_asm.c
    ${CMAKE_SOURCE_DIR}/src/lib/demux/psi_section_asm.c
    ${CMAKE_SOURCE_DIR}/src/lib/demux/tspack.c)

dvbipitools_add_unit_test(lib_demux_bitreader
    unit/lib/demux/test_bitreader.c
    ${CMAKE_SOURCE_DIR}/src/lib/demux/bitreader.c)

dvbipitools_add_unit_test(lib_demux_rtp
    unit/lib/demux/test_rtp.c
    ${CMAKE_SOURCE_DIR}/src/lib/demux/rtp.c)

dvbipitools_add_unit_test(lib_demux_rtx
    unit/lib/demux/test_rtx.c
    ${CMAKE_SOURCE_DIR}/src/lib/demux/rtx.c
    ${CMAKE_SOURCE_DIR}/src/lib/demux/rtp.c)

dvbipitools_add_unit_test(lib_demux_tspack
    unit/lib/demux/test_tspack.c
    ${CMAKE_SOURCE_DIR}/src/lib/demux/tspack.c)

dvbipitools_add_unit_test(lib_demux_pes
    unit/lib/demux/test_pes.c
    ${CMAKE_SOURCE_DIR}/src/lib/demux/pes.c
    ${CMAKE_SOURCE_DIR}/src/lib/demux/tspack.c)

dvbipitools_add_unit_test(lib_mux_psi_build
    unit/lib/mux/test_psi_build.c
    ${CMAKE_SOURCE_DIR}/src/lib/mux/psi_build.c
    ${CMAKE_SOURCE_DIR}/src/lib/demux/psi.c
    ${CMAKE_SOURCE_DIR}/src/lib/demux/tspack.c
    ${CMAKE_SOURCE_DIR}/src/lib/demux/psi_section_asm.c
    ${CMAKE_SOURCE_DIR}/src/lib/demux/crc32.c
    ${CMAKE_SOURCE_DIR}/src/lib/log.c)

dvbipitools_add_unit_test(lib_demux_mpts_probe
    unit/lib/demux/test_mpts_probe.c
    ${CMAKE_SOURCE_DIR}/src/lib/demux/mpts_probe.c
    ${CMAKE_SOURCE_DIR}/src/lib/demux/psi.c
    ${CMAKE_SOURCE_DIR}/src/lib/demux/psi_section_asm.c
    ${CMAKE_SOURCE_DIR}/src/lib/demux/tspack.c
    ${CMAKE_SOURCE_DIR}/src/lib/demux/crc32.c
    ${CMAKE_SOURCE_DIR}/src/lib/mux/psi_build.c
    ${CMAKE_SOURCE_DIR}/src/lib/net/tssource.c
    ${CMAKE_SOURCE_DIR}/src/lib/net/httpclient/httpclient.c
    ${CMAKE_SOURCE_DIR}/src/lib/net/httpclient/url.c
    ${CMAKE_SOURCE_DIR}/src/lib/net/httpclient/read.c
    ${CMAKE_SOURCE_DIR}/src/lib/net/httpclient/async.c
    ${CMAKE_SOURCE_DIR}/src/lib/ioutil.c
    ${CMAKE_SOURCE_DIR}/src/lib/net/netconnect.c
    ${CMAKE_SOURCE_DIR}/src/lib/net/tls_stub.c
    ${CMAKE_SOURCE_DIR}/src/lib/net/multicast.c
    ${CMAKE_SOURCE_DIR}/src/lib/net/udpxy.c
    ${CMAKE_SOURCE_DIR}/src/lib/demux/rtp.c
    ${CMAKE_SOURCE_DIR}/src/lib/signal.c
    ${CMAKE_SOURCE_DIR}/src/lib/log.c)

dvbipitools_add_unit_test(lib_mux_rtpheader
    unit/lib/mux/test_rtpheader.c
    ${CMAKE_SOURCE_DIR}/src/lib/mux/rtpheader.c
    ${CMAKE_SOURCE_DIR}/src/lib/demux/rtp.c)

dvbipitools_add_unit_test(lib_mux_rtx
    unit/lib/mux/test_rtx.c
    ${CMAKE_SOURCE_DIR}/src/lib/mux/rtx.c
    ${CMAKE_SOURCE_DIR}/src/lib/demux/rtx.c
    ${CMAKE_SOURCE_DIR}/src/lib/demux/rtp.c)

dvbipitools_add_unit_test(lib_mux_rtcp_build
    unit/lib/mux/test_rtcp_build.c
    ${CMAKE_SOURCE_DIR}/src/lib/mux/rtcp_build.c
    ${CMAKE_SOURCE_DIR}/src/lib/demux/rtcp.c)

dvbipitools_add_unit_test(lib_mux_tspacket_write
    unit/lib/mux/test_tspacket_write.c
    ${CMAKE_SOURCE_DIR}/src/lib/mux/tspacket_write.c)

dvbipitools_add_unit_test(lib_mux_ebml
    unit/lib/mux/test_ebml.c
    ${CMAKE_SOURCE_DIR}/src/lib/mux/ebml.c)

dvbipitools_add_unit_test(lib_mux_teletext
    unit/lib/mux/test_teletext.c
    ${CMAKE_SOURCE_DIR}/src/lib/mux/teletext.c
    ${CMAKE_SOURCE_DIR}/src/lib/demux/pes.c
    ${CMAKE_SOURCE_DIR}/src/lib/demux/tspack.c)

dvbipitools_add_unit_test(lib_mux_mkv
    unit/lib/mux/test_mkv.c
    ${CMAKE_SOURCE_DIR}/src/lib/mux/mkv/mkv.c
    ${CMAKE_SOURCE_DIR}/src/lib/mux/mkv/bitreader.c
    ${CMAKE_SOURCE_DIR}/src/lib/mux/mkv/audio.c
    ${CMAKE_SOURCE_DIR}/src/lib/mux/mkv/video.c
    ${CMAKE_SOURCE_DIR}/src/lib/mux/mkv/write.c
    ${CMAKE_SOURCE_DIR}/src/lib/mux/mkv/feed.c
    ${CMAKE_SOURCE_DIR}/src/lib/ioutil.c
    ${CMAKE_SOURCE_DIR}/src/lib/mux/ebml.c
    ${CMAKE_SOURCE_DIR}/src/lib/mux/teletext.c
    ${CMAKE_SOURCE_DIR}/src/lib/mux/psi_build.c
    ${CMAKE_SOURCE_DIR}/src/lib/demux/pes.c
    ${CMAKE_SOURCE_DIR}/src/lib/demux/psi.c
    ${CMAKE_SOURCE_DIR}/src/lib/demux/tspack.c
    ${CMAKE_SOURCE_DIR}/src/lib/demux/psi_section_asm.c
    ${CMAKE_SOURCE_DIR}/src/lib/demux/crc32.c
    ${CMAKE_SOURCE_DIR}/src/lib/log.c)

dvbipitools_add_unit_test(lib_mux_cadescbuild
    unit/lib/mux/test_cadescbuild.c
    ${CMAKE_SOURCE_DIR}/src/lib/mux/cadescbuild.c
    ${CMAKE_SOURCE_DIR}/src/lib/mux/psi_build.c
    ${CMAKE_SOURCE_DIR}/src/lib/demux/crc32.c)

dvbipitools_add_unit_test(lib_net_netconnect
    unit/lib/net/test_netconnect.c
    ${CMAKE_SOURCE_DIR}/src/lib/net/netconnect.c
    ${CMAKE_SOURCE_DIR}/src/lib/signal.c
    ${CMAKE_SOURCE_DIR}/src/lib/log.c)

dvbipitools_add_unit_test(lib_net_httpclient_async
    unit/lib/net/test_httpclient_async.c
    ${CMAKE_SOURCE_DIR}/src/lib/net/httpclient/httpclient.c
    ${CMAKE_SOURCE_DIR}/src/lib/net/httpclient/url.c
    ${CMAKE_SOURCE_DIR}/src/lib/net/httpclient/read.c
    ${CMAKE_SOURCE_DIR}/src/lib/net/httpclient/async.c
    ${CMAKE_SOURCE_DIR}/src/lib/ioutil.c
    ${CMAKE_SOURCE_DIR}/src/lib/net/netconnect.c
    ${CMAKE_SOURCE_DIR}/src/lib/net/tls_stub.c
    ${CMAKE_SOURCE_DIR}/src/lib/signal.c
    ${CMAKE_SOURCE_DIR}/src/lib/log.c)

dvbipitools_add_unit_test(lib_net_tssource_async
    unit/lib/net/test_tssource_async.c
    ${CMAKE_SOURCE_DIR}/src/lib/net/tssource.c
    ${CMAKE_SOURCE_DIR}/src/lib/net/httpclient/httpclient.c
    ${CMAKE_SOURCE_DIR}/src/lib/net/httpclient/url.c
    ${CMAKE_SOURCE_DIR}/src/lib/net/httpclient/read.c
    ${CMAKE_SOURCE_DIR}/src/lib/net/httpclient/async.c
    ${CMAKE_SOURCE_DIR}/src/lib/ioutil.c
    ${CMAKE_SOURCE_DIR}/src/lib/net/netconnect.c
    ${CMAKE_SOURCE_DIR}/src/lib/net/tls_stub.c
    ${CMAKE_SOURCE_DIR}/src/lib/net/multicast.c
    ${CMAKE_SOURCE_DIR}/src/lib/net/udpxy.c
    ${CMAKE_SOURCE_DIR}/src/lib/demux/rtp.c
    ${CMAKE_SOURCE_DIR}/src/lib/signal.c
    ${CMAKE_SOURCE_DIR}/src/lib/log.c)

dvbipitools_add_unit_test(lib_net_tssource_file
    unit/lib/net/test_tssource_file.c
    ${CMAKE_SOURCE_DIR}/src/lib/net/tssource.c
    ${CMAKE_SOURCE_DIR}/src/lib/net/httpclient/httpclient.c
    ${CMAKE_SOURCE_DIR}/src/lib/net/httpclient/url.c
    ${CMAKE_SOURCE_DIR}/src/lib/net/httpclient/read.c
    ${CMAKE_SOURCE_DIR}/src/lib/net/httpclient/async.c
    ${CMAKE_SOURCE_DIR}/src/lib/ioutil.c
    ${CMAKE_SOURCE_DIR}/src/lib/net/netconnect.c
    ${CMAKE_SOURCE_DIR}/src/lib/net/tls_stub.c
    ${CMAKE_SOURCE_DIR}/src/lib/net/multicast.c
    ${CMAKE_SOURCE_DIR}/src/lib/net/udpxy.c
    ${CMAKE_SOURCE_DIR}/src/lib/demux/rtp.c
    ${CMAKE_SOURCE_DIR}/src/lib/signal.c
    ${CMAKE_SOURCE_DIR}/src/lib/log.c)

dvbipitools_add_unit_test(lib_net_retryset
    unit/lib/net/test_retryset.c
    ${CMAKE_SOURCE_DIR}/src/lib/net/retryset.c
    ${CMAKE_SOURCE_DIR}/src/lib/log.c)

find_package(OpenSSL)
if (OpenSSL_FOUND)
    dvbipitools_add_unit_test(lib_scrambler_cissa
        unit/lib/scrambler/test_cissa.c
        ${CMAKE_SOURCE_DIR}/src/lib/scrambler/scrambler.c
        ${CMAKE_SOURCE_DIR}/src/lib/scrambler/cissa.c
        ${CMAKE_SOURCE_DIR}/src/lib/scrambler/csa2_stub.c
        ${CMAKE_SOURCE_DIR}/src/lib/log.c)
    target_link_libraries(test_lib_scrambler_cissa PRIVATE OpenSSL::Crypto)

    dvbipitools_add_unit_test(lib_cas_biss
        unit/lib/cas/test_biss.c
        ${CMAKE_SOURCE_DIR}/src/lib/cas/biss/biss.c)
    target_link_libraries(test_lib_cas_biss PRIVATE OpenSSL::Crypto)

    dvbipitools_add_unit_test(lib_cas_biss_ca
        unit/lib/cas/test_biss_ca.c
        ${CMAKE_SOURCE_DIR}/src/lib/cas/biss/ca.c)
    target_link_libraries(test_lib_cas_biss_ca PRIVATE OpenSSL::Crypto)

    dvbipitools_add_unit_test(lib_cas_biss_ca_engine
        unit/lib/cas/test_biss_ca_engine.c
        ${CMAKE_SOURCE_DIR}/src/lib/cas/biss/ca_engine.c
        ${CMAKE_SOURCE_DIR}/src/lib/cas/biss/ca.c
        ${CMAKE_SOURCE_DIR}/src/lib/cas/biss/ca_sections.c
        ${CMAKE_SOURCE_DIR}/src/lib/cas/cas_scramble_engine.c
        ${CMAKE_SOURCE_DIR}/src/lib/scrambler/scrambler.c
        ${CMAKE_SOURCE_DIR}/src/lib/scrambler/cissa.c
        ${CMAKE_SOURCE_DIR}/src/lib/scrambler/csa2_stub.c
        ${CMAKE_SOURCE_DIR}/src/lib/mux/cadescbuild.c
        ${CMAKE_SOURCE_DIR}/src/lib/mux/psi_build.c
        ${CMAKE_SOURCE_DIR}/src/lib/demux/crc32.c
        ${CMAKE_SOURCE_DIR}/src/lib/log.c)
    target_link_libraries(test_lib_cas_biss_ca_engine PRIVATE OpenSSL::Crypto)
else ()
    message(WARNING "tests: OpenSSL not found, skipping lib_scrambler_cissa/lib_cas_biss/lib_cas_biss_ca/lib_cas_biss_ca_engine unit tests")
endif ()

dvbipitools_add_unit_test(lib_cas_biss_ca_sections
    unit/lib/cas/test_biss_ca_sections.c
    ${CMAKE_SOURCE_DIR}/src/lib/cas/biss/ca_sections.c
    ${CMAKE_SOURCE_DIR}/src/lib/mux/psi_build.c
    ${CMAKE_SOURCE_DIR}/src/lib/demux/crc32.c)

find_path(DVBCSA_INCLUDE_DIR dvbcsa/dvbcsa.h)
find_library(DVBCSA_LIBRARY dvbcsa)
if (DVBCSA_INCLUDE_DIR AND DVBCSA_LIBRARY)
    dvbipitools_add_unit_test(lib_scrambler_csa2
        unit/lib/scrambler/test_csa2.c
        ${CMAKE_SOURCE_DIR}/src/lib/scrambler/scrambler.c
        ${CMAKE_SOURCE_DIR}/src/lib/scrambler/csa2.c
        ${CMAKE_SOURCE_DIR}/src/lib/scrambler/cissa_stub.c
        ${CMAKE_SOURCE_DIR}/src/lib/log.c)
    target_include_directories(test_lib_scrambler_csa2 PRIVATE ${DVBCSA_INCLUDE_DIR})
    target_link_libraries(test_lib_scrambler_csa2 PRIVATE ${DVBCSA_LIBRARY})
else ()
    message(WARNING "tests: libdvbcsa not found, skipping lib_scrambler_csa2 unit test")
endif ()

if (OpenSSL_FOUND)
    dvbipitools_add_unit_test(dipicam378_crypto
        unit/dipicam378/test_crypto.c
        ${CMAKE_SOURCE_DIR}/src/dipicam378/crypto.c)
    target_link_libraries(test_dipicam378_crypto PRIVATE OpenSSL::Crypto)

    dvbipitools_add_unit_test(dipicam378_device
        unit/dipicam378/test_device.c
        ${CMAKE_SOURCE_DIR}/src/dipicam378/device.c
        ${CMAKE_SOURCE_DIR}/src/dipicam378/crypto.c
        ${CMAKE_SOURCE_DIR}/src/lib/log.c)
    target_link_libraries(test_dipicam378_device PRIVATE OpenSSL::Crypto)

    dvbipitools_add_unit_test(dipicam378_cs378x
        unit/dipicam378/test_cs378x.c
        ${CMAKE_SOURCE_DIR}/src/dipicam378/cs378x.c
        ${CMAKE_SOURCE_DIR}/src/lib/log.c
        ${CMAKE_SOURCE_DIR}/src/lib/signal.c)
    target_link_libraries(test_dipicam378_cs378x PRIVATE OpenSSL::Crypto Threads::Threads)

    dvbipitools_add_unit_test(dipidescramble_crypto
        unit/dipidescramble/test_crypto.c
        ${CMAKE_SOURCE_DIR}/src/dipidescramble/crypto.c)
    target_link_libraries(test_dipidescramble_crypto PRIVATE OpenSSL::Crypto)

    dvbipitools_add_unit_test(dipidescramble_device
        unit/dipidescramble/test_device.c
        ${CMAKE_SOURCE_DIR}/src/dipidescramble/device.c
        ${CMAKE_SOURCE_DIR}/src/dipidescramble/crypto.c
        ${CMAKE_SOURCE_DIR}/src/lib/log.c)
    target_link_libraries(test_dipidescramble_device PRIVATE OpenSSL::Crypto)

    dvbipitools_add_unit_test(dipidescramble_biss_ca_state
        unit/dipidescramble/test_biss_ca_state.c
        ${CMAKE_SOURCE_DIR}/src/dipidescramble/biss_ca_state.c
        ${CMAKE_SOURCE_DIR}/src/lib/cas/biss/ca.c
        ${CMAKE_SOURCE_DIR}/src/lib/cas/biss/ca_sections.c
        ${CMAKE_SOURCE_DIR}/src/lib/mux/psi_build.c
        ${CMAKE_SOURCE_DIR}/src/lib/demux/crc32.c)
    target_link_libraries(test_dipidescramble_biss_ca_state PRIVATE OpenSSL::Crypto)
else ()
    message(WARNING "tests: OpenSSL not found, skipping dipicam378_crypto/dipicam378_device/dipicam378_cs378x/dipidescramble_crypto/dipidescramble_device/dipidescramble_biss_ca_state unit tests")
endif ()

dvbipitools_add_unit_test(lib_bim_bitwriter
    unit/lib/bim/test_bitwriter.c
    ${CMAKE_SOURCE_DIR}/src/lib/bim/bitwriter.c
    ${CMAKE_SOURCE_DIR}/src/lib/bim/bitreader.c)

dvbipitools_add_unit_test(lib_bim_bitreader
    unit/lib/bim/test_bitreader.c
    ${CMAKE_SOURCE_DIR}/src/lib/bim/bitreader.c)

dvbipitools_add_unit_test(lib_bim_strrepo
    unit/lib/bim/test_strrepo.c
    ${CMAKE_SOURCE_DIR}/src/lib/bim/strrepo.c)

dvbipitools_add_unit_test(lib_bim_codec
    unit/lib/bim/test_codec.c
    ${CMAKE_SOURCE_DIR}/src/lib/bim/codec.c
    ${CMAKE_SOURCE_DIR}/src/lib/bim/bitwriter.c
    ${CMAKE_SOURCE_DIR}/src/lib/bim/bitreader.c
    ${CMAKE_SOURCE_DIR}/src/lib/bim/strrepo.c)

dvbipitools_add_unit_test(lib_xml_util
    unit/lib/test_xml_util.c
    ${CMAKE_SOURCE_DIR}/src/lib/xml_util.c)

dvbipitools_add_unit_test(lib_sds_xml
    unit/lib/test_sds_xml.c
    ${CMAKE_SOURCE_DIR}/src/lib/sds_xml.c
    ${CMAKE_SOURCE_DIR}/src/lib/xml_util.c)

dvbipitools_add_unit_test(lib_tva_timefmt
    unit/lib/tva/test_timefmt.c
    ${CMAKE_SOURCE_DIR}/src/lib/tva/timefmt.c)

dvbipitools_add_unit_test(lib_tva_bcg_doc
    unit/lib/tva/test_bcg_doc.c
    ${CMAKE_SOURCE_DIR}/src/lib/tva/bcg_doc.c
    ${CMAKE_SOURCE_DIR}/src/lib/log.c)

dvbipitools_add_unit_test(lib_tva_mapping
    unit/lib/tva/test_mapping.c
    ${CMAKE_SOURCE_DIR}/src/lib/tva/mapping.c
    ${CMAKE_SOURCE_DIR}/src/lib/ioutil.c)

dvbipitools_add_unit_test(lib_tva_xmltv
    unit/lib/tva/test_xmltv.c
    ${CMAKE_SOURCE_DIR}/src/lib/tva/xmltv.c
    ${CMAKE_SOURCE_DIR}/src/lib/tva/timefmt.c
    ${CMAKE_SOURCE_DIR}/src/lib/tva/bcg_doc.c
    ${CMAKE_SOURCE_DIR}/src/lib/xml_util.c
    ${CMAKE_SOURCE_DIR}/src/lib/ioutil.c
    ${CMAKE_SOURCE_DIR}/src/lib/log.c)

dvbipitools_add_unit_test(lib_tva_tva_xml
    unit/lib/tva/test_tva_xml.c
    ${CMAKE_SOURCE_DIR}/src/lib/tva/tva_xml.c
    ${CMAKE_SOURCE_DIR}/src/lib/tva/bcg_doc.c
    ${CMAKE_SOURCE_DIR}/src/lib/xml_util.c
    ${CMAKE_SOURCE_DIR}/src/lib/ioutil.c
    ${CMAKE_SOURCE_DIR}/src/lib/log.c)

set(BIM_FRAGMENT_DEPS
    ${CMAKE_SOURCE_DIR}/src/lib/bim/fragment.c
    ${CMAKE_SOURCE_DIR}/src/lib/bim/codec.c
    ${CMAKE_SOURCE_DIR}/src/lib/bim/bitwriter.c
    ${CMAKE_SOURCE_DIR}/src/lib/bim/bitreader.c
    ${CMAKE_SOURCE_DIR}/src/lib/bim/strrepo.c
    ${CMAKE_SOURCE_DIR}/src/lib/tva/tva_xml.c
    ${CMAKE_SOURCE_DIR}/src/lib/tva/bcg_doc.c
    ${CMAKE_SOURCE_DIR}/src/lib/xml_util.c
    ${CMAKE_SOURCE_DIR}/src/lib/ioutil.c
    ${CMAKE_SOURCE_DIR}/src/lib/log.c)

dvbipitools_add_unit_test(lib_bim_fragment
    unit/lib/bim/test_fragment.c
    ${BIM_FRAGMENT_DEPS})

dvbipitools_add_unit_test(lib_bim_accessunit
    unit/lib/bim/test_accessunit.c
    ${CMAKE_SOURCE_DIR}/src/lib/bim/accessunit.c
    ${BIM_FRAGMENT_DEPS})

dvbipitools_add_unit_test(dipiscan_format
    unit/dipiscan/test_format.c
    ${CMAKE_SOURCE_DIR}/src/dipiscan/format.c
    ${CMAKE_SOURCE_DIR}/src/lib/playlist_out.c
    ${CMAKE_SOURCE_DIR}/src/lib/sds_xml.c
    ${CMAKE_SOURCE_DIR}/src/lib/xml_util.c)

dvbipitools_add_unit_test(dipixmltv_revmap
    unit/dipixmltv/test_revmap.c
    ${CMAKE_SOURCE_DIR}/src/dipixmltv/revmap.c
    ${CMAKE_SOURCE_DIR}/src/lib/ioutil.c)

dvbipitools_add_unit_test(dipixmltv_suggest
    unit/dipixmltv/test_suggest.c
    ${CMAKE_SOURCE_DIR}/src/dipixmltv/suggest.c
    ${CMAKE_SOURCE_DIR}/src/lib/tva/xmltv.c
    ${CMAKE_SOURCE_DIR}/src/lib/tva/timefmt.c
    ${CMAKE_SOURCE_DIR}/src/lib/tva/bcg_doc.c
    ${CMAKE_SOURCE_DIR}/src/lib/xml_util.c
    ${CMAKE_SOURCE_DIR}/src/lib/ioutil.c
    ${CMAKE_SOURCE_DIR}/src/lib/log.c)

dvbipitools_add_unit_test(dipiradiohead_mpegaudio
    unit/dipiradiohead/test_mpegaudio.c
    ${CMAKE_SOURCE_DIR}/src/dipiradiohead/framer/mpegaudio.c)

dvbipitools_add_unit_test(dipiradiohead_aac_adts
    unit/dipiradiohead/test_aac_adts.c
    ${CMAKE_SOURCE_DIR}/src/dipiradiohead/framer/aac_adts.c)

dvbipitools_add_unit_test(dipiradiohead_aac_latm
    unit/dipiradiohead/test_aac_latm.c
    ${CMAKE_SOURCE_DIR}/src/dipiradiohead/framer/aac_latm.c
    ${CMAKE_SOURCE_DIR}/src/lib/demux/bitreader.c
    ${CMAKE_SOURCE_DIR}/src/lib/bim/bitwriter.c)

dvbipitools_add_unit_test(dipiradiohead_psi
    unit/dipiradiohead/test_psi.c
    ${CMAKE_SOURCE_DIR}/src/dipiradiohead/mux/psi.c
    ${CMAKE_SOURCE_DIR}/src/lib/mux/psi_build.c
    ${CMAKE_SOURCE_DIR}/src/lib/demux/psi.c
    ${CMAKE_SOURCE_DIR}/src/lib/demux/tspack.c
    ${CMAKE_SOURCE_DIR}/src/lib/demux/psi_section_asm.c
    ${CMAKE_SOURCE_DIR}/src/lib/demux/crc32.c
    ${CMAKE_SOURCE_DIR}/src/lib/log.c)

dvbipitools_add_unit_test(dipiradiohead_id3
    unit/dipiradiohead/test_id3.c
    ${CMAKE_SOURCE_DIR}/src/dipiradiohead/input/id3.c
    ${CMAKE_SOURCE_DIR}/src/lib/ioutil.c)

dvbipitools_add_unit_test(dipiradiohead_pes
    unit/dipiradiohead/test_pes.c
    ${CMAKE_SOURCE_DIR}/src/dipiradiohead/mux/pes.c
    ${CMAKE_SOURCE_DIR}/src/lib/demux/pes.c
    ${CMAKE_SOURCE_DIR}/src/lib/demux/tspack.c)

dvbipitools_add_unit_test(dipiradiohead_tspacketizer
    unit/dipiradiohead/test_tspacketizer.c
    ${CMAKE_SOURCE_DIR}/src/dipiradiohead/mux/tspacketizer.c
    ${CMAKE_SOURCE_DIR}/src/lib/ioutil.c
    ${CMAKE_SOURCE_DIR}/src/dipiradiohead/mux/psi.c
    ${CMAKE_SOURCE_DIR}/src/dipiradiohead/mux/pes.c
    ${CMAKE_SOURCE_DIR}/src/dipiradiohead/cas/cas.c
    ${CMAKE_SOURCE_DIR}/src/lib/mux/cadescbuild.c
    ${CMAKE_SOURCE_DIR}/src/lib/cas/ecmg_client.c
    ${CMAKE_SOURCE_DIR}/src/lib/cas/emmg_server.c
    ${CMAKE_SOURCE_DIR}/src/lib/cas/simulcrypt_msg.c
    ${CMAKE_SOURCE_DIR}/src/lib/cas/cas_group.c
    ${CMAKE_SOURCE_DIR}/src/lib/cas/cas_scramble_engine.c
    ${CMAKE_SOURCE_DIR}/src/lib/mux/psi_build.c
    ${CMAKE_SOURCE_DIR}/src/lib/mux/tspacket_write.c
    ${CMAKE_SOURCE_DIR}/src/lib/demux/psi.c
    ${CMAKE_SOURCE_DIR}/src/lib/demux/tspack.c
    ${CMAKE_SOURCE_DIR}/src/lib/demux/psi_section_asm.c
    ${CMAKE_SOURCE_DIR}/src/lib/demux/crc32.c
    ${CMAKE_SOURCE_DIR}/src/lib/scrambler/scrambler.c
    ${CMAKE_SOURCE_DIR}/src/lib/scrambler/cissa_stub.c
    ${CMAKE_SOURCE_DIR}/src/lib/scrambler/csa2_stub.c
    ${CMAKE_SOURCE_DIR}/src/lib/cas/biss/stub.c
    ${CMAKE_SOURCE_DIR}/src/lib/cas/biss/ca_stub.c
    ${CMAKE_SOURCE_DIR}/src/lib/cas/biss/ca_sections.c
    ${CMAKE_SOURCE_DIR}/src/lib/cas/biss/ca_engine.c
    ${CMAKE_SOURCE_DIR}/src/lib/log.c
    ${CMAKE_SOURCE_DIR}/src/lib/signal.c)

dvbipitools_add_unit_test(dipiradiohead_args
    unit/dipiradiohead/test_args.c
    ${CMAKE_SOURCE_DIR}/src/dipiradiohead/args.c
    ${CMAKE_SOURCE_DIR}/src/lib/argutil.c
    ${CMAKE_SOURCE_DIR}/src/lib/cas/cas_args.c
    ${CMAKE_SOURCE_DIR}/src/lib/cas/biss/stub.c
    ${CMAKE_SOURCE_DIR}/src/lib/cas/biss/ca_stub.c
    ${CMAKE_SOURCE_DIR}/src/lib/log.c)

dvbipitools_add_unit_test(dipiradiohead_source_async
    unit/dipiradiohead/input/test_source_async.c
    ${CMAKE_SOURCE_DIR}/src/dipiradiohead/input/source.c
    ${CMAKE_SOURCE_DIR}/src/dipiradiohead/input/playlist.c
    ${CMAKE_SOURCE_DIR}/src/dipiradiohead/input/icy.c
    ${CMAKE_SOURCE_DIR}/src/dipiradiohead/input/id3.c
    ${CMAKE_SOURCE_DIR}/src/dipiradiohead/framer/mpegaudio.c
    ${CMAKE_SOURCE_DIR}/src/dipiradiohead/framer/aac_adts.c
    ${CMAKE_SOURCE_DIR}/src/dipiradiohead/framer/aac_latm.c
    ${CMAKE_SOURCE_DIR}/src/lib/demux/bitreader.c
    ${CMAKE_SOURCE_DIR}/src/lib/net/httpclient/httpclient.c
    ${CMAKE_SOURCE_DIR}/src/lib/net/httpclient/url.c
    ${CMAKE_SOURCE_DIR}/src/lib/net/httpclient/read.c
    ${CMAKE_SOURCE_DIR}/src/lib/net/httpclient/async.c
    ${CMAKE_SOURCE_DIR}/src/lib/ioutil.c
    ${CMAKE_SOURCE_DIR}/src/lib/net/netconnect.c
    ${CMAKE_SOURCE_DIR}/src/lib/net/tls_stub.c
    ${CMAKE_SOURCE_DIR}/src/lib/signal.c
    ${CMAKE_SOURCE_DIR}/src/lib/log.c)

dvbipitools_add_unit_test(dipiradiohead_inputset
    unit/dipiradiohead/input/test_inputset.c
    ${CMAKE_SOURCE_DIR}/src/dipiradiohead/input/inputset.c
    ${CMAKE_SOURCE_DIR}/src/lib/net/retryset.c
    ${CMAKE_SOURCE_DIR}/src/dipiradiohead/input/source.c
    ${CMAKE_SOURCE_DIR}/src/dipiradiohead/input/playlist.c
    ${CMAKE_SOURCE_DIR}/src/dipiradiohead/input/icy.c
    ${CMAKE_SOURCE_DIR}/src/dipiradiohead/input/id3.c
    ${CMAKE_SOURCE_DIR}/src/dipiradiohead/framer/mpegaudio.c
    ${CMAKE_SOURCE_DIR}/src/dipiradiohead/framer/aac_adts.c
    ${CMAKE_SOURCE_DIR}/src/dipiradiohead/framer/aac_latm.c
    ${CMAKE_SOURCE_DIR}/src/lib/demux/bitreader.c
    ${CMAKE_SOURCE_DIR}/src/lib/net/httpclient/httpclient.c
    ${CMAKE_SOURCE_DIR}/src/lib/net/httpclient/url.c
    ${CMAKE_SOURCE_DIR}/src/lib/net/httpclient/read.c
    ${CMAKE_SOURCE_DIR}/src/lib/net/httpclient/async.c
    ${CMAKE_SOURCE_DIR}/src/lib/ioutil.c
    ${CMAKE_SOURCE_DIR}/src/lib/net/netconnect.c
    ${CMAKE_SOURCE_DIR}/src/lib/net/tls_stub.c
    ${CMAKE_SOURCE_DIR}/src/lib/signal.c
    ${CMAKE_SOURCE_DIR}/src/lib/log.c)

dvbipitools_add_unit_test(lib_mux_mpts
    unit/lib/mux/test_mpts.c
    ${CMAKE_SOURCE_DIR}/src/lib/mux/mpts.c
    ${CMAKE_SOURCE_DIR}/src/lib/mux/psi_build.c
    ${CMAKE_SOURCE_DIR}/src/lib/mux/tspacket_write.c
    ${CMAKE_SOURCE_DIR}/src/lib/demux/psi.c
    ${CMAKE_SOURCE_DIR}/src/lib/demux/tspack.c
    ${CMAKE_SOURCE_DIR}/src/lib/demux/psi_section_asm.c
    ${CMAKE_SOURCE_DIR}/src/lib/demux/crc32.c
    ${CMAKE_SOURCE_DIR}/src/lib/log.c)

dvbipitools_add_unit_test(dipiradiohead_cas
    unit/dipiradiohead/test_cas.c
    ${CMAKE_SOURCE_DIR}/src/dipiradiohead/cas/cas.c
    ${CMAKE_SOURCE_DIR}/src/lib/mux/cadescbuild.c
    ${CMAKE_SOURCE_DIR}/src/lib/cas/ecmg_client.c
    ${CMAKE_SOURCE_DIR}/src/lib/cas/emmg_server.c
    ${CMAKE_SOURCE_DIR}/src/lib/cas/simulcrypt_msg.c
    ${CMAKE_SOURCE_DIR}/src/lib/cas/cas_group.c
    ${CMAKE_SOURCE_DIR}/src/lib/cas/cas_scramble_engine.c
    ${CMAKE_SOURCE_DIR}/src/lib/mux/psi_build.c
    ${CMAKE_SOURCE_DIR}/src/lib/demux/psi.c
    ${CMAKE_SOURCE_DIR}/src/lib/demux/tspack.c
    ${CMAKE_SOURCE_DIR}/src/lib/demux/psi_section_asm.c
    ${CMAKE_SOURCE_DIR}/src/lib/demux/crc32.c
    ${CMAKE_SOURCE_DIR}/src/lib/scrambler/scrambler.c
    ${CMAKE_SOURCE_DIR}/src/lib/scrambler/cissa_stub.c
    ${CMAKE_SOURCE_DIR}/src/lib/scrambler/csa2_stub.c
    ${CMAKE_SOURCE_DIR}/src/lib/cas/biss/stub.c
    ${CMAKE_SOURCE_DIR}/src/lib/cas/biss/ca_stub.c
    ${CMAKE_SOURCE_DIR}/src/lib/cas/biss/ca_sections.c
    ${CMAKE_SOURCE_DIR}/src/lib/cas/biss/ca_engine.c
    ${CMAKE_SOURCE_DIR}/src/lib/log.c
    ${CMAKE_SOURCE_DIR}/src/lib/signal.c)

dvbipitools_add_unit_test(dipitvhead_args
    unit/dipitvhead/test_args.c
    ${CMAKE_SOURCE_DIR}/src/dipitvhead/args.c
    ${CMAKE_SOURCE_DIR}/src/lib/argutil.c
    ${CMAKE_SOURCE_DIR}/src/lib/cas/cas_args.c
    ${CMAKE_SOURCE_DIR}/src/lib/cas/biss/stub.c
    ${CMAKE_SOURCE_DIR}/src/lib/cas/biss/ca_stub.c
    ${CMAKE_SOURCE_DIR}/src/lib/net/httpclient/httpclient.c
    ${CMAKE_SOURCE_DIR}/src/lib/net/httpclient/url.c
    ${CMAKE_SOURCE_DIR}/src/lib/net/httpclient/read.c
    ${CMAKE_SOURCE_DIR}/src/lib/net/httpclient/async.c
    ${CMAKE_SOURCE_DIR}/src/lib/ioutil.c
    ${CMAKE_SOURCE_DIR}/src/lib/net/netconnect.c
    ${CMAKE_SOURCE_DIR}/src/lib/net/tls_stub.c
    ${CMAKE_SOURCE_DIR}/src/lib/log.c
    ${CMAKE_SOURCE_DIR}/src/lib/signal.c)

dvbipitools_add_unit_test(dipitvhead_pmtbuild
    unit/dipitvhead/test_pmtbuild.c
    ${CMAKE_SOURCE_DIR}/src/dipitvhead/mux/pmtbuild.c
    ${CMAKE_SOURCE_DIR}/src/lib/mux/psi_build.c
    ${CMAKE_SOURCE_DIR}/src/lib/demux/psi.c
    ${CMAKE_SOURCE_DIR}/src/lib/demux/tspack.c
    ${CMAKE_SOURCE_DIR}/src/lib/demux/psi_section_asm.c
    ${CMAKE_SOURCE_DIR}/src/lib/demux/crc32.c
    ${CMAKE_SOURCE_DIR}/src/lib/log.c)

dvbipitools_add_unit_test(dipitvhead_aitbuild
    unit/dipitvhead/test_aitbuild.c
    ${CMAKE_SOURCE_DIR}/src/dipitvhead/mux/aitbuild.c
    ${CMAKE_SOURCE_DIR}/src/lib/mux/psi_build.c
    ${CMAKE_SOURCE_DIR}/src/lib/demux/crc32.c)

dvbipitools_add_unit_test(dipitvhead_bitrate
    unit/dipitvhead/test_bitrate.c
    ${CMAKE_SOURCE_DIR}/src/dipitvhead/mux/bitrate.c
    ${CMAKE_SOURCE_DIR}/src/lib/log.c)

dvbipitools_add_unit_test(dipitvhead_remux
    unit/dipitvhead/test_remux.c
    ${CMAKE_SOURCE_DIR}/src/dipitvhead/mux/remux.c
    ${CMAKE_SOURCE_DIR}/src/dipitvhead/mux/pmtbuild.c
    ${CMAKE_SOURCE_DIR}/src/dipitvhead/mux/aitbuild.c
    ${CMAKE_SOURCE_DIR}/src/dipitvhead/cas/cas.c
    ${CMAKE_SOURCE_DIR}/src/lib/mux/cadescbuild.c
    ${CMAKE_SOURCE_DIR}/src/lib/cas/ecmg_client.c
    ${CMAKE_SOURCE_DIR}/src/lib/cas/emmg_server.c
    ${CMAKE_SOURCE_DIR}/src/lib/cas/simulcrypt_msg.c
    ${CMAKE_SOURCE_DIR}/src/lib/cas/cas_group.c
    ${CMAKE_SOURCE_DIR}/src/lib/cas/cas_scramble_engine.c
    ${CMAKE_SOURCE_DIR}/src/lib/mux/psi_build.c
    ${CMAKE_SOURCE_DIR}/src/lib/mux/tspacket_write.c
    ${CMAKE_SOURCE_DIR}/src/lib/demux/psi.c
    ${CMAKE_SOURCE_DIR}/src/lib/demux/tspack.c
    ${CMAKE_SOURCE_DIR}/src/lib/demux/psi_section_asm.c
    ${CMAKE_SOURCE_DIR}/src/lib/demux/crc32.c
    ${CMAKE_SOURCE_DIR}/src/lib/scrambler/scrambler.c
    ${CMAKE_SOURCE_DIR}/src/lib/scrambler/cissa_stub.c
    ${CMAKE_SOURCE_DIR}/src/lib/scrambler/csa2_stub.c
    ${CMAKE_SOURCE_DIR}/src/lib/cas/biss/stub.c
    ${CMAKE_SOURCE_DIR}/src/lib/cas/biss/ca_stub.c
    ${CMAKE_SOURCE_DIR}/src/lib/cas/biss/ca_sections.c
    ${CMAKE_SOURCE_DIR}/src/lib/cas/biss/ca_engine.c
    ${CMAKE_SOURCE_DIR}/src/lib/log.c
    ${CMAKE_SOURCE_DIR}/src/lib/signal.c)

dvbipitools_add_unit_test(dipitvhead_simulcrypt_msg
    unit/dipitvhead/test_simulcrypt_msg.c
    ${CMAKE_SOURCE_DIR}/src/lib/cas/simulcrypt_msg.c
    ${CMAKE_SOURCE_DIR}/src/lib/mux/psi_build.c
    ${CMAKE_SOURCE_DIR}/src/lib/demux/psi.c
    ${CMAKE_SOURCE_DIR}/src/lib/demux/tspack.c
    ${CMAKE_SOURCE_DIR}/src/lib/demux/psi_section_asm.c
    ${CMAKE_SOURCE_DIR}/src/lib/demux/crc32.c
    ${CMAKE_SOURCE_DIR}/src/lib/log.c)

dvbipitools_add_unit_test(dipitvhead_ecmg_client
    unit/dipitvhead/test_ecmg_client.c
    ${CMAKE_SOURCE_DIR}/src/lib/cas/ecmg_client.c
    ${CMAKE_SOURCE_DIR}/src/lib/cas/simulcrypt_msg.c
    ${CMAKE_SOURCE_DIR}/src/lib/mux/psi_build.c
    ${CMAKE_SOURCE_DIR}/src/lib/demux/psi.c
    ${CMAKE_SOURCE_DIR}/src/lib/demux/tspack.c
    ${CMAKE_SOURCE_DIR}/src/lib/demux/psi_section_asm.c
    ${CMAKE_SOURCE_DIR}/src/lib/demux/crc32.c
    ${CMAKE_SOURCE_DIR}/src/lib/scrambler/scrambler.c
    ${CMAKE_SOURCE_DIR}/src/lib/scrambler/cissa_stub.c
    ${CMAKE_SOURCE_DIR}/src/lib/scrambler/csa2_stub.c
    ${CMAKE_SOURCE_DIR}/src/lib/log.c
    ${CMAKE_SOURCE_DIR}/src/lib/signal.c)

dvbipitools_add_unit_test(dipitvhead_emmg_server
    unit/dipitvhead/test_emmg_server.c
    ${CMAKE_SOURCE_DIR}/src/lib/cas/emmg_server.c
    ${CMAKE_SOURCE_DIR}/src/lib/cas/simulcrypt_msg.c
    ${CMAKE_SOURCE_DIR}/src/lib/mux/psi_build.c
    ${CMAKE_SOURCE_DIR}/src/lib/demux/psi.c
    ${CMAKE_SOURCE_DIR}/src/lib/demux/tspack.c
    ${CMAKE_SOURCE_DIR}/src/lib/demux/psi_section_asm.c
    ${CMAKE_SOURCE_DIR}/src/lib/demux/crc32.c
    ${CMAKE_SOURCE_DIR}/src/lib/log.c
    ${CMAKE_SOURCE_DIR}/src/lib/signal.c)

dvbipitools_add_unit_test(dipitvhead_cas
    unit/dipitvhead/test_cas.c
    ${CMAKE_SOURCE_DIR}/src/dipitvhead/cas/cas.c
    ${CMAKE_SOURCE_DIR}/src/lib/cas/ecmg_client.c
    ${CMAKE_SOURCE_DIR}/src/lib/cas/emmg_server.c
    ${CMAKE_SOURCE_DIR}/src/lib/cas/simulcrypt_msg.c
    ${CMAKE_SOURCE_DIR}/src/lib/cas/cas_group.c
    ${CMAKE_SOURCE_DIR}/src/lib/cas/cas_scramble_engine.c
    ${CMAKE_SOURCE_DIR}/src/lib/mux/cadescbuild.c
    ${CMAKE_SOURCE_DIR}/src/lib/mux/psi_build.c
    ${CMAKE_SOURCE_DIR}/src/lib/demux/psi.c
    ${CMAKE_SOURCE_DIR}/src/lib/demux/tspack.c
    ${CMAKE_SOURCE_DIR}/src/lib/demux/psi_section_asm.c
    ${CMAKE_SOURCE_DIR}/src/lib/demux/crc32.c
    ${CMAKE_SOURCE_DIR}/src/lib/scrambler/scrambler.c
    ${CMAKE_SOURCE_DIR}/src/lib/scrambler/cissa_stub.c
    ${CMAKE_SOURCE_DIR}/src/lib/scrambler/csa2_stub.c
    ${CMAKE_SOURCE_DIR}/src/lib/cas/biss/stub.c
    ${CMAKE_SOURCE_DIR}/src/lib/cas/biss/ca_stub.c
    ${CMAKE_SOURCE_DIR}/src/lib/cas/biss/ca_sections.c
    ${CMAKE_SOURCE_DIR}/src/lib/cas/biss/ca_engine.c
    ${CMAKE_SOURCE_DIR}/src/lib/log.c
    ${CMAKE_SOURCE_DIR}/src/lib/signal.c)

dvbipitools_add_unit_test(lib_cas_cas_group
    unit/lib/cas/test_cas_group.c
    ${CMAKE_SOURCE_DIR}/src/lib/cas/cas_group.c
    ${CMAKE_SOURCE_DIR}/src/lib/cas/ecmg_client.c
    ${CMAKE_SOURCE_DIR}/src/lib/cas/emmg_server.c
    ${CMAKE_SOURCE_DIR}/src/lib/cas/simulcrypt_msg.c
    ${CMAKE_SOURCE_DIR}/src/lib/cas/cas_scramble_engine.c
    ${CMAKE_SOURCE_DIR}/src/lib/mux/cadescbuild.c
    ${CMAKE_SOURCE_DIR}/src/lib/mux/psi_build.c
    ${CMAKE_SOURCE_DIR}/src/lib/demux/psi.c
    ${CMAKE_SOURCE_DIR}/src/lib/demux/tspack.c
    ${CMAKE_SOURCE_DIR}/src/lib/demux/psi_section_asm.c
    ${CMAKE_SOURCE_DIR}/src/lib/demux/crc32.c
    ${CMAKE_SOURCE_DIR}/src/lib/scrambler/scrambler.c
    ${CMAKE_SOURCE_DIR}/src/lib/scrambler/cissa_stub.c
    ${CMAKE_SOURCE_DIR}/src/lib/scrambler/csa2_stub.c
    ${CMAKE_SOURCE_DIR}/src/lib/log.c
    ${CMAKE_SOURCE_DIR}/src/lib/signal.c)

dvbipitools_add_unit_test(dipirec_ts_filter
    unit/dipirec/test_ts_filter.c
    ${CMAKE_SOURCE_DIR}/src/dipirec/filter/ts.c
    ${CMAKE_SOURCE_DIR}/src/lib/demux/psi.c
    ${CMAKE_SOURCE_DIR}/src/lib/demux/tspack.c
    ${CMAKE_SOURCE_DIR}/src/lib/demux/psi_section_asm.c
    ${CMAKE_SOURCE_DIR}/src/lib/demux/crc32.c
    ${CMAKE_SOURCE_DIR}/src/lib/log.c)

dvbipitools_add_unit_test(dipirec_pace
    unit/dipirec/test_pace.c
    ${CMAKE_SOURCE_DIR}/src/dipirec/filter/pace.c
    ${CMAKE_SOURCE_DIR}/src/lib/signal.c)

dvbipitools_add_unit_test(dipifccret_channel
    unit/dipifccret/test_channel.c
    ${CMAKE_SOURCE_DIR}/src/dipifccret/channel.c
    ${CMAKE_SOURCE_DIR}/src/lib/mux/psi_build.c
    ${CMAKE_SOURCE_DIR}/src/lib/demux/psi.c
    ${CMAKE_SOURCE_DIR}/src/lib/demux/psi_section_asm.c
    ${CMAKE_SOURCE_DIR}/src/lib/demux/tspack.c
    ${CMAKE_SOURCE_DIR}/src/lib/demux/crc32.c
    ${CMAKE_SOURCE_DIR}/src/lib/log.c)

dvbipitools_add_unit_test(dipifccret_ret_mcsend
    unit/dipifccret/ret/test_mcsend.c
    ${CMAKE_SOURCE_DIR}/src/dipifccret/ret/mcsend.c
    ${CMAKE_SOURCE_DIR}/src/lib/net/multicast.c
    ${CMAKE_SOURCE_DIR}/src/lib/net/netconnect.c
    ${CMAKE_SOURCE_DIR}/src/lib/signal.c
    ${CMAKE_SOURCE_DIR}/src/lib/log.c)

dvbipitools_add_unit_test(dipifccret_capture
    unit/dipifccret/test_capture.c
    ${CMAKE_SOURCE_DIR}/src/dipifccret/capture.c
    ${CMAKE_SOURCE_DIR}/src/lib/demux/rtp.c
    ${CMAKE_SOURCE_DIR}/src/lib/signal.c)

dvbipitools_add_unit_test(dipifccret_burst
    unit/dipifccret/test_burst.c
    ${CMAKE_SOURCE_DIR}/src/dipifccret/fcc/burst.c
    ${CMAKE_SOURCE_DIR}/src/dipifccret/channel.c
    ${CMAKE_SOURCE_DIR}/src/lib/mux/rtx.c
    ${CMAKE_SOURCE_DIR}/src/lib/demux/rtx.c
    ${CMAKE_SOURCE_DIR}/src/lib/demux/rtp.c
    ${CMAKE_SOURCE_DIR}/src/lib/mux/psi_build.c
    ${CMAKE_SOURCE_DIR}/src/lib/demux/psi.c
    ${CMAKE_SOURCE_DIR}/src/lib/demux/psi_section_asm.c
    ${CMAKE_SOURCE_DIR}/src/lib/demux/tspack.c
    ${CMAKE_SOURCE_DIR}/src/lib/demux/crc32.c
    ${CMAKE_SOURCE_DIR}/src/lib/log.c)

function(dvbipitools_add_integration_test tool script_path)
    get_filename_component(case_name ${script_path} NAME_WE)
    add_test(NAME integration_${tool}_${case_name} COMMAND sh ${script_path} $<TARGET_FILE:${tool}>)
endfunction()

file(GLOB DIPIBIM_INTEGRATION_SCRIPTS "${CMAKE_CURRENT_SOURCE_DIR}/integration/dipibim/*.sh")
foreach(script ${DIPIBIM_INTEGRATION_SCRIPTS})
    dvbipitools_add_integration_test(dipibim ${script})
endforeach()

file(GLOB DIPIXMLTV_INTEGRATION_SCRIPTS "${CMAKE_CURRENT_SOURCE_DIR}/integration/dipixmltv/*.sh")
foreach(script ${DIPIXMLTV_INTEGRATION_SCRIPTS})
    dvbipitools_add_integration_test(dipixmltv ${script})
endforeach()

file(GLOB DIPITVHEAD_INTEGRATION_SCRIPTS "${CMAKE_CURRENT_SOURCE_DIR}/integration/dipitvhead/*.sh")
foreach(script ${DIPITVHEAD_INTEGRATION_SCRIPTS})
    dvbipitools_add_integration_test(dipitvhead ${script})
endforeach()

file(GLOB DIPIRADIOHEAD_INTEGRATION_SCRIPTS "${CMAKE_CURRENT_SOURCE_DIR}/integration/dipiradiohead/*.sh")
foreach(script ${DIPIRADIOHEAD_INTEGRATION_SCRIPTS})
    dvbipitools_add_integration_test(dipiradiohead ${script})
endforeach()

file(GLOB DIPIREC_INTEGRATION_SCRIPTS "${CMAKE_CURRENT_SOURCE_DIR}/integration/dipirec/*.sh")
foreach(script ${DIPIREC_INTEGRATION_SCRIPTS})
    dvbipitools_add_integration_test(dipirec ${script})
endforeach()

file(GLOB DIPIDESCRAMBLE_INTEGRATION_SCRIPTS "${CMAKE_CURRENT_SOURCE_DIR}/integration/dipidescramble/*.sh")
foreach(script ${DIPIDESCRAMBLE_INTEGRATION_SCRIPTS})
    dvbipitools_add_integration_test(dipidescramble ${script})
endforeach()

file(GLOB DIPIMETRICS_INTEGRATION_SCRIPTS "${CMAKE_CURRENT_SOURCE_DIR}/integration/dipimetrics/*.sh")
foreach(script ${DIPIMETRICS_INTEGRATION_SCRIPTS})
    dvbipitools_add_integration_test(dipimetrics ${script})
endforeach()

dvbipitools_add_unit_test(dipifccret_ret
    unit/dipifccret/test_ret.c
    ${CMAKE_SOURCE_DIR}/src/dipifccret/ret/ret.c
    ${CMAKE_SOURCE_DIR}/src/dipifccret/channel.c
    ${CMAKE_SOURCE_DIR}/src/lib/demux/rtcp.c
    ${CMAKE_SOURCE_DIR}/src/lib/mux/rtcp_build.c
    ${CMAKE_SOURCE_DIR}/src/lib/mux/rtx.c
    ${CMAKE_SOURCE_DIR}/src/lib/demux/rtx.c
    ${CMAKE_SOURCE_DIR}/src/lib/demux/rtp.c
    ${CMAKE_SOURCE_DIR}/src/lib/mux/psi_build.c
    ${CMAKE_SOURCE_DIR}/src/lib/demux/psi.c
    ${CMAKE_SOURCE_DIR}/src/lib/demux/psi_section_asm.c
    ${CMAKE_SOURCE_DIR}/src/lib/demux/tspack.c
    ${CMAKE_SOURCE_DIR}/src/lib/demux/crc32.c
    ${CMAKE_SOURCE_DIR}/src/lib/log.c)

dvbipitools_add_unit_test(lib_metrics_protocol
    unit/lib/metrics/test_protocol.c
    ${CMAKE_SOURCE_DIR}/src/lib/metrics/protocol.c
    ${CMAKE_SOURCE_DIR}/src/lib/log.c
    ${CMAKE_SOURCE_DIR}/src/lib/ioutil.c)

dvbipitools_add_unit_test(lib_metrics_export
    unit/lib/metrics/test_export.c
    ${CMAKE_SOURCE_DIR}/src/lib/metrics/export.c
    ${CMAKE_SOURCE_DIR}/src/lib/metrics/protocol.c
    ${CMAKE_SOURCE_DIR}/src/lib/net/netconnect.c
    ${CMAKE_SOURCE_DIR}/src/lib/signal.c
    ${CMAKE_SOURCE_DIR}/src/lib/log.c
    ${CMAKE_SOURCE_DIR}/src/lib/ioutil.c)

dvbipitools_add_unit_test(dipimetrics_store
    unit/dipimetrics/test_store.c
    ${CMAKE_SOURCE_DIR}/src/dipimetrics/store.c
    ${CMAKE_SOURCE_DIR}/src/lib/metrics/protocol.c
    ${CMAKE_SOURCE_DIR}/src/lib/log.c
    ${CMAKE_SOURCE_DIR}/src/lib/ioutil.c)

dvbipitools_add_unit_test(dipimetrics_render
    unit/dipimetrics/test_render.c
    ${CMAKE_SOURCE_DIR}/src/dipimetrics/render.c
    ${CMAKE_SOURCE_DIR}/src/dipimetrics/store.c
    ${CMAKE_SOURCE_DIR}/src/lib/metrics/protocol.c
    ${CMAKE_SOURCE_DIR}/src/lib/log.c
    ${CMAKE_SOURCE_DIR}/src/lib/ioutil.c)

dvbipitools_add_unit_test(dipimetrics_httpserver
    unit/dipimetrics/test_httpserver.c
    ${CMAKE_SOURCE_DIR}/src/dipimetrics/httpserver.c
    ${CMAKE_SOURCE_DIR}/src/dipimetrics/render.c
    ${CMAKE_SOURCE_DIR}/src/dipimetrics/store.c
    ${CMAKE_SOURCE_DIR}/src/lib/metrics/protocol.c
    ${CMAKE_SOURCE_DIR}/src/lib/signal.c
    ${CMAKE_SOURCE_DIR}/src/lib/log.c
    ${CMAKE_SOURCE_DIR}/src/lib/ioutil.c)
