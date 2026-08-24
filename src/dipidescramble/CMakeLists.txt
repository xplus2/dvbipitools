# Copyright 2026 dvbipitools authors. Licensed under GPL-3.0-or-later.
# See NOTICE and LICENSE for details and authorship information.

dipidescramble_resolve_sources()

if (DIPIDESCRAMBLE_FOUND)
    add_executable(dipidescramble ${DIPIDESCRAMBLE_SRCS})

    target_link_libraries(dipidescramble PRIVATE OpenSSL::SSL OpenSSL::Crypto ${DIPIDESCRAMBLE_ATOMIC_LIB})

    if (DIPIDESCRAMBLE_CSA)
        target_link_libraries(dipidescramble PRIVATE ${CMAKE_DL_LIBS})
    endif ()

    if (DVBIPITOOLS_HAVE_RIST)
        target_link_libraries(dipidescramble PRIVATE PkgConfig::RIST)
    endif ()

    if (DVBIPITOOLS_HAVE_SRT)
        target_link_libraries(dipidescramble PRIVATE PkgConfig::SRT)
    endif ()

    # rist:///srt:// input's reader thread: pthread is a hard dependency from here on, not optional
    set(THREADS_PREFER_PTHREAD_FLAG ON)
    find_package(Threads REQUIRED)
    target_link_libraries(dipidescramble PRIVATE Threads::Threads)
endif ()
