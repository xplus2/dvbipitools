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
    ${CMAKE_SOURCE_DIR}/src/lib/demux/crc32.c)

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
    ${CMAKE_SOURCE_DIR}/src/lib/demux/pes.c)

dvbipitools_add_unit_test(lib_mux_psi_build
    unit/lib/mux/test_psi_build.c
    ${CMAKE_SOURCE_DIR}/src/lib/mux/psi_build.c
    ${CMAKE_SOURCE_DIR}/src/lib/demux/psi.c
    ${CMAKE_SOURCE_DIR}/src/lib/demux/crc32.c)

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

dvbipitools_add_unit_test(lib_tva_epg_doc
    unit/lib/tva/test_epg_doc.c
    ${CMAKE_SOURCE_DIR}/src/lib/tva/epg_doc.c)

dvbipitools_add_unit_test(lib_tva_mapping
    unit/lib/tva/test_mapping.c
    ${CMAKE_SOURCE_DIR}/src/lib/tva/mapping.c)

dvbipitools_add_unit_test(lib_tva_xmltv
    unit/lib/tva/test_xmltv.c
    ${CMAKE_SOURCE_DIR}/src/lib/tva/xmltv.c
    ${CMAKE_SOURCE_DIR}/src/lib/tva/timefmt.c
    ${CMAKE_SOURCE_DIR}/src/lib/tva/epg_doc.c
    ${CMAKE_SOURCE_DIR}/src/lib/xml_util.c
    ${CMAKE_SOURCE_DIR}/src/lib/ioutil.c)

dvbipitools_add_unit_test(lib_tva_tva_xml
    unit/lib/tva/test_tva_xml.c
    ${CMAKE_SOURCE_DIR}/src/lib/tva/tva_xml.c
    ${CMAKE_SOURCE_DIR}/src/lib/tva/epg_doc.c
    ${CMAKE_SOURCE_DIR}/src/lib/xml_util.c
    ${CMAKE_SOURCE_DIR}/src/lib/ioutil.c)

set(BIM_FRAGMENT_DEPS
    ${CMAKE_SOURCE_DIR}/src/lib/bim/fragment.c
    ${CMAKE_SOURCE_DIR}/src/lib/bim/codec.c
    ${CMAKE_SOURCE_DIR}/src/lib/bim/bitwriter.c
    ${CMAKE_SOURCE_DIR}/src/lib/bim/bitreader.c
    ${CMAKE_SOURCE_DIR}/src/lib/bim/strrepo.c
    ${CMAKE_SOURCE_DIR}/src/lib/tva/tva_xml.c
    ${CMAKE_SOURCE_DIR}/src/lib/tva/epg_doc.c
    ${CMAKE_SOURCE_DIR}/src/lib/xml_util.c
    ${CMAKE_SOURCE_DIR}/src/lib/ioutil.c)

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
    ${CMAKE_SOURCE_DIR}/src/lib/sds_xml.c
    ${CMAKE_SOURCE_DIR}/src/lib/xml_util.c)

dvbipitools_add_unit_test(dipixmltv_revmap
    unit/dipixmltv/test_revmap.c
    ${CMAKE_SOURCE_DIR}/src/dipixmltv/revmap.c)

dvbipitools_add_unit_test(dipixmltv_suggest
    unit/dipixmltv/test_suggest.c
    ${CMAKE_SOURCE_DIR}/src/dipixmltv/suggest.c
    ${CMAKE_SOURCE_DIR}/src/lib/tva/xmltv.c
    ${CMAKE_SOURCE_DIR}/src/lib/tva/timefmt.c
    ${CMAKE_SOURCE_DIR}/src/lib/tva/epg_doc.c
    ${CMAKE_SOURCE_DIR}/src/lib/xml_util.c
    ${CMAKE_SOURCE_DIR}/src/lib/ioutil.c)

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
    ${CMAKE_SOURCE_DIR}/src/lib/demux/crc32.c)

dvbipitools_add_unit_test(dipiradiohead_pes
    unit/dipiradiohead/test_pes.c
    ${CMAKE_SOURCE_DIR}/src/dipiradiohead/mux/pes.c
    ${CMAKE_SOURCE_DIR}/src/lib/demux/pes.c)

dvbipitools_add_unit_test(dipiradiohead_tspacketizer
    unit/dipiradiohead/test_tspacketizer.c
    ${CMAKE_SOURCE_DIR}/src/dipiradiohead/mux/tspacketizer.c
    ${CMAKE_SOURCE_DIR}/src/dipiradiohead/mux/psi.c
    ${CMAKE_SOURCE_DIR}/src/dipiradiohead/mux/pes.c
    ${CMAKE_SOURCE_DIR}/src/lib/mux/psi_build.c
    ${CMAKE_SOURCE_DIR}/src/lib/mux/tspacket_write.c
    ${CMAKE_SOURCE_DIR}/src/lib/demux/crc32.c)

dvbipitools_add_unit_test(dipitvhead_pmtbuild
    unit/dipitvhead/test_pmtbuild.c
    ${CMAKE_SOURCE_DIR}/src/dipitvhead/mux/pmtbuild.c
    ${CMAKE_SOURCE_DIR}/src/lib/mux/psi_build.c
    ${CMAKE_SOURCE_DIR}/src/lib/demux/psi.c
    ${CMAKE_SOURCE_DIR}/src/lib/demux/crc32.c)

dvbipitools_add_unit_test(dipitvhead_aitbuild
    unit/dipitvhead/test_aitbuild.c
    ${CMAKE_SOURCE_DIR}/src/dipitvhead/mux/aitbuild.c
    ${CMAKE_SOURCE_DIR}/src/lib/mux/psi_build.c
    ${CMAKE_SOURCE_DIR}/src/lib/demux/crc32.c)

dvbipitools_add_unit_test(dipitvhead_bitrate
    unit/dipitvhead/test_bitrate.c
    ${CMAKE_SOURCE_DIR}/src/dipitvhead/mux/bitrate.c)

dvbipitools_add_unit_test(dipitvhead_remux
    unit/dipitvhead/test_remux.c
    ${CMAKE_SOURCE_DIR}/src/dipitvhead/mux/remux.c
    ${CMAKE_SOURCE_DIR}/src/dipitvhead/mux/pmtbuild.c
    ${CMAKE_SOURCE_DIR}/src/dipitvhead/mux/aitbuild.c
    ${CMAKE_SOURCE_DIR}/src/lib/mux/psi_build.c
    ${CMAKE_SOURCE_DIR}/src/lib/mux/tspacket_write.c
    ${CMAKE_SOURCE_DIR}/src/lib/demux/psi.c
    ${CMAKE_SOURCE_DIR}/src/lib/demux/crc32.c
    ${CMAKE_SOURCE_DIR}/src/lib/log.c)

dvbipitools_add_unit_test(dipirec_ebml
    unit/dipirec/test_ebml.c
    ${CMAKE_SOURCE_DIR}/src/dipirec/mux/ebml.c)

dvbipitools_add_unit_test(dipirec_ts_filter
    unit/dipirec/test_ts_filter.c
    ${CMAKE_SOURCE_DIR}/src/dipirec/filter/ts.c
    ${CMAKE_SOURCE_DIR}/src/lib/demux/psi.c
    ${CMAKE_SOURCE_DIR}/src/lib/demux/crc32.c)

dvbipitools_add_unit_test(dipirec_teletext
    unit/dipirec/test_teletext.c
    ${CMAKE_SOURCE_DIR}/src/dipirec/mux/teletext.c)

dvbipitools_add_unit_test(dipirec_mkv
    unit/dipirec/test_mkv.c
    ${CMAKE_SOURCE_DIR}/src/dipirec/mux/mkv.c
    ${CMAKE_SOURCE_DIR}/src/dipirec/mux/ebml.c
    ${CMAKE_SOURCE_DIR}/src/dipirec/mux/teletext.c
    ${CMAKE_SOURCE_DIR}/src/dipirec/args.c
    ${CMAKE_SOURCE_DIR}/src/lib/mux/psi_build.c
    ${CMAKE_SOURCE_DIR}/src/lib/demux/pes.c
    ${CMAKE_SOURCE_DIR}/src/lib/demux/psi.c
    ${CMAKE_SOURCE_DIR}/src/lib/demux/crc32.c
    ${CMAKE_SOURCE_DIR}/src/lib/log.c)

dvbipitools_add_unit_test(dipifccret_channel
    unit/dipifccret/test_channel.c
    ${CMAKE_SOURCE_DIR}/src/dipifccret/channel.c
    ${CMAKE_SOURCE_DIR}/src/lib/mux/psi_build.c
    ${CMAKE_SOURCE_DIR}/src/lib/demux/psi.c
    ${CMAKE_SOURCE_DIR}/src/lib/demux/crc32.c
    ${CMAKE_SOURCE_DIR}/src/lib/log.c)

dvbipitools_add_unit_test(dipifccret_ret_mcsend
    unit/dipifccret/ret/test_mcsend.c
    ${CMAKE_SOURCE_DIR}/src/dipifccret/ret/mcsend.c
    ${CMAKE_SOURCE_DIR}/src/lib/net/multicast.c
    ${CMAKE_SOURCE_DIR}/src/lib/log.c)

dvbipitools_add_unit_test(dipifccret_burst
    unit/dipifccret/test_burst.c
    ${CMAKE_SOURCE_DIR}/src/dipifccret/fcc/burst.c
    ${CMAKE_SOURCE_DIR}/src/dipifccret/channel.c
    ${CMAKE_SOURCE_DIR}/src/lib/mux/rtx.c
    ${CMAKE_SOURCE_DIR}/src/lib/demux/rtx.c
    ${CMAKE_SOURCE_DIR}/src/lib/demux/rtp.c
    ${CMAKE_SOURCE_DIR}/src/lib/mux/psi_build.c
    ${CMAKE_SOURCE_DIR}/src/lib/demux/psi.c
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
    ${CMAKE_SOURCE_DIR}/src/lib/demux/crc32.c
    ${CMAKE_SOURCE_DIR}/src/lib/log.c)
