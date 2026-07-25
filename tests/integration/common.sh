# Copyright 2026 dvbipitools authors. Licensed under GPL-3.0-or-later.
# See NOTICE and LICENSE for details and authorship information.

# sourced by integration test scripts, not run directly
# caller sets BIN from $1 before sourcing

if [ -z "${BIN:-}" ] || [ ! -x "$BIN" ]; then
    echo "FAIL: no executable binary given as \$1 ($BIN)" >&2
    exit 1
fi

WORK=$(mktemp -d)
trap 'rm -rf "$WORK"' EXIT

fail() {
    echo "FAIL: $*" >&2
    exit 1
}

# run_expect_rc <expected-rc> <label> -- rest of the line runs
run_expect_rc() {
    want=$1
    label=$2
    shift 2
    "$@"
    got=$?
    if [ "$got" != "$want" ]; then
        fail "$label: expected exit $want, got $got"
    fi
}

assert_contains() {
    file=$1
    pattern=$2
    label=$3
    if ! grep -q -- "$pattern" "$file"; then
        fail "$label: expected to find '$pattern' in $file"
    fi
}

assert_not_contains() {
    file=$1
    pattern=$2
    label=$3
    if grep -q -- "$pattern" "$file"; then
        fail "$label: did not expect to find '$pattern' in $file"
    fi
}

#EOF
