# Copyright 2026 dvbipitools authors. Licensed under GPL-3.0-or-later.
# See NOTICE and LICENSE for details and authorship information.

dipirec_resolve_sources()

add_executable(dipirec ${DIPIREC_SRCS})

if (DIPIREC_HAVE_TLS)
    target_link_libraries(dipirec PRIVATE OpenSSL::SSL OpenSSL::Crypto ${DIPIREC_ATOMIC_LIB})
endif ()

if (DVBIPITOOLS_HAVE_RIST)
    target_link_libraries(dipirec PRIVATE PkgConfig::RIST)
endif ()
