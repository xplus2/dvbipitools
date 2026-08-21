# Copyright 2026 dvbipitools authors. Licensed under GPL-3.0-or-later.
# See NOTICE and LICENSE for details and authorship information.

dipidescramble_resolve_sources()

if (DIPIDESCRAMBLE_FOUND)
    add_executable(dipidescramble ${DIPIDESCRAMBLE_SRCS})

    target_link_libraries(dipidescramble PRIVATE OpenSSL::SSL OpenSSL::Crypto ${DIPIDESCRAMBLE_ATOMIC_LIB})

    if (DIPIDESCRAMBLE_CSA)
        target_link_libraries(dipidescramble PRIVATE ${CMAKE_DL_LIBS})
    endif ()
endif ()
