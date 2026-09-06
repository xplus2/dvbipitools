# Copyright 2026 dvbipitools authors. Licensed under GPL-3.0-or-later.
# See NOTICE and LICENSE for details and authorship information.

dipimetrics_resolve_sources()

add_executable(dipimetrics ${DIPIMETRICS_SRCS})

if (DIPIMETRICS_HAVE_TLS)
    target_link_libraries(dipimetrics PRIVATE OpenSSL::SSL OpenSSL::Crypto ${DIPIMETRICS_ATOMIC_LIB})
endif ()
