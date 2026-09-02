# Copyright 2026 dvbipitools authors. Licensed under GPL-3.0-or-later.
# See NOTICE and LICENSE for details and authorship information.

dipixy_resolve_sources()

set(DIPIXY_HTDOCS_INDEX_GEN ${CMAKE_CURRENT_BINARY_DIR}/htdocs_index.gen.c)
add_custom_command(
    OUTPUT ${DIPIXY_HTDOCS_INDEX_GEN}
    COMMAND ${GEN_HTDOCS_EXE} ${CMAKE_CURRENT_SOURCE_DIR}/htdocs/index.html ${DIPIXY_HTDOCS_INDEX_GEN} g_htdocs_index_html
    DEPENDS ${GEN_HTDOCS_DEP} ${CMAKE_CURRENT_SOURCE_DIR}/htdocs/index.html
    COMMENT "dipixy: embedding htdocs/index.html")

add_executable(dipixy ${DIPIXY_SRCS} ${DIPIXY_HTDOCS_INDEX_GEN})
target_link_libraries(dipixy PRIVATE m)

if (DIPIXY_HAVE_TLS)
    target_compile_definitions(dipixy PRIVATE HAVE_TLS)
    target_link_libraries(dipixy PRIVATE OpenSSL::SSL OpenSSL::Crypto ${DIPIXY_ATOMIC_LIB})
endif ()

if (DVBIPITOOLS_HAVE_RIST)
    target_link_libraries(dipixy PRIVATE PkgConfig::RIST)
endif ()

if (DVBIPITOOLS_HAVE_SRT)
    target_link_libraries(dipixy PRIVATE PkgConfig::SRT)
endif ()

if (DIPIXY_HAVE_HTTP2)
    target_compile_definitions(dipixy PRIVATE HAVE_HTTP2)
    target_link_libraries(dipixy PRIVATE PkgConfig::NGHTTP2)
endif ()

if (DIPIXY_HAVE_HTTP3)
    target_compile_definitions(dipixy PRIVATE HAVE_HTTP3)
    target_link_libraries(dipixy PRIVATE PkgConfig::NGTCP2 PkgConfig::NGTCP2_CRYPTO_OSSL PkgConfig::NGHTTP3)
endif ()

set(THREADS_PREFER_PTHREAD_FLAG ON)
find_package(Threads REQUIRED)
target_link_libraries(dipixy PRIVATE Threads::Threads)
