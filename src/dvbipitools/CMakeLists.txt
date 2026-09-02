# Copyright 2026 dvbipitools authors. Licensed under GPL-3.0-or-later.
# See NOTICE and LICENSE for details and authorship information.

# multicall binary:
# renamed main()/args_parse() from all tools in one executable. main.c dispatcher by argv[0] (or subcommand).
# reuses tool_sources.cmake functions, each tool's own add_executable() untouched.

dipibcg_resolve_sources()
dipibim_resolve_sources()
dipicam378_resolve_sources()
dipidescramble_resolve_sources()
dipifccret_resolve_sources()
dipimetrics_resolve_sources()
dipiradiohead_resolve_sources()
dipirec_resolve_sources()
dipirist_resolve_sources()
dipisrt_resolve_sources()
dipiscan_resolve_sources()
dipisds_resolve_sources()
dipitvhead_resolve_sources()
dipixmltv_resolve_sources()
dipixy_resolve_sources()

# dipixy embeds htdocs/index.html at build time. src/dipixy/CMakeLists.txt
# does this for its own standalone binary; multicall needs its own copy.
set(DVBIPITOOLS_DIPIXY_HTDOCS_GEN ${CMAKE_CURRENT_BINARY_DIR}/dipixy_htdocs_index.gen.c)
add_custom_command(
    OUTPUT ${DVBIPITOOLS_DIPIXY_HTDOCS_GEN}
    COMMAND ${GEN_HTDOCS_EXE} ${CMAKE_SOURCE_DIR}/src/dipixy/htdocs/index.html ${DVBIPITOOLS_DIPIXY_HTDOCS_GEN} g_htdocs_index_html
    DEPENDS ${GEN_HTDOCS_DEP} ${CMAKE_SOURCE_DIR}/src/dipixy/htdocs/index.html
    COMMENT "dvbipitools: embedding dipixy htdocs/index.html")
list(APPEND DIPIXY_SRCS ${DVBIPITOOLS_DIPIXY_HTDOCS_GEN})

function(dvbipitools_rename_main_args tool)
    set_source_files_properties(${CMAKE_SOURCE_DIR}/src/${tool}/main.c PROPERTIES
        COMPILE_DEFINITIONS "main=${tool}_main;args_parse=${tool}_args_parse")
    set_source_files_properties(${CMAKE_SOURCE_DIR}/src/${tool}/args.c PROPERTIES
        COMPILE_DEFINITIONS "args_parse=${tool}_args_parse")
endfunction()

foreach (t dipibcg dipibim dipifccret dipimetrics dipiradiohead dipirec dipiscan dipisds dipitvhead dipixmltv dipixy)
    dvbipitools_rename_main_args(${t})
endforeach ()
if (DIPICAM378_FOUND)
    dvbipitools_rename_main_args(dipicam378)
endif ()
if (DIPIDESCRAMBLE_FOUND)
    dvbipitools_rename_main_args(dipidescramble)
endif ()
if (DIPIRIST_FOUND)
    dvbipitools_rename_main_args(dipirist)
endif ()
if (DIPISRT_FOUND)
    dvbipitools_rename_main_args(dipisrt)
endif ()

# few non-static helpers collide across unrelated tools only once merged here.
# one side renamed, other stays. scoped to src/<tool>/ own files only, no shared src/lib/ files: differing renames of same name
# (mcast_describe: radiohead/sds/tvhead each want own target) would redefine same macro twice on shared files
function(dvbipitools_apply_tool_defs tool srcs_var)
    foreach (f ${${srcs_var}})
        string(FIND "${f}" "${CMAKE_SOURCE_DIR}/src/${tool}/" pos)
        if (pos EQUAL 0)
            set_property(SOURCE ${f} APPEND PROPERTY COMPILE_DEFINITIONS ${ARGN})
        endif ()
    endforeach ()
endfunction()

if (DIPICAM378_FOUND)
    dvbipitools_apply_tool_defs(dipicam378 DIPICAM378_SRCS "accept_main=dipicam378_accept_main")
endif ()
if (DIPIDESCRAMBLE_FOUND)
    dvbipitools_apply_tool_defs(dipidescramble DIPIDESCRAMBLE_SRCS
        "device_on_emm=dipidescramble_device_on_emm"
        "device_resolve_cw=dipidescramble_device_resolve_cw"
        "device_state_free=dipidescramble_device_state_free"
        "device_state_new=dipidescramble_device_state_new"
        "out_describe=dipidescramble_out_describe")
endif ()
# config_is_sender/endpoint_describe/bridge_run: only dipirist/dipisrt define these, collide once merged here
if (DIPIRIST_FOUND)
    dvbipitools_apply_tool_defs(dipirist DIPIRIST_SRCS
        "config_is_sender=dipirist_config_is_sender"
        "endpoint_describe=dipirist_endpoint_describe"
        "bridge_run=dipirist_bridge_run")
endif ()
if (DIPISRT_FOUND)
    dvbipitools_apply_tool_defs(dipisrt DIPISRT_SRCS
        "config_is_sender=dipisrt_config_is_sender"
        "endpoint_describe=dipisrt_endpoint_describe"
        "bridge_run=dipisrt_bridge_run")
endif ()
dvbipitools_apply_tool_defs(dipiradiohead DIPIRADIOHEAD_SRCS
    "codec_name=dipiradiohead_codec_name"
    "mcast_describe=dipiradiohead_mcast_describe")
# rtmp_fanout_cb: dipidescramble/pipeline.c defines its own, collides once merged here
dvbipitools_apply_tool_defs(dipirec DIPIREC_SRCS
    "rtmp_fanout_cb=dipirec_rtmp_fanout_cb")
dvbipitools_apply_tool_defs(dipisds DIPISDS_SRCS
    "announce_run=dipisds_announce_run"
    "listen_run=dipisds_listen_run"
    "mcast_describe=dipisds_mcast_describe")
dvbipitools_apply_tool_defs(dipitvhead DIPITVHEAD_SRCS
    "cas_start=dipitvhead_cas_start"
    "cas_stop=dipitvhead_cas_stop"
    "cas_failed=dipitvhead_cas_failed"
    "cas_scramble_packet=dipitvhead_cas_scramble_packet"
    "cas_flush=dipitvhead_cas_flush"
    "cas_get_metrics=dipitvhead_cas_get_metrics"
    "cas_vendor_metrics=dipitvhead_cas_vendor_metrics"
    "cas_vendor_super_cas_id=dipitvhead_cas_vendor_super_cas_id"
    "cas_prog_desc=dipitvhead_cas_prog_desc"
    "cas_build_cat=dipitvhead_cas_build_cat"
    "cas_vendor_count=dipitvhead_cas_vendor_count"
    "cas_vendor_ecm_pid=dipitvhead_cas_vendor_ecm_pid"
    "cas_vendor_emm_pid=dipitvhead_cas_vendor_emm_pid"
    "cas_vendor_ecm_due=dipitvhead_cas_vendor_ecm_due"
    "cas_vendor_next_emm=dipitvhead_cas_vendor_next_emm"
    "cas_reload_receivers=dipitvhead_cas_reload_receivers"
    "emit_metrics=dipitvhead_emit_metrics"
    "flush_batch=dipitvhead_flush_batch"
    "packet_cb=dipitvhead_packet_cb"
    "mcast_describe=dipitvhead_mcast_describe"
    "source_describe=dipitvhead_source_describe")
# capture_open/capture_close: dipifccret defines its own, collides once merged here
# find_header: dipidescramble defines its own, collides once merged here
dvbipitools_apply_tool_defs(dipixy DIPIXY_SRCS
    "capture_open=dipixy_capture_open"
    "capture_close=dipixy_capture_close"
    "find_header=dipixy_find_header")

set(DVBIPITOOLS_ALL_SRCS
    main.c
    ${DIPIBCG_SRCS}
    ${DIPIBIM_SRCS}
    ${DIPICAM378_SRCS}
    ${DIPIDESCRAMBLE_SRCS}
    ${DIPIFCCRET_SRCS}
    ${DIPIMETRICS_SRCS}
    ${DIPIRADIOHEAD_SRCS}
    ${DIPIREC_SRCS}
    ${DIPIRIST_SRCS}
    ${DIPISRT_SRCS}
    ${DIPISCAN_SRCS}
    ${DIPISDS_SRCS}
    ${DIPITVHEAD_SRCS}
    ${DIPIXMLTV_SRCS}
    ${DIPIXY_SRCS})

# real vs stub variant picked per tool (TLS, CSA2, CISSA, BISS, BISS-CA, RTMP auth).
# tools can disagree (dipiscan always stubs TLS, dipirec picks real when OpenSSL found).
# 1 variant per pair: real if an included tool->applet wants it.
set(DVBIPITOOLS_NEED_OPENSSL FALSE)
if (DIPIREC_HAVE_TLS OR DIPIRADIOHEAD_HAVE_TLS OR DIPIRADIOHEAD_HAVE_CISSA OR
    DIPITVHEAD_HAVE_TLS OR DIPITVHEAD_HAVE_CISSA OR DIPIDESCRAMBLE_FOUND OR
    DIPICAM378_FOUND OR DIPIRIST_HAVE_TLS OR DIPISRT_HAVE_TLS OR DIPIXY_HAVE_TLS)
    set(DVBIPITOOLS_NEED_OPENSSL TRUE)
endif ()

set(DVBIPITOOLS_NEED_CSA FALSE)
if (DIPIRADIOHEAD_CSA OR DIPITVHEAD_CSA OR (DIPIDESCRAMBLE_FOUND AND DIPIDESCRAMBLE_CSA))
    set(DVBIPITOOLS_NEED_CSA TRUE)
endif ()

set(DVBIPITOOLS_NEED_CISSA FALSE)
if (DIPIRADIOHEAD_HAVE_CISSA OR DIPITVHEAD_HAVE_CISSA OR DIPIDESCRAMBLE_FOUND)
    set(DVBIPITOOLS_NEED_CISSA TRUE)
endif ()

set(DVBIPITOOLS_NEED_AUTH FALSE)
if (DIPIREC_HAVE_TLS OR DIPIDESCRAMBLE_FOUND)
    set(DVBIPITOOLS_NEED_AUTH TRUE)
endif ()

list(REMOVE_ITEM DVBIPITOOLS_ALL_SRCS
    ${CMAKE_SOURCE_DIR}/src/lib/net/tls.c
    ${CMAKE_SOURCE_DIR}/src/lib/net/tls_stub.c
    ${CMAKE_SOURCE_DIR}/src/lib/scrambler/csa2.c
    ${CMAKE_SOURCE_DIR}/src/lib/scrambler/csa2_stub.c
    ${CMAKE_SOURCE_DIR}/src/lib/scrambler/cissa.c
    ${CMAKE_SOURCE_DIR}/src/lib/scrambler/cissa_stub.c
    ${CMAKE_SOURCE_DIR}/src/lib/cas/biss/biss.c
    ${CMAKE_SOURCE_DIR}/src/lib/cas/biss/stub.c
    ${CMAKE_SOURCE_DIR}/src/lib/cas/biss/ca.c
    ${CMAKE_SOURCE_DIR}/src/lib/cas/biss/ca_stub.c
    ${CMAKE_SOURCE_DIR}/src/lib/net/rtmp/auth.c
    ${CMAKE_SOURCE_DIR}/src/lib/net/rtmp/auth_stub.c)

if (DVBIPITOOLS_NEED_OPENSSL)
    list(APPEND DVBIPITOOLS_ALL_SRCS ${CMAKE_SOURCE_DIR}/src/lib/net/tls.c)
else ()
    list(APPEND DVBIPITOOLS_ALL_SRCS ${CMAKE_SOURCE_DIR}/src/lib/net/tls_stub.c)
endif ()

if (DVBIPITOOLS_NEED_CSA)
    list(APPEND DVBIPITOOLS_ALL_SRCS ${CMAKE_SOURCE_DIR}/src/lib/scrambler/csa2.c)
else ()
    list(APPEND DVBIPITOOLS_ALL_SRCS ${CMAKE_SOURCE_DIR}/src/lib/scrambler/csa2_stub.c)
endif ()

if (DVBIPITOOLS_NEED_CISSA)
    list(APPEND DVBIPITOOLS_ALL_SRCS
        ${CMAKE_SOURCE_DIR}/src/lib/scrambler/cissa.c
        ${CMAKE_SOURCE_DIR}/src/lib/cas/biss/biss.c
        ${CMAKE_SOURCE_DIR}/src/lib/cas/biss/ca.c)
else ()
    list(APPEND DVBIPITOOLS_ALL_SRCS
        ${CMAKE_SOURCE_DIR}/src/lib/scrambler/cissa_stub.c
        ${CMAKE_SOURCE_DIR}/src/lib/cas/biss/stub.c
        ${CMAKE_SOURCE_DIR}/src/lib/cas/biss/ca_stub.c)
endif ()

if (DVBIPITOOLS_NEED_AUTH)
    list(APPEND DVBIPITOOLS_ALL_SRCS ${CMAKE_SOURCE_DIR}/src/lib/net/rtmp/auth.c)
else ()
    list(APPEND DVBIPITOOLS_ALL_SRCS ${CMAKE_SOURCE_DIR}/src/lib/net/rtmp/auth_stub.c)
endif ()

list(REMOVE_DUPLICATES DVBIPITOOLS_ALL_SRCS)

add_executable(dvbipitools ${DVBIPITOOLS_ALL_SRCS})

if (DIPICAM378_FOUND)
    target_compile_definitions(dvbipitools PRIVATE DVBIPITOOLS_HAVE_CAM378)
endif ()
if (DIPIDESCRAMBLE_FOUND)
    target_compile_definitions(dvbipitools PRIVATE DVBIPITOOLS_HAVE_DESCRAMBLE)
endif ()
if (DIPIRIST_FOUND)
    target_compile_definitions(dvbipitools PRIVATE DVBIPITOOLS_HAVE_RIST)
endif ()
if (DIPIRIST_FOUND AND DIPIRIST_HAVE_AVG_BUFFER_TIME)
    target_compile_definitions(dvbipitools PRIVATE DIPIRIST_HAVE_AVG_BUFFER_TIME)
endif ()
if (DIPIRIST_FOUND AND DIPIRIST_LIBRIST_IPV6_WARN)
    target_compile_definitions(dvbipitools PRIVATE DIPIRIST_LIBRIST_IPV6_WARN)
endif ()
if (DIPISRT_FOUND)
    target_compile_definitions(dvbipitools PRIVATE DVBIPITOOLS_HAVE_SRT)
endif ()
if (DIPISRT_FOUND AND DIPISRT_HAVE_BONDING)
    target_compile_definitions(dvbipitools PRIVATE DIPISRT_HAVE_BONDING)
endif ()

if (DVBIPITOOLS_NEED_OPENSSL)
    set(DVBIPITOOLS_ATOMIC_LIB "")
    if (DVBIPITOOLS_STATIC)
        set(DVBIPITOOLS_ATOMIC_LIB atomic)
    endif ()
    target_link_libraries(dvbipitools PRIVATE OpenSSL::SSL OpenSSL::Crypto ${DVBIPITOOLS_ATOMIC_LIB})
endif ()

if (DVBIPITOOLS_HAVE_RIST)
    target_link_libraries(dvbipitools PRIVATE PkgConfig::RIST)
endif ()

if (DIPISRT_FOUND)
    target_link_libraries(dvbipitools PRIVATE PkgConfig::SRT)
endif ()

if (DIPIBCG_HAVE_ZLIB)
    target_link_libraries(dvbipitools PRIVATE ZLIB::ZLIB)
endif ()

if (DVBIPITOOLS_NEED_CSA)
    target_link_libraries(dvbipitools PRIVATE ${CMAKE_DL_LIBS})
endif ()

# dipifccret always needs pthread (channel.c ring buffers)
set(THREADS_PREFER_PTHREAD_FLAG ON)
find_package(Threads REQUIRED)
target_link_libraries(dvbipitools PRIVATE Threads::Threads)

# dipixy's hls.c calls ceil()
target_link_libraries(dvbipitools PRIVATE m)
