# Copyright 2026 dvbipitools authors. Licensed under GPL-3.0-or-later.
# See NOTICE and LICENSE for details and authorship information.

dipiscan_resolve_sources()

add_executable(dipiscan ${DIPISCAN_SRCS})

# -j/--jets threads
set(THREADS_PREFER_PTHREAD_FLAG ON)
find_package(Threads REQUIRED)
target_link_libraries(dipiscan PRIVATE Threads::Threads)
