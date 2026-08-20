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

link_libraries(atomic)

set(BUILD_ARCH "${CMAKE_SYSTEM_PROCESSOR}")

add_compile_definitions(
    BUILD_ARCH="${BUILD_ARCH}"
    BUILD_TYPE="${BUILD_TYPE}"
    BUILD_LINK="${BUILD_LINK}")

include_directories("${CMAKE_SOURCE_DIR}/src")

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
