# Copyright 2026 dvbipitools authors. Licensed under GPL-3.0-or-later.
# See NOTICE and LICENSE for details and authorship information.

dipitvhead_resolve_sources()

add_executable(dipitvhead ${DIPITVHEAD_SRCS})

if (DIPITVHEAD_HAVE_TLS)
    target_link_libraries(dipitvhead PRIVATE OpenSSL::SSL OpenSSL::Crypto ${DIPITVHEAD_ATOMIC_LIB})
endif ()

if (DIPITVHEAD_HAVE_CISSA AND NOT DIPITVHEAD_HAVE_TLS)
    target_link_libraries(dipitvhead PRIVATE OpenSSL::Crypto ${DIPITVHEAD_ATOMIC_LIB})
endif ()

if (DIPITVHEAD_CSA)
    target_link_libraries(dipitvhead PRIVATE ${CMAKE_DL_LIBS})
endif ()

if (DVBIPITOOLS_HAVE_RIST)
    target_link_libraries(dipitvhead PRIVATE PkgConfig::RIST)
endif ()

# ECMG client thread: pthread is a hard dependency from here on, not optional
set(THREADS_PREFER_PTHREAD_FLAG ON)
find_package(Threads REQUIRED)
target_link_libraries(dipitvhead PRIVATE Threads::Threads)
