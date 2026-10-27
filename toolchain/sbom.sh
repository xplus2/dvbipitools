#!/bin/sh
# Copyright 2026 dvbipitools authors. Licensed under GPL-3.0-or-later.
# See NOTICE and LICENSE for details and authorship information.

# CycloneDX SBOM for static binaries from ld -Map files
# usage: sbom.sh -o <out.json> -c <cc> [-f <cppflags>] <binary>...
set -e

usage() {
	echo "usage: $0 -o <out.json> -c <cc> [-f <cppflags>] <binary>..." >&2
	exit 1
}

die() {
	echo "sbom.sh: $*" >&2
	exit 1
}

OUT=""
CC_CMD=""
CPPF=""
while getopts o:c:f: opt; do
	case "$opt" in
	o) OUT="$OPTARG" ;;
	c) CC_CMD="$OPTARG" ;;
	f) CPPF="$OPTARG" ;;
	*) usage ;;
	esac
done
shift $((OPTIND - 1))
[ -n "$OUT" ] && [ -n "$CC_CMD" ] && [ $# -gt 0 ] || usage

SRC="$(cd "$(dirname "$0")/.." && pwd)"
WORK="$(mktemp -d)"
trap 'rm -rf "$WORK"' EXIT

# tool_version <tool>: TOOL_VERSION from src/<tool>/version.h
tool_version() {
	v="$(sed -n 's/^#define TOOL_VERSION "\(.*\)"$/\1/p' "$SRC/src/$1/version.h" 2>/dev/null)" || v=""
	[ -n "$v" ] || die "no TOOL_VERSION in src/$1/version.h"
	echo "$v"
}

VERSION="$(tool_version dvbipitools)"
TARGET="$($CC_CMD -dumpmachine)"
PKGC="${PKG_CONFIG:-pkg-config}"

# cross gcc lacks /usr/include, idirafter keeps sysroot first
IDA=""
MULTIARCH="$($CC_CMD -print-multiarch 2>/dev/null || true)"
[ -d /usr/include ] && IDA="-idirafter /usr/include"
[ -n "$MULTIARCH" ] && [ -d "/usr/include/$MULTIARCH" ] && IDA="$IDA -idirafter /usr/include/$MULTIARCH"

# map to component ids, archives count only if ld pulled members
MAPAWK='
function lib(n) {
	# amd64 libm.a is ld script for libm-<ver>.a
	if (n ~ /^lib(c|m|pthread|dl|rt|resolv|mvec|util|anl)(-[0-9.]+)?$/) return "glibc"
	if (n ~ /^lib(gcc|gcc_eh|atomic|atomic_asneeded|stdc\+\+|supc\+\+)$/) return "gcc"
	if (n ~ /^lib(ssl|crypto)$/) return "openssl"
	if (n == "libz") return "zlib"
	if (n == "libzstd") return "zstd"
	if (n == "libjitterentropy") return "jitterentropy"
	if (n == "librist") return "librist"
	if (n == "libcjson") return "cjson"
	if (n == "libsrt") return "srt"
	if (n ~ /^lib(mbedtls|mbedx509|mbedcrypto|tfpsacrypto)$/) return "mbedtls"
	if (n ~ /^libngtcp2(_crypto_[a-z0-9]+)?$/) return "ngtcp2"
	if (n == "libnghttp3") return "nghttp3"
	if (n == "libnghttp2") return "nghttp2"
	return ""
}
/^Archive member included/ { sec = 1; next }
# ld notes on aarch64 gnu property merge
/^(Removed|Updated) property / { next }
/^(Discarded input sections|Allocating common symbols|As-needed library included|Memory Configuration)/ { sec = 0 }
sec && /^[^ \t].*\.a\(/ {
	p = $0; sub(/\.a\(.*/, ".a", p)
	m = $0; sub(/^[^(]*\.a\(/, "", m); sub(/\).*/, "", m)
	n = p; sub(/.*\//, "", n); sub(/\.a$/, "", n)
	c = lib(n)
	if (c == "") { print "unknown archive " p > "/dev/stderr"; bad = 1; next }
	seen[c] = 1
	if (c == "librist" && tolower(m) ~ /cjson/) seen["cjson-bundled"] = 1
	d = p; sub(/\/[^\/]*$/, "", d)
	print c "\t" d > dirs
	next
}
/^LOAD / {
	p = $2
	if (p !~ /\.o$/) next
	n = p; sub(/.*\//, "", n)
	if (n ~ /^[grS]?crt1\.o$/ || n ~ /^crt[in]\.o$/) seen["glibc"] = 1
	else if (n ~ /^crt(begin|end)/) seen["gcc"] = 1
	else if (p ~ /vendor\/libyaml\//) seen["libyaml"] = 1
	else if (p ~ /vendor\/picohttpparser\//) seen["picohttpparser"] = 1
	else if (p ~ /^\//) { print "unknown object " p > "/dev/stderr"; bad = 1 }
}
END {
	for (c in seen) print c
	exit bad
}'

: > "$WORK/all"
: > "$WORK/dirs"
for bin in "$@"; do
	[ -f "$bin" ] || die "$bin: no such binary"
	[ -f "$bin.map" ] || die "$bin.map: no linker map"
	name="$(basename "$bin")"
	tool_version "$name" > "$WORK/$name.ver"
	awk -v dirs="$WORK/dirs" "$MAPAWK" "$bin.map" > "$WORK/raw" || die "$bin.map: unclassified input"
	sort "$WORK/raw" > "$WORK/$name.deps"
	cat "$WORK/$name.deps" >> "$WORK/all"
done
sort -u "$WORK/all" > "$WORK/comps"

# headers next to linked archive first: <prefix>/lib or <prefix>/lib/<multiarch>
archive_incs() {
	awk -F '\t' -v c="$1" '$1 == c && !s[$2]++ { print $2 }' "$WORK/dirs" | while read -r d; do
		for i in "$d/../include" "$d/../../include"; do
			[ -d "$i" ] && printf ' -I%s' "$i"
		done
	done
}

# macros <component> <header>
macros() {
	printf '#include <%s>\n' "$2" | $CC_CMD $(archive_incs "$1") $CPPF $IDA -E -dM -x c - 2>/dev/null
}

macro() {
	awk -v m="$2" '$1 == "#define" && $2 == m { $1 = ""; $2 = ""; sub(/^ +/, ""); gsub(/"/, ""); print; exit }' "$1"
}

triple() {
	a="$(macro "$1" "$2")"
	b="$(macro "$1" "$3")"
	c="$(macro "$1" "$4")"
	[ -n "$a" ] && [ -n "$b" ] && [ -n "$c" ] && echo "$a.$b.$c"
}

version_of() {
	case "$1" in
	glibc)
		macros glibc features.h > "$WORK/m" || return 0
		a="$(macro "$WORK/m" __GLIBC__)"
		b="$(macro "$WORK/m" __GLIBC_MINOR__)"
		[ -n "$a" ] && [ -n "$b" ] && echo "$a.$b"
		;;
	gcc) $CC_CMD -dumpfullversion 2>/dev/null || $CC_CMD -dumpversion ;;
	openssl)
		macros openssl openssl/opensslv.h > "$WORK/m" || return 0
		v="$(macro "$WORK/m" OPENSSL_VERSION_STR)"
		[ -n "$v" ] || v="$(macro "$WORK/m" OPENSSL_VERSION_TEXT | awk '{ print $2 }')"
		echo "$v"
		;;
	zlib)
		macros zlib zlib.h > "$WORK/m" || return 0
		macro "$WORK/m" ZLIB_VERSION
		;;
	zstd)
		macros zstd zstd.h > "$WORK/m" || return 0
		triple "$WORK/m" ZSTD_VERSION_MAJOR ZSTD_VERSION_MINOR ZSTD_VERSION_RELEASE
		;;
	jitterentropy)
		macros jitterentropy jitterentropy.h > "$WORK/m" || return 0
		triple "$WORK/m" JENT_MAJVERSION JENT_MINVERSION JENT_PATCHLEVEL
		;;
	mbedtls)
		macros mbedtls mbedtls/version.h > "$WORK/m" || return 0
		macro "$WORK/m" MBEDTLS_VERSION_STRING
		;;
	librist) $PKGC --modversion librist 2>/dev/null ;;
	srt) $PKGC --modversion srt 2>/dev/null ;;
	ngtcp2) $PKGC --modversion libngtcp2 2>/dev/null ;;
	nghttp3) $PKGC --modversion libnghttp3 2>/dev/null ;;
	nghttp2) $PKGC --modversion libnghttp2 2>/dev/null ;;
	cjson) $PKGC --modversion libcjson 2>/dev/null ;;
	libyaml) sed -n '1s/^libyaml //p' "$SRC/src/lib/vendor/libyaml/README.md" ;;
	esac
}

# id to name|license|website
meta() {
	case "$1" in
	glibc) echo "glibc|LGPL-2.1-or-later|https://www.gnu.org/software/libc/" ;;
	gcc) echo "gcc-runtime|GPL-3.0-or-later WITH GCC-exception-3.1|https://gcc.gnu.org/" ;;
	openssl) echo "openssl|Apache-2.0|https://www.openssl.org/" ;;
	zlib) echo "zlib|Zlib|https://zlib.net/" ;;
	zstd) echo "zstd|BSD-3-Clause OR GPL-2.0-only|https://github.com/facebook/zstd" ;;
	jitterentropy) echo "jitterentropy|BSD-3-Clause OR GPL-2.0-only|https://github.com/smuellerDD/jitterentropy-library" ;;
	mbedtls) echo "mbedtls|Apache-2.0 OR GPL-2.0-or-later|https://www.trustedfirmware.org/projects/mbed-tls/" ;;
	librist) echo "librist|BSD-2-Clause|https://code.videolan.org/rist/librist" ;;
	cjson | cjson-bundled) echo "cjson|MIT|https://github.com/DaveGamble/cJSON" ;;
	srt) echo "srt|MPL-2.0|https://github.com/Haivision/srt" ;;
	ngtcp2) echo "ngtcp2|MIT|https://github.com/ngtcp2/ngtcp2" ;;
	nghttp3) echo "nghttp3|MIT|https://github.com/ngtcp2/nghttp3" ;;
	nghttp2) echo "nghttp2|MIT|https://github.com/nghttp2/nghttp2" ;;
	libyaml) echo "libyaml|MIT|https://github.com/yaml/libyaml" ;;
	picohttpparser) echo "picohttpparser|MIT|https://github.com/h2o/picohttpparser" ;;
	esac
}

# no upstream version: cJSON inside librist, picohttpparser
needs_version() {
	case "$1" in
	picohttpparser | cjson-bundled) return 1 ;;
	*) return 0 ;;
	esac
}

jstr() {
	printf '"%s"' "$(printf '%s' "$1" | sed -e 's/\\/\\\\/g' -e 's/"/\\"/g')"
}

library() {
	m="$(meta "$1")"
	name="${m%%|*}"
	rest="${m#*|}"
	lic="${rest%%|*}"
	url="${rest#*|}"
	v="$(version_of "$1")" || v=""
	if [ -z "$v" ] && needs_version "$1"; then
		die "$1: linked, but version not determinable"
	fi
	printf '%s{"type": "library", "bom-ref": %s, "name": %s' "$2" "$(jstr "$1")" "$(jstr "$name")"
	if [ -n "$v" ]; then
		printf ', "version": %s, "purl": %s' "$(jstr "$v")" "$(jstr "pkg:generic/$name@$v")"
	else
		printf ', "purl": %s' "$(jstr "pkg:generic/$name")"
	fi
	printf ', "licenses": [{"expression": %s}]' "$(jstr "$lic")"
	printf ', "externalReferences": [{"type": "website", "url": %s}]' "$(jstr "$url")"
	if [ "$1" = librist ] && grep -qx cjson-bundled "$WORK/comps"; then
		printf ', "components": [\n'
		library cjson-bundled "        "
		printf '\n      ]'
	fi
	printf '}'
}

{
	printf '{\n  "bomFormat": "CycloneDX",\n  "specVersion": "1.6",\n  "version": 1,\n'
	printf '  "metadata": {\n'
	printf '    "component": {"type": "application", "bom-ref": "dvbipitools", "name": "dvbipitools", "version": %s, ' "$(jstr "$VERSION")"
	printf '"purl": %s, "licenses": [{"expression": "GPL-3.0-or-later"}]},\n' "$(jstr "pkg:generic/dvbipitools@$VERSION")"
	printf '    "properties": [{"name": "dvbipitools:target", "value": %s}, {"name": "dvbipitools:link", "value": "static"}]\n' "$(jstr "$TARGET")"
	printf '  },\n  "components": [\n'
	sep=""
	for bin in "$@"; do
		name="$(basename "$bin")"
		sum="$(sha256sum "$bin" | cut -d' ' -f1)"
		printf '%s    {"type": "application", "bom-ref": %s, "name": %s, "version": %s, ' "$sep" "$(jstr "bin:$name")" "$(jstr "$name")" "$(jstr "$(cat "$WORK/$name.ver")")"
		printf '"hashes": [{"alg": "SHA-256", "content": "%s"}]}' "$sum"
		sep=",
"
	done
	while read -r c; do
		[ "$c" = cjson-bundled ] && continue
		printf '%s' "$sep"
		library "$c" "    "
	done < "$WORK/comps"
	printf '\n  ],\n  "dependencies": [\n'
	printf '    {"ref": "dvbipitools", "dependsOn": ['
	sep=""
	for bin in "$@"; do
		printf '%s%s' "$sep" "$(jstr "bin:$(basename "$bin")")"
		sep=", "
	done
	printf ']}'
	for bin in "$@"; do
		name="$(basename "$bin")"
		printf ',\n    {"ref": %s, "dependsOn": [' "$(jstr "bin:$name")"
		sep=""
		while read -r c; do
			[ "$c" = cjson-bundled ] && continue
			printf '%s%s' "$sep" "$(jstr "$c")"
			sep=", "
		done < "$WORK/$name.deps"
		printf ']}'
	done
	printf '\n  ]\n}\n'
} > "$WORK/out"

mv "$WORK/out" "$OUT"
