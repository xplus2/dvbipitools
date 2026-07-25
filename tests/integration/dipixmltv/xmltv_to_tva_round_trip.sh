#!/bin/sh
# Copyright 2026 dvbipitools authors. Licensed under GPL-3.0-or-later.
# See NOTICE and LICENSE for details and authorship information.

BIN=$1
. "$(dirname "$0")/../common.sh"

cat > "$WORK/in.xmltv" <<'EOF'
<?xml version="1.0" encoding="UTF-8"?>
<tv>
  <channel id="orf1">
    <display-name>ORFeins</display-name>
  </channel>
  <programme start="20201215120000 +0000" channel="orf1">
    <title>News</title>
    <desc>Evening news</desc>
    <category>News</category>
  </programme>
</tv>
EOF

cat > "$WORK/map.csv" <<'EOF'
orf1,rtp://239.1.1.1:5000,1,2,101
EOF

run_expect_rc 0 "xmltv->tva" "$BIN" -f xmltv -M "$WORK/map.csv" -i "$WORK/in.xmltv" -o "$WORK/out.tva.xml"
assert_contains "$WORK/out.tva.xml" "ORFeins" "xmltv->tva"
assert_contains "$WORK/out.tva.xml" "News" "xmltv->tva"
assert_contains "$WORK/out.tva.xml" "rtp://239.1.1.1:5000" "xmltv->tva"

echo "OK"

#EOF