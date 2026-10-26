# Copyright 2026 dvbipitools authors. Licensed under GPL-3.0-or-later.
# See NOTICE and LICENSE for details and authorship information.

dipibim_resolve_sources()

add_executable(dipibim ${DIPIBIM_SRCS})

if (DIPIBIM_HAVE_ZLIB)
    target_link_libraries(dipibim PRIVATE ZLIB::ZLIB)
    target_compile_definitions(dipibim PRIVATE DIPIBIM_ZLIB)
endif ()
