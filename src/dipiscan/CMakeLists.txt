# Copyright 2026 dvbipitools authors. Licensed under GPL-3.0-or-later.
# See NOTICE and LICENSE for details and authorship information.

# keep in sync with CMakeLists/Makefile
add_executable(dipiscan
    main.c
    args.c
    format.c
    scan.c
    ../lib/playlist_out.c
    ../lib/log.c
    ../lib/argutil.c
    ../lib/signal.c
    ../lib/sds_xml.c
    ../lib/xml_util.c
    ../lib/ioutil.c
    ../lib/net/multicast.c
    ../lib/net/netconnect.c
    ../lib/net/httpclient/httpclient.c
    ../lib/net/httpclient/url.c
    ../lib/net/httpclient/read.c
    ../lib/net/httpclient/async.c
    ../lib/net/tls_stub.c
    ../lib/demux/rtp.c
    ../lib/demux/crc32.c
    ../lib/demux/psi/psi.c
    ../lib/demux/psi/parse.c
    ../lib/demux/psi/descriptors.c
    ../lib/demux/psi/section_asm.c
    ../lib/demux/tspack.c)
