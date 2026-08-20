# Copyright 2026 dvbipitools authors. Licensed under GPL-3.0-or-later.
# See NOTICE and LICENSE for details and authorship information.

if (DVBIPITOOLS_STATIC)
    set(OPENSSL_USE_STATIC_LIBS TRUE)
    set(ATOMIC_LIB atomic)
endif ()
find_package(OpenSSL)

if (OpenSSL_FOUND)
    add_executable(dipicam378
        main.c
        args.c
        cs378x/cs378x.c
        cs378x/crypto.c
        cs378x/protocol.c
        cs378x/worker.c
        device.c
        ../lib/cas/device_crypto.c
        ../lib/cas/device_state_core.c
        ../lib/log.c
        ../lib/argutil.c
        ../lib/signal.c
        ../lib/secure_zero.c)

    target_link_libraries(dipicam378 PRIVATE OpenSSL::Crypto ${ATOMIC_LIB})

    set(THREADS_PREFER_PTHREAD_FLAG ON)
    find_package(Threads REQUIRED)
    target_link_libraries(dipicam378 PRIVATE Threads::Threads)
else ()
    message(WARNING "dipicam378: OpenSSL not found, skipping this tool entirely (RSA/AES crypto is its whole purpose)")
endif ()
