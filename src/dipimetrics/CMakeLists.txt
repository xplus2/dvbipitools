# Copyright 2026 dvbipitools authors. Licensed under GPL-3.0-or-later.
# See NOTICE and LICENSE for details and authorship information.

# keep in sync with CMakeLists/Makefile
add_executable(dipimetrics
    main.c
    args.c
    store.c
    render.c
    httpserver.c
    ../lib/log.c
    ../lib/argutil.c
    ../lib/signal.c
    ../lib/ioutil.c
    ../lib/metrics/protocol.c)
