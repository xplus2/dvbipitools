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
    ${CMAKE_SOURCE_DIR}/src/lib/demux/psi/section_asm.c
    ${CMAKE_SOURCE_DIR}/src/lib/demux/tspack.c
    ${CMAKE_SOURCE_DIR}/src/lib/demux/crc32.c
    ${CMAKE_SOURCE_DIR}/src/lib/helper/log.c)

set(FUZZ_BIM_DEPS
    ${CMAKE_SOURCE_DIR}/src/lib/bim/fragment.c
    ${CMAKE_SOURCE_DIR}/src/lib/bim/codec.c
    ${CMAKE_SOURCE_DIR}/src/lib/bim/bitwriter.c
    ${CMAKE_SOURCE_DIR}/src/lib/bim/bitreader.c
    ${CMAKE_SOURCE_DIR}/src/lib/bim/strrepo.c
    ${CMAKE_SOURCE_DIR}/src/lib/tva/tva_xml.c
    ${CMAKE_SOURCE_DIR}/src/lib/tva/bcg_doc.c
    ${CMAKE_SOURCE_DIR}/src/lib/helper/xml_util.c
    ${CMAKE_SOURCE_DIR}/src/lib/helper/ioutil.c
    ${CMAKE_SOURCE_DIR}/src/lib/helper/log.c)

dvbipitools_add_fuzz_harness(bim_accessunit
    fuzz_bim_accessunit.c
    ${CMAKE_SOURCE_DIR}/src/lib/bim/accessunit.c
    ${FUZZ_BIM_DEPS})

dvbipitools_add_fuzz_harness(sds_xml
    fuzz_sds_xml.c
    ${CMAKE_SOURCE_DIR}/src/lib/helper/sds_xml.c
    ${CMAKE_SOURCE_DIR}/src/lib/helper/xml_util.c)

dvbipitools_add_fuzz_harness(rtcp
    fuzz_rtcp.c
    ${CMAKE_SOURCE_DIR}/src/lib/demux/rtcp.c)

dvbipitools_add_fuzz_harness(simulcrypt_msg
    fuzz_simulcrypt_msg.c
    ${CMAKE_SOURCE_DIR}/src/lib/cas/simulcrypt_msg.c
    ${CMAKE_SOURCE_DIR}/src/lib/mux/psi_build.c
    ${CMAKE_SOURCE_DIR}/src/lib/demux/crc32.c
    ${CMAKE_SOURCE_DIR}/src/lib/helper/signal.c)

dvbipitools_add_fuzz_harness(ecmg_channel_status
    fuzz_ecmg_channel_status.c
    ${CMAKE_SOURCE_DIR}/src/lib/helper/ioutil.c
    ${CMAKE_SOURCE_DIR}/src/lib/cas/ecmg_client/ecmg_client.c
    ${CMAKE_SOURCE_DIR}/src/lib/cas/ecmg_client/protocol.c
    ${CMAKE_SOURCE_DIR}/src/lib/cas/ecmg_client/connect.c
    ${CMAKE_SOURCE_DIR}/src/lib/cas/ecmg_client/run.c
    ${CMAKE_SOURCE_DIR}/src/lib/cas/simulcrypt_msg.c
    ${CMAKE_SOURCE_DIR}/src/lib/mux/psi_build.c
    ${CMAKE_SOURCE_DIR}/src/lib/demux/crc32.c
    ${CMAKE_SOURCE_DIR}/src/lib/scrambler/scrambler.c
    ${CMAKE_SOURCE_DIR}/src/lib/scrambler/cissa_stub.c
    ${CMAKE_SOURCE_DIR}/src/lib/scrambler/csa2_stub.c
    ${CMAKE_SOURCE_DIR}/src/lib/helper/log.c
    ${CMAKE_SOURCE_DIR}/src/lib/helper/signal.c)

dvbipitools_add_fuzz_harness(emmg_datagrams
    fuzz_emmg_datagrams.c
    ${CMAKE_SOURCE_DIR}/src/lib/cas/emmg_server/emmg_server.c
    ${CMAKE_SOURCE_DIR}/src/lib/cas/emmg_server/protocol.c
    ${CMAKE_SOURCE_DIR}/src/lib/cas/emmg_server/worker.c
    ${CMAKE_SOURCE_DIR}/src/lib/cas/simulcrypt_msg.c
    ${CMAKE_SOURCE_DIR}/src/lib/mux/psi_build.c
    ${CMAKE_SOURCE_DIR}/src/lib/demux/crc32.c
    ${CMAKE_SOURCE_DIR}/src/lib/helper/log.c
    ${CMAKE_SOURCE_DIR}/src/lib/helper/signal.c)

dvbipitools_add_fuzz_harness(dvbstp
    fuzz_dvbstp.c
    ${CMAKE_SOURCE_DIR}/src/lib/net/dvbstp.c
    ${CMAKE_SOURCE_DIR}/src/lib/net/multicast.c
    ${CMAKE_SOURCE_DIR}/src/lib/net/netconnect.c
    ${CMAKE_SOURCE_DIR}/src/lib/helper/ioutil.c
    ${CMAKE_SOURCE_DIR}/src/lib/demux/crc32.c
    ${CMAKE_SOURCE_DIR}/src/lib/helper/signal.c
    ${CMAKE_SOURCE_DIR}/src/lib/helper/log.c)

dvbipitools_add_fuzz_harness(ws_frame
    fuzz_ws_frame.c
    ${CMAKE_SOURCE_DIR}/src/dipixy/ws/ws_frame.c)

dvbipitools_add_fuzz_harness(route
    fuzz_route.c
    ${CMAKE_SOURCE_DIR}/src/dipixy/core/route.c
    ${CMAKE_SOURCE_DIR}/src/lib/helper/argutil.c
    ${CMAKE_SOURCE_DIR}/src/lib/helper/ioutil.c
    ${CMAKE_SOURCE_DIR}/src/lib/helper/uriparse.c)

dvbipitools_add_fuzz_harness(ssdp
    fuzz_ssdp.c
    ${CMAKE_SOURCE_DIR}/src/dipixy/dlna/ssdp.c
    ${CMAKE_SOURCE_DIR}/src/lib/net/multicast.c
    ${CMAKE_SOURCE_DIR}/src/lib/net/netconnect.c
    ${CMAKE_SOURCE_DIR}/src/lib/helper/ioutil.c
    ${CMAKE_SOURCE_DIR}/src/lib/helper/signal.c
    ${CMAKE_SOURCE_DIR}/src/lib/helper/log.c)

# ecmg_client.c/emmg_server.c/ssdp.c pull in real pthread usage even though fuzzed
# functions themselves are pure. link Threads on all like dipitvhead itself does
find_package(Threads REQUIRED)
target_link_libraries(fuzz_ecmg_channel_status PRIVATE Threads::Threads)
target_link_libraries(fuzz_emmg_datagrams PRIVATE Threads::Threads)
target_link_libraries(fuzz_ssdp PRIVATE Threads::Threads)

add_executable(fuzz_gen_seeds
    gen_seeds.c
    ${CMAKE_SOURCE_DIR}/src/lib/bim/accessunit.c
    ${FUZZ_BIM_DEPS}
    ${CMAKE_SOURCE_DIR}/src/lib/mux/psi_build.c
    ${CMAKE_SOURCE_DIR}/src/lib/mux/rtcp_build.c
    ${CMAKE_SOURCE_DIR}/src/lib/demux/rtcp.c
    ${CMAKE_SOURCE_DIR}/src/lib/demux/crc32.c
    ${CMAKE_SOURCE_DIR}/src/lib/helper/sds_xml.c
    ${CMAKE_SOURCE_DIR}/src/lib/cas/simulcrypt_msg.c
    ${CMAKE_SOURCE_DIR}/src/lib/net/dvbstp.c
    ${CMAKE_SOURCE_DIR}/src/lib/net/multicast.c
    ${CMAKE_SOURCE_DIR}/src/lib/net/netconnect.c
    ${CMAKE_SOURCE_DIR}/src/lib/helper/signal.c)
