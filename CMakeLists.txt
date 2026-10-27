# Copyright 2026 dvbipitools authors. Licensed under GPL-3.0-or-later.
# See NOTICE and LICENSE for details and authorship information.

cmake_minimum_required(VERSION 3.16)
project(dvbipitools C)

if (NOT CMAKE_BUILD_TYPE)
    set(CMAKE_BUILD_TYPE Debug CACHE STRING "build type" FORCE)
endif ()

option(DVBIPITOOLS_STATIC "link statically" OFF)
option(DVBIPITOOLS_SANITIZE "build with ASan + UBSan" OFF)
option(DVBIPITOOLS_TSAN "build with TSan" OFF)
option(DVBIPITOOLS_COVERAGE "build with gcov" OFF)
option(DVBIPITOOLS_TESTS "build unit tests (requires 'check' via pkg-config)" OFF)
option(DVBIPITOOLS_FUZZING "build AFL++ fuzz harnesses (configure with -DCMAKE_C_COMPILER=afl-clang-fast)" OFF)
option(DVBIPITOOLS_TLS "build with HTTPS/TLS network transport support" ON)
option(DVBIPITOOLS_CSA "build with DVB-CSA (CSA1/CSA2/BISS1) support" ON)

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

if (DVBIPITOOLS_COVERAGE)
    add_compile_options(--coverage -fprofile-update=atomic -O0)
    add_link_options(--coverage)
    find_program(GCOVR_EXECUTABLE gcovr)
    if (GCOVR_EXECUTABLE)
        add_custom_target(coverage
            COMMAND ${CMAKE_COMMAND} -E make_directory ${CMAKE_BINARY_DIR}/coverage
            COMMAND ${GCOVR_EXECUTABLE} --root . --object-directory ${CMAKE_BINARY_DIR} --filter src/
                    --exclude-directories ".*/CMakeFiles/[0-9.]+" --exclude-throw-branches --exclude-unreachable-branches
                    --merge-lines --gcov-ignore-parse-errors=negative_hits.warn_once_per_file
                    --exclude src/lib/vendor/ --json-summary ${CMAKE_BINARY_DIR}/coverage/src-novendor.json
                    --txt ${CMAKE_BINARY_DIR}/coverage/src-novendor.txt
            WORKING_DIRECTORY ${CMAKE_SOURCE_DIR}
            COMMENT "gcovr report in ${CMAKE_BINARY_DIR}/coverage")
    endif ()
endif ()

option(DVBIPITOOLS_RIST "detect librist for rist:// support (dipirist; rist:// output in dipirec/dipitvhead/dipiradiohead; rist:// input in dipirec/dipitvhead/dipidescramble)" ON)

set(DVBIPITOOLS_HAVE_RIST FALSE)
if (DVBIPITOOLS_RIST)
    find_package(PkgConfig)
    if (PkgConfig_FOUND)
        pkg_check_modules(RIST IMPORTED_TARGET librist)
        if (RIST_FOUND)
            set(DVBIPITOOLS_HAVE_RIST TRUE)
            include(CheckCSourceCompiles)
            set(CMAKE_REQUIRED_INCLUDES ${RIST_INCLUDE_DIRS})
            check_c_source_compiles("
#include <librist/librist.h>
int main(void) {
    struct rist_stats_receiver_flow f;
    return (int) f.avg_buffer_time;
}
" DVBIPITOOLS_RIST_HAVE_AVG_BUFFER_TIME)
            unset(CMAKE_REQUIRED_INCLUDES)
            if (DVBIPITOOLS_RIST_HAVE_AVG_BUFFER_TIME)
                set_property(TARGET PkgConfig::RIST APPEND PROPERTY INTERFACE_COMPILE_DEFINITIONS DVBIPITOOLS_RIST_HAVE_AVG_BUFFER_TIME)
            endif ()
            set(DVBIPITOOLS_RIST_IPV6_WARN FALSE)
            if (RIST_VERSION AND RIST_VERSION VERSION_LESS_EQUAL "0.2.20")
                set(DVBIPITOOLS_RIST_IPV6_WARN TRUE)
            endif ()
            if (DVBIPITOOLS_STATIC)
                list(APPEND RIST_STATIC_LDFLAGS atomic)
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
        message(WARNING "librist not found (or no static libraries available): dipirist skipped, rist:// output unavailable in dipirec/dipitvhead/dipiradiohead, rist:// input unavailable in dipirec/dipitvhead/dipidescramble")
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

# netconnect.c resolves names in a thread: every tool needs pthread
find_package(Threads REQUIRED)
link_libraries(Threads::Threads)

link_libraries(atomic)

set(BUILD_ARCH "${CMAKE_SYSTEM_PROCESSOR}")

add_compile_definitions(
    BUILD_ARCH="${BUILD_ARCH}"
    BUILD_TYPE="${BUILD_TYPE}"
    BUILD_LINK="${BUILD_LINK}")

include_directories("${CMAKE_SOURCE_DIR}/src")

include(${CMAKE_SOURCE_DIR}/toolchain/tool_sources.cmake)

if (CMAKE_CROSSCOMPILING)
    if (DEFINED ENV{CC_FOR_BUILD} AND NOT "$ENV{CC_FOR_BUILD}" STREQUAL "")
        set(GEN_HTDOCS_HOST_CC_CANDIDATES $ENV{CC_FOR_BUILD})
    else ()
        set(GEN_HTDOCS_HOST_CC_CANDIDATES cc gcc clang)
    endif ()
    find_program(GEN_HTDOCS_HOST_CC NAMES ${GEN_HTDOCS_HOST_CC_CANDIDATES})
    if (NOT GEN_HTDOCS_HOST_CC)
        message(FATAL_ERROR "gen_htdocs: no build-host C compiler found (set CC_FOR_BUILD)")
    endif ()
    set(GEN_HTDOCS_EXE ${CMAKE_BINARY_DIR}/gen_htdocs)
    add_custom_command(
        OUTPUT ${GEN_HTDOCS_EXE}
        COMMAND ${GEN_HTDOCS_HOST_CC} -std=gnu11 -Wall -Wextra -Werror -O2 ${CMAKE_SOURCE_DIR}/src/dipixy/gen_htdocs.c -o ${GEN_HTDOCS_EXE}
        DEPENDS ${CMAKE_SOURCE_DIR}/src/dipixy/gen_htdocs.c
        COMMENT "building gen_htdocs for build host")
    add_custom_target(gen_htdocs_host DEPENDS ${GEN_HTDOCS_EXE})
    set(GEN_HTDOCS_DEP gen_htdocs_host)
else ()
    add_executable(gen_htdocs ${CMAKE_SOURCE_DIR}/src/dipixy/gen_htdocs.c)
    set(GEN_HTDOCS_EXE gen_htdocs)
    set(GEN_HTDOCS_DEP gen_htdocs)
endif ()

# generic src/*/CMakeLists.txt
file(GLOB tool_dirs LIST_DIRECTORIES true "${CMAKE_SOURCE_DIR}/src/*")
foreach (d ${tool_dirs})
    if (IS_DIRECTORY "${d}" AND EXISTS "${d}/CMakeLists.txt")
        add_subdirectory("${d}")
    endif ()
endforeach ()

# static builds: SBOM from link maps
if (DVBIPITOOLS_STATIC)
    set(SBOM_TARGETS "")
    set(SBOM_BINS "")
    foreach (d ${tool_dirs})
        get_filename_component(t "${d}" NAME)
        if (TARGET ${t})
            target_link_options(${t} PRIVATE "-Wl,-Map=$<TARGET_FILE:${t}>.map")
            list(APPEND SBOM_TARGETS ${t})
            list(APPEND SBOM_BINS "$<TARGET_FILE:${t}>")
        endif ()
    endforeach ()
    set(SBOM_CPPFLAGS "")
    if (CMAKE_SYSROOT)
        string(APPEND SBOM_CPPFLAGS " --sysroot=${CMAKE_SYSROOT}")
    endif ()
    foreach (i ${OPENSSL_INCLUDE_DIR} ${ZLIB_INCLUDE_DIR})
        string(APPEND SBOM_CPPFLAGS " -I${i}")
    endforeach ()
    # build step lacks configure-time pkg-config env
    set(SBOM_ENV "")
    if (PKG_CONFIG_EXECUTABLE)
        list(APPEND SBOM_ENV "PKG_CONFIG=${PKG_CONFIG_EXECUTABLE}")
    endif ()
    foreach (v PKG_CONFIG_PATH PKG_CONFIG_LIBDIR PKG_CONFIG_SYSROOT_DIR)
        if (DEFINED ENV{${v}})
            list(APPEND SBOM_ENV "${v}=$ENV{${v}}")
        endif ()
    endforeach ()
    set(SBOM_OUT ${CMAKE_BINARY_DIR}/dvbipitools.cdx.json)
    add_custom_command(
        OUTPUT ${SBOM_OUT}
        COMMAND ${CMAKE_COMMAND} -E env ${SBOM_ENV} sh ${CMAKE_SOURCE_DIR}/toolchain/sbom.sh -o ${SBOM_OUT} -c ${CMAKE_C_COMPILER} -f "${SBOM_CPPFLAGS}" ${SBOM_BINS}
        DEPENDS ${SBOM_TARGETS} ${CMAKE_SOURCE_DIR}/toolchain/sbom.sh
        COMMENT "sbom: dvbipitools.cdx.json"
        VERBATIM)
    add_custom_target(sbom ALL DEPENDS ${SBOM_OUT})
endif ()

if (DVBIPITOOLS_TESTS)
    enable_testing()
    add_subdirectory(tests)
endif ()

if (DVBIPITOOLS_FUZZING)
    add_subdirectory(tests/fuzz)
endif ()
