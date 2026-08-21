# Copyright 2026 dvbipitools authors. Licensed under GPL-3.0-or-later.
# See NOTICE and LICENSE for details and authorship information.

dipiradiohead_resolve_sources()

add_executable(dipiradiohead ${DIPIRADIOHEAD_SRCS})

if (DIPIRADIOHEAD_HAVE_TLS)
    target_link_libraries(dipiradiohead PRIVATE OpenSSL::SSL OpenSSL::Crypto ${DIPIRADIOHEAD_ATOMIC_LIB})
endif ()

if (DIPIRADIOHEAD_HAVE_CISSA AND NOT DIPIRADIOHEAD_HAVE_TLS)
    target_link_libraries(dipiradiohead PRIVATE OpenSSL::Crypto ${DIPIRADIOHEAD_ATOMIC_LIB})
endif ()

if (DIPIRADIOHEAD_CSA)
    target_link_libraries(dipiradiohead PRIVATE ${CMAKE_DL_LIBS})
endif ()

if (DVBIPITOOLS_HAVE_RIST)
    target_link_libraries(dipiradiohead PRIVATE PkgConfig::RIST)
endif ()

# ECMG client thread: pthread is a hard dependency from here on, not optional
set(THREADS_PREFER_PTHREAD_FLAG ON)
find_package(Threads REQUIRED)
target_link_libraries(dipiradiohead PRIVATE Threads::Threads)
