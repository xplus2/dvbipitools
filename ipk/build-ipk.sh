#!/bin/sh
# builds an opkg .ipk from a staged usr/bin tree
# usage: build-ipk.sh <staging-dir> <arch> <version> <output-ipk-path>
set -e

STAGING="$1"
ARCH="$2"
VERSION="$3"
OUT="$4"

if [ -z "$STAGING" ] || [ -z "$ARCH" ] || [ -z "$VERSION" ] || [ -z "$OUT" ]; then
	echo "usage: $0 <staging-dir> <arch> <version> <output-ipk-path>" >&2
	exit 1
fi

SCRIPT_DIR="$(cd "$(dirname "$0")" && pwd)"
WORK="$(mktemp -d)"
trap 'rm -rf "$WORK"' EXIT

SIZE_KB="$(du -sk "$STAGING" | cut -f1)"

mkdir -p "$WORK/control"
sed -e "s/@VERSION@/$VERSION/" -e "s/@ARCH@/$ARCH/" -e "s/@INSTALLED_SIZE@/$SIZE_KB/" \
	"$SCRIPT_DIR/control.template" > "$WORK/control/control"

tar -C "$WORK/control" -czf "$WORK/control.tar.gz" ./control
tar -C "$STAGING" -czf "$WORK/data.tar.gz" .
echo "2.0" > "$WORK/debian-binary"

mkdir -p "$(dirname "$OUT")"
rm -f "$OUT"
ar rc "$OUT" "$WORK/debian-binary" "$WORK/control.tar.gz" "$WORK/data.tar.gz"
