# Copyright 2026 dvbipitools authors. Licensed under GPL-3.0-or-later.
# See NOTICE and LICENSE for details and authorship information.

# keep in sync with CMakeLists/Makefile
add_executable(dipibcg
    main.c
    args.c
    announce.c
    listen.c
    container.c
    ../lib/log.c
    ../lib/argutil.c
    ../lib/signal.c
    ../lib/metrics/protocol.c
    ../lib/metrics/export.c
    ../lib/xml_util.c
    ../lib/ioutil.c
    ../lib/net/multicast.c
    ../lib/net/netconnect.c
    ../lib/net/dvbstp.c
    ../lib/demux/crc32.c
    ../lib/tva/bcg_doc.c
    ../lib/tva/tva_xml.c
    ../lib/tva/mapping.c
    ../lib/tva/xmltv.c
    ../lib/tva/timefmt.c
    ../lib/bim/bitwriter.c
    ../lib/bim/bitreader.c
    ../lib/bim/strrepo.c
    ../lib/bim/codec.c
    ../lib/bim/fragment.c
    ../lib/bim/accessunit.c)
