# Copyright 2026 dvbipitools authors. Licensed under GPL-3.0-or-later.
# See NOTICE and LICENSE for details and authorship information.

cmake_minimum_required(VERSION 3.16)
project(dvbipitools C)

if (NOT CMAKE_BUILD_TYPE)
    set(CMAKE_BUILD_TYPE Debug CACHE STRING "build type" FORCE)
endif ()

option(DVBIPITOOLS_STATIC "link tools statically" OFF)

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
