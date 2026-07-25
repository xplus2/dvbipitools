#!/bin/sh
# Copyright 2026 dvbipitools authors. Licensed under GPL-3.0-or-later.
# See NOTICE and LICENSE for details and authorship information.

BIN=$1
. "$(dirname "$0")/../common.sh"

cat > "$WORK/guide.xmltv" <<'EOF'
<?xml version="1.0" encoding="UTF-8"?>
<tv>
  <channel id="orf1"><display-name>ORFeins</display-name></channel>
  <channel id="mystery"><display-name>Mystery Channel</display-name></channel>
</tv>
EOF

cat > "$WORK/scan.csv" <<'EOF'
ORFeins,rtp://239.1.1.1:5000,1,2,101
EOF

run_expect_rc 0 "suggest" "$BIN" -S "$WORK/scan.csv" -i "$WORK/guide.xmltv" -o "$WORK/suggested.csv"
assert_contains "$WORK/suggested.csv" "orf1,rtp://239.1.1.1:5000,1,2,101" "suggest"
assert_contains "$WORK/suggested.csv" "UNMATCHED: mystery" "suggest"

echo "OK"

#EOF