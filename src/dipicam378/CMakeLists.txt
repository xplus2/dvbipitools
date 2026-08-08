# Copyright 2026 dvbipitools authors. Licensed under GPL-3.0-or-later.
# See NOTICE and LICENSE for details and authorship information.

if (DVBIPITOOLS_STATIC)
    set(OPENSSL_USE_STATIC_LIBS TRUE)
endif ()
find_package(OpenSSL)

if (OpenSSL_FOUND)
    add_executable(dipicam378
        main.c
        args.c
        cs378x.c
        crypto.c
        device.c
        ../lib/log.c
        ../lib/argutil.c
        ../lib/signal.c)

    target_link_libraries(dipicam378 PRIVATE OpenSSL::Crypto)

    set(THREADS_PREFER_PTHREAD_FLAG ON)
    find_package(Threads REQUIRED)
    target_link_libraries(dipicam378 PRIVATE Threads::Threads)
else ()
    message(WARNING "dipicam378: OpenSSL not found, skipping this tool entirely (RSA/AES crypto is its whole purpose)")
endif ()
