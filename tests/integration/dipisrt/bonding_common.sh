# Copyright 2026 dvbipitools authors. Licensed under GPL-3.0-or-later.
# See NOTICE and LICENSE for details and authorship information.

# sourced by dipisrt bonding_*.sh scripts, not run directly. caller sources
# ../common.sh (sets $WORK, $BIN, fail()) before sourcing this.

TC=/usr/sbin/tc
[ -x "$TC" ] || TC=/sbin/tc
[ -x "$TC" ] || TC=tc

netem_available() {
    "$TC" qdisc add dev lo root handle 9999: netem delay 1ms >/dev/null 2>&1 || return 1
    "$TC" qdisc del dev lo root >/dev/null 2>&1
    return 0
}

# doomed-fast group-mode call: "group create failed" (no build-time gate) or the gate's own
# rejection message both mean this libsrt has no bonding support
bonding_available() {
    probe=$("$BIN" -i /dev/null -o "srt://127.0.0.1:1" -o "srt://127.0.0.1:2" --group-mode broadcast 2>&1)
    case "$probe" in
        *"needs a libsrt built with bonding support"*|*"group create failed"*) return 1 ;;
    esac
    return 0
}

skip_unless_bonding_testable() {
    if ! netem_available; then
        echo "SKIP: no CAP_NET_ADMIN / tc unavailable, cannot inject network impairment"
        exit 0
    fi
    if ! bonding_available; then
        echo "SKIP: this libsrt build has no bonding support (ENABLE_BONDING=OFF)"
        exit 0
    fi
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
}

# net_setup <portA> <netem-args...>: portA gets the netem discipline, portB (and everything
# else) stays clean. always call net_teardown before exiting, impaired or not
net_setup() {
    port=$1
    shift
    "$TC" qdisc add dev lo root handle 1: prio bands 3
    "$TC" qdisc add dev lo parent 1:1 handle 10: netem "$@"
    "$TC" filter add dev lo parent 1: protocol ip u32 match ip dport "$port" 0xffff flowid 1:1
    "$TC" filter add dev lo parent 1: protocol ip u32 match ip sport "$port" 0xffff flowid 1:1
}

net_change() {
    shift
    "$TC" qdisc change dev lo parent 1:1 handle 10: netem "$@"
}

# net_setup2 <portA> <netem-args-A> <portB> <netem-args-B>: both bonded links impaired
# at once, independently. args strings are word-split, so quote each as one $2/$4 arg
net_setup2() {
    portA=$1
    argsA=$2
    portB=$3
    argsB=$4
    "$TC" qdisc add dev lo root handle 1: prio bands 3
    "$TC" qdisc add dev lo parent 1:1 handle 10: netem $argsA
    "$TC" qdisc add dev lo parent 1:2 handle 20: netem $argsB
    "$TC" filter add dev lo parent 1: protocol ip u32 match ip dport "$portA" 0xffff flowid 1:1
    "$TC" filter add dev lo parent 1: protocol ip u32 match ip sport "$portA" 0xffff flowid 1:1
    "$TC" filter add dev lo parent 1: protocol ip u32 match ip dport "$portB" 0xffff flowid 1:2
    "$TC" filter add dev lo parent 1: protocol ip u32 match ip sport "$portB" 0xffff flowid 1:2
}

# net_change2 A|B <netem-args>: retune one of the two links set up by net_setup2
net_change2() {
    if [ "$1" = A ]; then
        "$TC" qdisc change dev lo parent 1:1 handle 10: netem $2
    else
        "$TC" qdisc change dev lo parent 1:2 handle 20: netem $2
    fi
}

net_teardown() {
    "$TC" qdisc del dev lo root >/dev/null 2>&1
}

#EOF
