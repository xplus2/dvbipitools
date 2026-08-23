# Copyright 2026 dvbipitools authors. Licensed under GPL-3.0-or-later.
# See NOTICE and LICENSE for details and authorship information.

cmake_minimum_required(VERSION 3.16)
project(dvbipitools C)

if (NOT CMAKE_BUILD_TYPE)
    set(CMAKE_BUILD_TYPE Debug CACHE STRING "build type" FORCE)
endif ()

option(DVBIPITOOLS_STATIC "link tools statically" OFF)
option(DVBIPITOOLS_SANITIZE "build with AddressSanitizer + UndefinedBehaviorSanitizer" OFF)
option(DVBIPITOOLS_TSAN "build with ThreadSanitizer (cannot combine with DVBIPITOOLS_SANITIZE)" OFF)
option(DVBIPITOOLS_TESTS "build unit tests (requires Check via pkg-config)" OFF)
option(DVBIPITOOLS_FUZZING "build AFL++ fuzz harnesses (configure with -DCMAKE_C_COMPILER=afl-clang-fast for instrumentation)" OFF)

set(CMAKE_C_STANDARD 11)
set(CMAKE_C_STANDARD_REQUIRED ON)
set(CMAKE_C_EXTENSIONS ON)

add_compile_options(-Wall -Wextra -Werror)

if (CMAKE_BUILD_TYPE STREQUAL "Release")
    set(BUILD_TYPE "Release")
    add_link_options(-s)
else ()
    set(BUILD_TYPE "Debug")
endif ()

if (DVBIPITOOLS_STATIC)
    set(BUILD_LINK "static")
    add_link_options(-static)
else ()
    set(BUILD_LINK "dynamic")
endif ()

if (DVBIPITOOLS_SANITIZE)
    if (DVBIPITOOLS_STATIC)
        message(FATAL_ERROR "DVBIPITOOLS_SANITIZE and DVBIPITOOLS_STATIC cannot be combined")
    endif ()
    if (DVBIPITOOLS_TSAN)
        message(FATAL_ERROR "DVBIPITOOLS_SANITIZE and DVBIPITOOLS_TSAN cannot be combined")
    endif ()
    add_compile_options(-fsanitize=address,undefined -fno-omit-frame-pointer)
    add_link_options(-fsanitize=address,undefined)
endif ()

if (DVBIPITOOLS_TSAN)
    if (DVBIPITOOLS_STATIC)
        message(FATAL_ERROR "DVBIPITOOLS_TSAN and DVBIPITOOLS_STATIC cannot be combined")
    endif ()
    add_compile_options(-fsanitize=thread -fno-omit-frame-pointer)
    add_link_options(-fsanitize=thread)
endif ()

option(DVBIPITOOLS_RIST "detect librist for rist:// support (dipirist, and rist:// output in dipirec/dipitvhead/dipiradiohead)" ON)

set(DVBIPITOOLS_HAVE_RIST FALSE)
if (DVBIPITOOLS_RIST)
    find_package(PkgConfig)
    if (PkgConfig_FOUND)
        pkg_check_modules(RIST IMPORTED_TARGET librist)
        if (RIST_FOUND)
            set(DVBIPITOOLS_HAVE_RIST TRUE)
            set(DVBIPITOOLS_RIST_IPV6_WARN FALSE)
            if (RIST_VERSION AND RIST_VERSION VERSION_LESS_EQUAL "0.2.20")
                set(DVBIPITOOLS_RIST_IPV6_WARN TRUE)
            endif ()
            if (DVBIPITOOLS_STATIC)
                include(CheckCSourceCompiles)
                set(CMAKE_REQUIRED_LIBRARIES ${RIST_STATIC_LDFLAGS})
                set(CMAKE_REQUIRED_LINK_OPTIONS -static ${RIST_STATIC_LDFLAGS_OTHER})
                check_c_source_compiles("int main(void) { return 0; }" DVBIPITOOLS_RIST_STATIC_LINKS)
                unset(CMAKE_REQUIRED_LIBRARIES)
                unset(CMAKE_REQUIRED_LINK_OPTIONS)
                if (DVBIPITOOLS_RIST_STATIC_LINKS)
                    set_target_properties(PkgConfig::RIST PROPERTIES
                        INTERFACE_LINK_LIBRARIES "${RIST_STATIC_LDFLAGS}"
                        INTERFACE_LINK_OPTIONS "${RIST_STATIC_LDFLAGS_OTHER}")
                else ()
                    set(DVBIPITOOLS_HAVE_RIST FALSE)
                endif ()
            endif ()
        endif ()
    endif ()
    if (NOT DVBIPITOOLS_HAVE_RIST)
        message(WARNING "librist not found (or no static libraries available): dipirist skipped, rist:// output unavailable in dipirec/dipitvhead/dipiradiohead")
    endif ()
endif ()

option(DVBIPITOOLS_SRT "detect libsrt for srt:// support (dipisrt)" ON)

set(DVBIPITOOLS_HAVE_SRT FALSE)
if (DVBIPITOOLS_SRT)
    find_package(PkgConfig)
    if (PkgConfig_FOUND)
        pkg_check_modules(SRT IMPORTED_TARGET srt)
        if (SRT_FOUND)
            set(DVBIPITOOLS_HAVE_SRT TRUE)
            if (DVBIPITOOLS_STATIC)
                list(APPEND SRT_STATIC_LDFLAGS atomic)
                include(CheckCSourceCompiles)
                set(CMAKE_REQUIRED_LIBRARIES ${SRT_STATIC_LDFLAGS})
                set(CMAKE_REQUIRED_LINK_OPTIONS -static ${SRT_STATIC_LDFLAGS_OTHER})
                check_c_source_compiles("int main(void) { return 0; }" DVBIPITOOLS_SRT_STATIC_LINKS)
                unset(CMAKE_REQUIRED_LIBRARIES)
                unset(CMAKE_REQUIRED_LINK_OPTIONS)
                if (DVBIPITOOLS_SRT_STATIC_LINKS)
                    set_target_properties(PkgConfig::SRT PROPERTIES
                        INTERFACE_LINK_LIBRARIES "${SRT_STATIC_LDFLAGS}"
                        INTERFACE_LINK_OPTIONS "${SRT_STATIC_LDFLAGS_OTHER}")
                else ()
                    set(DVBIPITOOLS_HAVE_SRT FALSE)
                endif ()
            endif ()
        endif ()
    endif ()
    if (NOT DVBIPITOOLS_HAVE_SRT)
        message(WARNING "libsrt not found (or no static libraries available): dipisrt skipped")
    endif ()
endif ()

if (DVBIPITOOLS_HAVE_SRT AND CMAKE_CROSSCOMPILING)
    message(WARNING "dipisrt: cross-compiling, cannot probe libsrt for bonding support at configure time - --group-mode disabled")
    set(DVBIPITOOLS_SRT_HAVE_BONDING FALSE)
elseif (DVBIPITOOLS_HAVE_SRT)
    include(CheckCSourceRuns)
    set(CMAKE_REQUIRED_LIBRARIES PkgConfig::SRT)
    check_c_source_runs("
#include <srt/srt.h>
int main(void) {
    int rc;
    srt_startup();
    rc = srt_create_group(SRT_GTYPE_BROADCAST);
    if (rc >= 0)
        srt_close(rc);
    srt_cleanup();
    return rc < 0 ? 1 : 0;
}
" DVBIPITOOLS_SRT_HAVE_BONDING)
    unset(CMAKE_REQUIRED_LIBRARIES)
    if (NOT DVBIPITOOLS_SRT_HAVE_BONDING)
        message(WARNING "dipisrt: linked libsrt has no bonding support (ENABLE_BONDING=OFF) - --group-mode disabled")
    endif ()
else ()
    set(DVBIPITOOLS_SRT_HAVE_BONDING FALSE)
endif ()

link_libraries(atomic)

set(BUILD_ARCH "${CMAKE_SYSTEM_PROCESSOR}")

add_compile_definitions(
    BUILD_ARCH="${BUILD_ARCH}"
    BUILD_TYPE="${BUILD_TYPE}"
    BUILD_LINK="${BUILD_LINK}")

include_directories("${CMAKE_SOURCE_DIR}/src")

include(${CMAKE_SOURCE_DIR}/cmake/tool_sources.cmake)

# generic src/*/CMakeLists.txt
file(GLOB tool_dirs LIST_DIRECTORIES true "${CMAKE_SOURCE_DIR}/src/*")
foreach (d ${tool_dirs})
    if (IS_DIRECTORY "${d}" AND EXISTS "${d}/CMakeLists.txt")
        add_subdirectory("${d}")
    endif ()
endforeach ()

if (DVBIPITOOLS_TESTS)
    enable_testing()
    add_subdirectory(tests)
endif ()

if (DVBIPITOOLS_FUZZING)
    add_subdirectory(tests/fuzz)
endif ()
