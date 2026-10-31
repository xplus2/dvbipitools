#!/bin/sh
# Copyright 2026 dvbipitools authors. Licensed under GPL-3.0-or-later.
# See NOTICE and LICENSE for details and authorship information.

# maintenance/release helper
# stamp @DATE@ and @VERSION@ in man pages
# usage: stamp_man.sh <src/tool/tool.1> <out>
set -e

die() {
	echo "stamp_man.sh: $*" >&2
	exit 1
}

[ $# -eq 2 ] || die "usage: $0 <src/tool/tool.1> <out>"
PAGE="$1"
OUT="$2"

HDR="$(dirname "$PAGE")/version.h"
VERSION="$(sed -n 's/^#define TOOL_VERSION "\(.*\)"$/\1/p' "$HDR" 2>/dev/null)" || VERSION=""
[ -n "$VERSION" ] || die "no TOOL_VERSION in $HDR"

if [ -n "${SOURCE_DATE_EPOCH:-}" ]; then
	DATE="$(date -u -d "@$SOURCE_DATE_EPOCH" +%F)"
else
	DATE="$(date -u +%F)"
fi

mkdir -p "$(dirname "$OUT")"
sed -e "s/@DATE@/$DATE/g" -e "s/@VERSION@/$VERSION/g" "$PAGE" > "$OUT"
chmod 0644 "$OUT"

#EOF
