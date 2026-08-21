# Copyright 2026 dvbipitools authors. Licensed under GPL-3.0-or-later.
# See NOTICE and LICENSE for details and authorship information.

dipifccret_resolve_sources()

add_executable(dipifccret ${DIPIFCCRET_SRCS})

set(THREADS_PREFER_PTHREAD_FLAG ON)
find_package(Threads REQUIRED)
target_link_libraries(dipifccret PRIVATE Threads::Threads)

# channel.c's C11 64-bit atomics need libatomic on archs without native 8-byte atomics (32-bit ARM).
# target_link_libraries not link_options: must land after objects, static link order matters.
target_link_libraries(dipifccret PRIVATE atomic)
