# Copyright 2026 dvbipitools authors. Licensed under GPL-3.0-or-later.
# See NOTICE and LICENSE for details and authorship information.

function(dvbipitools_add_fuzz_harness name)
    add_executable(fuzz_${name} ${ARGN})
endfunction()

dvbipitools_add_fuzz_harness(psi
    fuzz_psi.c
    ${CMAKE_SOURCE_DIR}/src/lib/demux/psi/psi.c
    ${CMAKE_SOURCE_DIR}/src/lib/demux/psi/parse.c
    ${CMAKE_SOURCE_DIR}/src/lib/demux/psi/descriptors.c
    ${CMAKE_SOURCE_DIR}/src/lib/demux/psi/dvbtext.c
    ${CMAKE_SOURCE_DIR}/src/lib/demux/bitreader.c
    ${CMAKE_SOURCE_DIR}/src/lib/demux/psi/section_asm.c
    ${CMAKE_SOURCE_DIR}/src/lib/demux/tspack.c
    ${CMAKE_SOURCE_DIR}/src/lib/demux/crc32.c
    ${CMAKE_SOURCE_DIR}/src/lib/helper/log.c)

set(FUZZ_BCG_NEW
    ${CMAKE_SOURCE_DIR}/src/lib/bim/container.c
    ${CMAKE_SOURCE_DIR}/src/lib/bim/bcgip.c
    ${CMAKE_SOURCE_DIR}/src/lib/bim/dvbtva.c
    ${CMAKE_SOURCE_DIR}/src/lib/bim/tvafrag.c
    ${CMAKE_SOURCE_DIR}/src/lib/bim/tvainit.c
    ${CMAKE_SOURCE_DIR}/src/lib/bim/decoderinit/read.c
    ${CMAKE_SOURCE_DIR}/src/lib/bim/decoderinit/write.c
    ${CMAKE_SOURCE_DIR}/src/lib/helper/xml_util/tree.c
    ${CMAKE_SOURCE_DIR}/src/lib/helper/xml_util/escape.c
    ${CMAKE_SOURCE_DIR}/src/lib/helper/xml_util/encoding.c
    ${CMAKE_SOURCE_DIR}/src/lib/helper/xml_util/entity.c
    ${CMAKE_SOURCE_DIR}/src/lib/helper/xml_util/scan.c
    ${CMAKE_SOURCE_DIR}/src/lib/helper/xml_util/query.c
    ${CMAKE_SOURCE_DIR}/src/lib/sys/ioutil.c
    ${CMAKE_SOURCE_DIR}/src/lib/helper/log.c
    ${BIM_XSD_SRCS})

set(FUZZ_BCG_ENGINE ${FUZZ_BCG_NEW}
    ${CMAKE_SOURCE_DIR}/src/lib/bim/codec/dvbtypes.c
    ${CMAKE_SOURCE_DIR}/src/lib/bim/codec/tvatypes.c
    ${CMAKE_SOURCE_DIR}/src/lib/bim/codec/node.c
    ${CMAKE_SOURCE_DIR}/src/lib/bim/codec/encode/simple.c
    ${CMAKE_SOURCE_DIR}/src/lib/bim/codec/encode/element.c
    ${CMAKE_SOURCE_DIR}/src/lib/bim/codec/encode/any.c
    ${CMAKE_SOURCE_DIR}/src/lib/bim/codec/encode/lax.c
    ${CMAKE_SOURCE_DIR}/src/lib/bim/codec/encode/aod.c
    ${CMAKE_SOURCE_DIR}/src/lib/bim/codec/encode/zlib.c
    ${CMAKE_SOURCE_DIR}/src/lib/bim/codec/coding.c
    ${CMAKE_SOURCE_DIR}/src/lib/bim/codec/m7time.c
    ${CMAKE_SOURCE_DIR}/src/lib/bim/codec/decode/simple.c
    ${CMAKE_SOURCE_DIR}/src/lib/bim/codec/decode/element.c
    ${CMAKE_SOURCE_DIR}/src/lib/bim/codec/decode/any.c
    ${CMAKE_SOURCE_DIR}/src/lib/bim/codec/decode/lax.c
    ${CMAKE_SOURCE_DIR}/src/lib/bim/codec/laxsym.c
    ${CMAKE_SOURCE_DIR}/src/lib/bim/codec/decode/aod.c
    ${CMAKE_SOURCE_DIR}/src/lib/bim/aod.c
    ${CMAKE_SOURCE_DIR}/src/lib/bim/bimio.c
    ${CMAKE_SOURCE_DIR}/src/lib/helper/base64.c
    ${CMAKE_SOURCE_DIR}/src/lib/bim/bitwriter.c
    ${CMAKE_SOURCE_DIR}/src/lib/bim/bimreader.c
    ${CMAKE_SOURCE_DIR}/src/lib/bim/strrepo.c
    ${CMAKE_SOURCE_DIR}/src/lib/helper/xml_util/escape.c
    ${CMAKE_SOURCE_DIR}/src/lib/helper/xml_util/encoding.c
    ${CMAKE_SOURCE_DIR}/src/lib/helper/xml_util/entity.c
    ${CMAKE_SOURCE_DIR}/src/lib/helper/xml_util/scan.c
    ${CMAKE_SOURCE_DIR}/src/lib/helper/xml_util/query.c
    ${CMAKE_SOURCE_DIR}/src/lib/sys/ioutil.c
    ${CMAKE_SOURCE_DIR}/src/lib/helper/dateutil.c)

dvbipitools_add_fuzz_harness(sds_xml
    fuzz_sds_xml.c
    ${CMAKE_SOURCE_DIR}/src/lib/helper/sds_xml.c
    ${CMAKE_SOURCE_DIR}/src/lib/helper/xml_util/escape.c ${CMAKE_SOURCE_DIR}/src/lib/helper/xml_util/encoding.c ${CMAKE_SOURCE_DIR}/src/lib/helper/xml_util/entity.c ${CMAKE_SOURCE_DIR}/src/lib/helper/xml_util/scan.c ${CMAKE_SOURCE_DIR}/src/lib/helper/xml_util/query.c)

dvbipitools_add_fuzz_harness(rtcp
    fuzz_rtcp.c
    ${CMAKE_SOURCE_DIR}/src/lib/demux/rtcp.c)

dvbipitools_add_fuzz_harness(simulcrypt_msg
    fuzz_simulcrypt_msg.c
    ${CMAKE_SOURCE_DIR}/src/lib/cas/simulcrypt_msg.c
    ${CMAKE_SOURCE_DIR}/src/lib/mux/psi_build.c
    ${CMAKE_SOURCE_DIR}/src/lib/demux/crc32.c
    ${CMAKE_SOURCE_DIR}/src/lib/sys/signal.c)

dvbipitools_add_fuzz_harness(ecmg_channel_status
    fuzz_ecmg_channel_status.c
    ${CMAKE_SOURCE_DIR}/src/lib/sys/ioutil.c
    ${CMAKE_SOURCE_DIR}/src/lib/helper/dateutil.c
    ${CMAKE_SOURCE_DIR}/src/lib/cas/ecmg_client/ecmg_client.c
    ${CMAKE_SOURCE_DIR}/src/lib/cas/ecmg_client/protocol.c
    ${CMAKE_SOURCE_DIR}/src/lib/cas/ecmg_client/connect.c
    ${CMAKE_SOURCE_DIR}/src/lib/cas/ecmg_client/run.c
    ${CMAKE_SOURCE_DIR}/src/lib/cas/ecmg_client/cw_encryption_stub.c
    ${CMAKE_SOURCE_DIR}/src/lib/cas/ecmg_client/cw_encryption_common.c
    ${CMAKE_SOURCE_DIR}/src/lib/cas/cas_dial.c
    ${CMAKE_SOURCE_DIR}/src/lib/cas/simulcrypt_msg.c
    ${CMAKE_SOURCE_DIR}/src/lib/mux/psi_build.c
    ${CMAKE_SOURCE_DIR}/src/lib/demux/crc32.c
    ${CMAKE_SOURCE_DIR}/src/lib/scrambler/scrambler.c
    ${CMAKE_SOURCE_DIR}/src/lib/scrambler/cissa_stub.c
    ${CMAKE_SOURCE_DIR}/src/lib/scrambler/csa2_stub.c
    ${CMAKE_SOURCE_DIR}/src/lib/helper/log.c
    ${CMAKE_SOURCE_DIR}/src/lib/helper/secure_zero.c
    ${CMAKE_SOURCE_DIR}/src/lib/sys/signal.c)

dvbipitools_add_fuzz_harness(emmg_datagrams
    fuzz_emmg_datagrams.c
    ${CMAKE_SOURCE_DIR}/src/lib/cas/emmg_server/emmg_server.c
    ${CMAKE_SOURCE_DIR}/src/lib/cas/emmg_server/protocol.c
    ${CMAKE_SOURCE_DIR}/src/lib/cas/emmg_server/worker.c
    ${CMAKE_SOURCE_DIR}/src/lib/cas/emmg_server/dial.c
    ${CMAKE_SOURCE_DIR}/src/lib/cas/cas_dial.c
    ${CMAKE_SOURCE_DIR}/src/lib/cas/simulcrypt_msg.c
    ${CMAKE_SOURCE_DIR}/src/lib/mux/psi_build.c
    ${CMAKE_SOURCE_DIR}/src/lib/demux/crc32.c
    ${CMAKE_SOURCE_DIR}/src/lib/helper/log.c
    ${CMAKE_SOURCE_DIR}/src/lib/sys/signal.c
    ${CMAKE_SOURCE_DIR}/src/lib/helper/dateutil.c
    ${CMAKE_SOURCE_DIR}/src/lib/sys/ioutil.c)

dvbipitools_add_fuzz_harness(dvbstp
    fuzz_dvbstp.c
    ${CMAKE_SOURCE_DIR}/src/lib/net/dvbstp.c
    ${CMAKE_SOURCE_DIR}/src/lib/net/multicast.c
    ${CMAKE_SOURCE_DIR}/src/lib/net/netconnect.c
    ${CMAKE_SOURCE_DIR}/src/lib/helper/argutil.c
    ${CMAKE_SOURCE_DIR}/src/lib/sys/ioutil.c
    ${CMAKE_SOURCE_DIR}/src/lib/demux/crc32.c
    ${CMAKE_SOURCE_DIR}/src/lib/sys/signal.c
    ${CMAKE_SOURCE_DIR}/src/lib/helper/log.c)

dvbipitools_add_fuzz_harness(ws_frame
    fuzz_ws_frame.c
    ${CMAKE_SOURCE_DIR}/src/dipixy/ws/ws_frame.c)

dvbipitools_add_fuzz_harness(route
    fuzz_route.c
    ${CMAKE_SOURCE_DIR}/src/dipixy/core/route.c
    ${CMAKE_SOURCE_DIR}/src/lib/helper/argutil.c
    ${CMAKE_SOURCE_DIR}/src/lib/sys/ioutil.c
    ${CMAKE_SOURCE_DIR}/src/lib/helper/uriparse.c)

dvbipitools_add_fuzz_harness(ssdp
    fuzz_ssdp.c
    ${CMAKE_SOURCE_DIR}/src/dipixy/dlna/ssdp.c
    ${CMAKE_SOURCE_DIR}/src/lib/net/multicast.c
    ${CMAKE_SOURCE_DIR}/src/lib/net/netconnect.c
    ${CMAKE_SOURCE_DIR}/src/lib/helper/argutil.c
    ${CMAKE_SOURCE_DIR}/src/lib/sys/ioutil.c
    ${CMAKE_SOURCE_DIR}/src/lib/sys/signal.c
    ${CMAKE_SOURCE_DIR}/src/lib/helper/log.c)

dvbipitools_add_fuzz_harness(yamlcfg
    fuzz_yamlcfg.c
    ${CMAKE_SOURCE_DIR}/src/lib/config/yamlcfg.c
    ${CMAKE_SOURCE_DIR}/src/lib/vendor/libyaml/api.c
    ${CMAKE_SOURCE_DIR}/src/lib/vendor/libyaml/reader.c
    ${CMAKE_SOURCE_DIR}/src/lib/vendor/libyaml/scanner.c
    ${CMAKE_SOURCE_DIR}/src/lib/vendor/libyaml/parser.c
    ${CMAKE_SOURCE_DIR}/src/lib/helper/argutil.c
    ${CMAKE_SOURCE_DIR}/src/lib/sys/ioutil.c
    ${CMAKE_SOURCE_DIR}/src/lib/helper/log.c)

dvbipitools_add_fuzz_harness(tva_xml
    fuzz_tva_xml.c
    ${CMAKE_SOURCE_DIR}/src/lib/tva/tva_xml/crid.c
    ${CMAKE_SOURCE_DIR}/src/lib/tva/tva_xml/time.c
    ${CMAKE_SOURCE_DIR}/src/lib/tva/tva_xml/write.c
    ${CMAKE_SOURCE_DIR}/src/lib/tva/tva_xml/read_info.c
    ${CMAKE_SOURCE_DIR}/src/lib/tva/tva_xml/read_schedule.c
    ${CMAKE_SOURCE_DIR}/src/lib/tva/bcg_doc.c
    ${CMAKE_SOURCE_DIR}/src/lib/helper/xml_util/escape.c
    ${CMAKE_SOURCE_DIR}/src/lib/helper/xml_util/encoding.c
    ${CMAKE_SOURCE_DIR}/src/lib/helper/xml_util/entity.c
    ${CMAKE_SOURCE_DIR}/src/lib/helper/xml_util/scan.c
    ${CMAKE_SOURCE_DIR}/src/lib/helper/xml_util/query.c
    ${CMAKE_SOURCE_DIR}/src/lib/sys/ioutil.c
    ${CMAKE_SOURCE_DIR}/src/lib/helper/dateutil.c
    ${CMAKE_SOURCE_DIR}/src/lib/helper/uriparse.c
    ${CMAKE_SOURCE_DIR}/src/lib/helper/argutil.c
    ${CMAKE_SOURCE_DIR}/src/lib/helper/log.c)

dvbipitools_add_fuzz_harness(xmltv
    fuzz_xmltv.c
    ${CMAKE_SOURCE_DIR}/src/lib/tva/xmltv.c
    ${CMAKE_SOURCE_DIR}/src/lib/tva/mapping.c
    ${CMAKE_SOURCE_DIR}/src/lib/tva/bcg_doc.c
    ${CMAKE_SOURCE_DIR}/src/lib/helper/uriparse.c
    ${CMAKE_SOURCE_DIR}/src/lib/tva/timefmt.c
    ${CMAKE_SOURCE_DIR}/src/lib/helper/argutil.c
    ${CMAKE_SOURCE_DIR}/src/lib/helper/xml_util/escape.c
    ${CMAKE_SOURCE_DIR}/src/lib/helper/xml_util/encoding.c
    ${CMAKE_SOURCE_DIR}/src/lib/helper/xml_util/entity.c
    ${CMAKE_SOURCE_DIR}/src/lib/helper/xml_util/scan.c
    ${CMAKE_SOURCE_DIR}/src/lib/helper/xml_util/query.c
    ${CMAKE_SOURCE_DIR}/src/lib/sys/ioutil.c
    ${CMAKE_SOURCE_DIR}/src/lib/helper/dateutil.c
    ${CMAKE_SOURCE_DIR}/src/lib/helper/log.c)

find_package(ZLIB)
dvbipitools_add_fuzz_harness(bcg_container
    fuzz_bcg_container.c
    ${FUZZ_BCG_ENGINE})
if (ZLIB_FOUND)
    target_link_libraries(fuzz_bcg_container PRIVATE ZLIB::ZLIB)
    target_compile_definitions(fuzz_bcg_container PRIVATE FUZZ_BCG_ZLIB)
endif ()

dvbipitools_add_fuzz_harness(metrics_store
    fuzz_metrics_store.c
    ${CMAKE_SOURCE_DIR}/src/lib/metrics/store.c
    ${CMAKE_SOURCE_DIR}/src/lib/metrics/protocol.c
    ${CMAKE_SOURCE_DIR}/src/lib/sys/ioutil.c
    ${CMAKE_SOURCE_DIR}/src/lib/helper/log.c)

dvbipitools_add_fuzz_harness(rtmp
    fuzz_rtmp.c
    ${CMAKE_SOURCE_DIR}/src/lib/net/rtmp/rtmp.c
    ${CMAKE_SOURCE_DIR}/src/lib/net/rtmp/session.c
    ${CMAKE_SOURCE_DIR}/src/lib/net/rtmp/command.c
    ${CMAKE_SOURCE_DIR}/src/lib/net/rtmp/chunk.c
    ${CMAKE_SOURCE_DIR}/src/lib/net/rtmp/handshake.c
    ${CMAKE_SOURCE_DIR}/src/lib/net/rtmp/auth_stub.c
    ${CMAKE_SOURCE_DIR}/src/lib/mux/amf.c
    ${CMAKE_SOURCE_DIR}/src/lib/mux/ebml.c
    ${CMAKE_SOURCE_DIR}/src/lib/mux/growbuf.c
    ${CMAKE_SOURCE_DIR}/src/lib/sys/ioutil.c
    ${CMAKE_SOURCE_DIR}/src/lib/helper/uriparse.c
    ${CMAKE_SOURCE_DIR}/src/lib/helper/argutil.c
    ${CMAKE_SOURCE_DIR}/src/lib/helper/log.c)

# ecmg_client.c/emmg_server.c/ssdp.c pull in real pthread usage even though fuzzed
# functions themselves are pure. link Threads on all like dipitvhead itself does
find_package(Threads REQUIRED)
target_link_libraries(fuzz_ecmg_channel_status PRIVATE Threads::Threads)
target_link_libraries(fuzz_emmg_datagrams PRIVATE Threads::Threads)
target_link_libraries(fuzz_ssdp PRIVATE Threads::Threads)

add_executable(fuzz_gen_seeds
    gen_seeds.c
    ${CMAKE_SOURCE_DIR}/src/lib/tva/tva_xml/crid.c
    ${CMAKE_SOURCE_DIR}/src/lib/tva/tva_xml/time.c
    ${CMAKE_SOURCE_DIR}/src/lib/tva/tva_xml/write.c
    ${CMAKE_SOURCE_DIR}/src/lib/tva/tva_xml/read_info.c
    ${CMAKE_SOURCE_DIR}/src/lib/tva/tva_xml/read_schedule.c
    ${CMAKE_SOURCE_DIR}/src/lib/tva/bcg_doc.c
    ${CMAKE_SOURCE_DIR}/src/lib/helper/uriparse.c
    ${CMAKE_SOURCE_DIR}/src/lib/mux/psi_build.c
    ${CMAKE_SOURCE_DIR}/src/lib/mux/rtcp_build.c
    ${CMAKE_SOURCE_DIR}/src/lib/demux/rtcp.c
    ${CMAKE_SOURCE_DIR}/src/lib/demux/crc32.c
    ${CMAKE_SOURCE_DIR}/src/lib/helper/sds_xml.c
    ${CMAKE_SOURCE_DIR}/src/lib/cas/simulcrypt_msg.c
    ${CMAKE_SOURCE_DIR}/src/lib/net/dvbstp.c
    ${CMAKE_SOURCE_DIR}/src/lib/net/multicast.c
    ${CMAKE_SOURCE_DIR}/src/lib/net/netconnect.c
    ${CMAKE_SOURCE_DIR}/src/lib/helper/argutil.c
    ${CMAKE_SOURCE_DIR}/src/lib/sys/ioutil.c
    ${CMAKE_SOURCE_DIR}/src/lib/helper/dateutil.c
    ${CMAKE_SOURCE_DIR}/src/lib/sys/signal.c
    ${CMAKE_SOURCE_DIR}/src/lib/metrics/protocol.c
    ${FUZZ_BCG_ENGINE})

dvbipitools_add_fuzz_harness(pesstamp
    fuzz_pesstamp.c
    ${CMAKE_SOURCE_DIR}/src/dipitvhead/mux/pesstamp.c)

dvbipitools_add_fuzz_harness(scte35stamp
    fuzz_scte35stamp.c
    ${CMAKE_SOURCE_DIR}/src/dipitvhead/mux/scte35stamp.c
    ${CMAKE_SOURCE_DIR}/src/lib/demux/crc32.c)
