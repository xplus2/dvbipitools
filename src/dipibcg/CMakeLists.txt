# Copyright 2026 dvbipitools authors. Licensed under GPL-3.0-or-later.
# See NOTICE and LICENSE for details and authorship information.

dipibcg_resolve_sources()

add_executable(dipibcg ${DIPIBCG_SRCS})

if (DIPIBCG_HAVE_ZLIB)
    target_link_libraries(dipibcg PRIVATE ZLIB::ZLIB)
endif ()
