# Copyright 2026 dvbipitools authors. Licensed under GPL-3.0-or-later.
# See NOTICE and LICENSE for details and authorship information.

dipisds_resolve_sources()

add_executable(dipisds ${DIPISDS_SRCS})

if (DIPISDS_HAVE_ZLIB)
    target_link_libraries(dipisds PRIVATE ZLIB::ZLIB)
    target_compile_definitions(dipisds PRIVATE DVBIPITOOLS_SDNS_ZLIB)
endif ()
