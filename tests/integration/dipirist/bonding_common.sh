# Copyright 2026 dvbipitools authors. Licensed under GPL-3.0-or-later.
# See NOTICE and LICENSE for details and authorship information.

# sourced by dipirist bonding_*.sh scripts, not run directly. caller sources
# ../common.sh (sets $WORK, $BIN, fail()) before sourcing this.

TC=/usr/sbin/tc
[ -x "$TC" ] || TC=/sbin/tc
[ -x "$TC" ] || TC=tc

netem_available() {
    "$TC" qdisc add dev lo root handle 9999: netem delay 1ms >/dev/null 2>&1 || return 1
    "$TC" qdisc del dev lo root >/dev/null 2>&1
    return 0
}

skip_unless_bonding_testable() {
    if ! netem_available; then
        echo "SKIP: no CAP_NET_ADMIN / tc unavailable, cannot inject network impairment"
        exit 0
    fi
    return 0
}

make_fixture() {
    path=$1
    n_pkts=$2
    : > "$path"
    i=0
    while [ "$i" -lt "$n_pkts" ]; do
        printf '\107' >> "$path"
        dd if=/dev/urandom bs=187 count=1 2>/dev/null >> "$path"
        i=$((i + 1))
    done
    return 0
}

net_setup() {
    port=$1
    shift
    "$TC" qdisc add dev lo root handle 1: prio bands 3
    "$TC" qdisc add dev lo parent 1:1 handle 10: netem "$@"
    "$TC" filter add dev lo parent 1: protocol ip u32 match ip dport "$port" 0xffff flowid 1:1
    "$TC" filter add dev lo parent 1: protocol ip u32 match ip sport "$port" 0xffff flowid 1:1
    return 0
}

net_change() {
    shift
    "$TC" qdisc change dev lo parent 1:1 handle 10: netem "$@"
    return 0
}

net_teardown() {
    "$TC" qdisc del dev lo root >/dev/null 2>&1
    return 0
}

#EOF
