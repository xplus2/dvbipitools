# Copyright 2026 dvbipitools authors. Licensed under GPL-3.0-or-later.
# See NOTICE and LICENSE for details and authorship information.

dipisrt_resolve_sources()

if (DIPISRT_FOUND)
    add_executable(dipisrt ${DIPISRT_SRCS})

    target_link_libraries(dipisrt PRIVATE PkgConfig::SRT)
    if (DIPISRT_HAVE_TLS)
        target_link_libraries(dipisrt PRIVATE OpenSSL::SSL OpenSSL::Crypto)
    endif ()
    if (DIPISRT_HAVE_BONDING)
        target_compile_definitions(dipisrt PRIVATE DIPISRT_HAVE_BONDING)
    endif ()
endif ()
