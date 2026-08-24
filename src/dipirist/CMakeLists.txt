# Copyright 2026 dvbipitools authors. Licensed under GPL-3.0-or-later.
# See NOTICE and LICENSE for details and authorship information.

dipirist_resolve_sources()

if (DIPIRIST_FOUND)
    add_executable(dipirist ${DIPIRIST_SRCS})

    target_link_libraries(dipirist PRIVATE PkgConfig::RIST)
    if (DIPIRIST_HAVE_TLS)
        target_link_libraries(dipirist PRIVATE OpenSSL::SSL OpenSSL::Crypto ${DIPIRIST_ATOMIC_LIB})
    endif ()
    if (DIPIRIST_HAVE_AVG_BUFFER_TIME)
        target_compile_definitions(dipirist PRIVATE DIPIRIST_HAVE_AVG_BUFFER_TIME)
    endif ()
    if (DIPIRIST_LIBRIST_IPV6_WARN)
        target_compile_definitions(dipirist PRIVATE DIPIRIST_LIBRIST_IPV6_WARN)
    endif ()

    if (DVBIPITOOLS_HAVE_SRT)
        target_link_libraries(dipirist PRIVATE PkgConfig::SRT)
        set(THREADS_PREFER_PTHREAD_FLAG ON)
        find_package(Threads REQUIRED)
        target_link_libraries(dipirist PRIVATE Threads::Threads)
    endif ()
endif ()
