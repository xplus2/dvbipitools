# Copyright 2026 dvbipitools authors. Licensed under GPL-3.0-or-later.
# See NOTICE and LICENSE for details and authorship information.

function(dvbipitools_add_fuzz_harness name)
    add_executable(fuzz_${name} ${ARGN})
endfunction()

dvbipitools_add_fuzz_harness(psi
    fuzz_psi.c
    ${CMAKE_SOURCE_DIR}/src/lib/demux/psi.c
    ${CMAKE_SOURCE_DIR}/src/lib/demux/crc32.c)

set(FUZZ_BIM_DEPS
    ${CMAKE_SOURCE_DIR}/src/lib/bim/fragment.c
    ${CMAKE_SOURCE_DIR}/src/lib/bim/codec.c
    ${CMAKE_SOURCE_DIR}/src/lib/bim/bitwriter.c
    ${CMAKE_SOURCE_DIR}/src/lib/bim/bitreader.c
    ${CMAKE_SOURCE_DIR}/src/lib/bim/strrepo.c
    ${CMAKE_SOURCE_DIR}/src/lib/tva/tva_xml.c
    ${CMAKE_SOURCE_DIR}/src/lib/tva/epg_doc.c
    ${CMAKE_SOURCE_DIR}/src/lib/xml_util.c
    ${CMAKE_SOURCE_DIR}/src/lib/ioutil.c)

dvbipitools_add_fuzz_harness(bim_accessunit
    fuzz_bim_accessunit.c
    ${CMAKE_SOURCE_DIR}/src/lib/bim/accessunit.c
    ${FUZZ_BIM_DEPS})

dvbipitools_add_fuzz_harness(sds_xml
    fuzz_sds_xml.c
    ${CMAKE_SOURCE_DIR}/src/lib/sds_xml.c
    ${CMAKE_SOURCE_DIR}/src/lib/xml_util.c)

dvbipitools_add_fuzz_harness(rtcp
    fuzz_rtcp.c
    ${CMAKE_SOURCE_DIR}/src/lib/demux/rtcp.c)

add_executable(fuzz_gen_seeds
    gen_seeds.c
    ${CMAKE_SOURCE_DIR}/src/lib/bim/accessunit.c
    ${FUZZ_BIM_DEPS}
    ${CMAKE_SOURCE_DIR}/src/lib/mux/psi_build.c
    ${CMAKE_SOURCE_DIR}/src/lib/mux/rtcp_build.c
    ${CMAKE_SOURCE_DIR}/src/lib/demux/rtcp.c
    ${CMAKE_SOURCE_DIR}/src/lib/demux/crc32.c
    ${CMAKE_SOURCE_DIR}/src/lib/sds_xml.c)
