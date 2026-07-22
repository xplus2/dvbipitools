# Copyright 2026 dvbipitools authors. Licensed under GPL-3.0-or-later.
# See NOTICE and LICENSE for details and authorship information.

# keep in sync with CMakeLists/Makefile
add_executable(dipixmltv
    main.c
    args.c
    revmap.c
    suggest.c
    ../lib/log.c
    ../lib/xml_util.c
    ../lib/ioutil.c
    ../lib/tva/epg_doc.c
    ../lib/tva/tva_xml.c
    ../lib/tva/mapping.c
    ../lib/tva/xmltv.c
    ../lib/tva/timefmt.c)
