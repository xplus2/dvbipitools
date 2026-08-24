# Copyright 2026 dvbipitools authors. Licensed under GPL-3.0-or-later.
# See NOTICE and LICENSE for details and authorship information.

dipisrt_resolve_sources()

if (DIPISRT_FOUND)
    add_executable(dipisrt ${DIPISRT_SRCS})

    target_link_libraries(dipisrt PRIVATE PkgConfig::SRT)
    if (DVBIPITOOLS_HAVE_RIST)
        target_link_libraries(dipisrt PRIVATE PkgConfig::RIST)
    endif ()
    if (DIPISRT_HAVE_TLS)
        target_link_libraries(dipisrt PRIVATE OpenSSL::SSL OpenSSL::Crypto)
    endif ()
    if (DIPISRT_HAVE_BONDING)
        target_compile_definitions(dipisrt PRIVATE DIPISRT_HAVE_BONDING)
    endif ()

    # srtsrc.c's reader thread (feeds tssource.c's TSSRC_SRT, unused by dipisrt itself
    # but linked in for multicall-binary symbol consistency): pthread is a hard dependency
    set(THREADS_PREFER_PTHREAD_FLAG ON)
    find_package(Threads REQUIRED)
    target_link_libraries(dipisrt PRIVATE Threads::Threads)
endif ()
