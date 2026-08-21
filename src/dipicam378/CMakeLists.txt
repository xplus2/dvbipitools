# Copyright 2026 dvbipitools authors. Licensed under GPL-3.0-or-later.
# See NOTICE and LICENSE for details and authorship information.

dipicam378_resolve_sources()

if (DIPICAM378_FOUND)
    add_executable(dipicam378 ${DIPICAM378_SRCS})

    target_link_libraries(dipicam378 PRIVATE OpenSSL::Crypto ${DIPICAM378_ATOMIC_LIB})

    set(THREADS_PREFER_PTHREAD_FLAG ON)
    find_package(Threads REQUIRED)
    target_link_libraries(dipicam378 PRIVATE Threads::Threads)
endif ()
