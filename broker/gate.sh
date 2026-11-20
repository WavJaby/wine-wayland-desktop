#!/bin/bash
# Broker gate: Wine launches from separate `flatpak run` instances (= sibling pid namespaces) against one test prefix.
# Usage: gate.sh [control|cross|concurrent|signals|bottles|all]   (default all). Prints PASS/FAIL lines; logs in $P/gate-logs/.
#   control     unbrokered runner: second instance while the first runs (expect it NOT to reach the first's server)
#   cross       brokered runner: A long probe; B short probe (stdout + exit 7), winepath, reg, wineserver -w; one pid ns
#   concurrent  brokered runner: two cold first launches at once -> one broker, one wineserver, both succeed
#   signals     brokered runner: SIGINT to a proxied caller ends only its child; wineserver -k from another ns ends all
# Touches only $P (test prefix, proto runner) and /tmp/.wine-<uid>/{server,broker}-<key of that prefix>.
set -uo pipefail
P=$HOME/.var/app/com.usebottles.bottles/data/broker-proto
# These gates run Wine, create files and kill wineservers in the named bottle: point them only at a disposable test bottle, never one in daily use.
# BROKER_GATE_BOTTLE = bottle name, BROKER_GATE_RUNNER = brokered runner dir name, BROKER_GATE_STOCK_RUNNER = unbrokered runner (only for `control`).
BOTTLE=${BROKER_GATE_BOTTLE:?set BROKER_GATE_BOTTLE to a disposable test bottle}
PROTO_NAME=${BROKER_GATE_RUNNER:?set BROKER_GATE_RUNNER to the brokered runner directory name}
BOTTLES_DATA=$HOME/.var/app/com.usebottles.bottles/data/bottles
PREFIX=$BOTTLES_DATA/bottles/$BOTTLE
PROTO=$BOTTLES_DATA/runners/$PROTO_NAME/bin
STOCK=$BOTTLES_DATA/runners/${BROKER_GATE_STOCK_RUNNER:-unset}/bin
L=$P/gate-logs
mkdir -p "$L"
fails=0
pass() { echo "PASS $*"; }
fail() { echo "FAIL $*"; fails=$((fails + 1)); }

# fresh sandbox instance; $1 = runner bin dir, rest = command line run inside it
fr() {
    local bin=$1; shift
    flatpak run --command=sh --env=WINEPREFIX="$PREFIX" --env=WINEDEBUG=-all --env=DISPLAY= \
        --env=WINEDLLOVERRIDES=mscoree,mshtml=d --env=B="$bin" com.usebottles.bottles -c "$*" 2>/dev/null
}
# host view: pid namespace of every Wine process using the test prefix, one line per process. python3 = the broker
# (owner ns) or a waiting caller proxy (caller ns, by design) -> not Wine, skipped.
prefix_ns() {
    for p in $(pgrep -u "$(id -u)"); do
        grep -qsaz "^WINEPREFIX=$PREFIX/\?\$" /proc/$p/environ 2>/dev/null || continue  # Bottles may add a slash
        case $(cat /proc/$p/comm 2>/dev/null) in sh|flatpak*|bwrap|python3|timeout|wine-proxy|wine-broker) continue;; esac
        echo "$(readlink /proc/$p/ns/pid) $(cat /proc/$p/comm)"
    done | sort
}
wait_line() { for _ in $(seq 1 ${3:-60}); do grep -q "$2" "$1" 2>/dev/null && return 0; sleep 0.5; done; return 1; }
# The C broker has its own comm name; proxies are excluded from Wine namespace counts.
broker_alive() {
    local pid
    for pid in $(pgrep -u "$(id -u)" -x wine-broker); do
        grep -qsaz "^WINEPREFIX=$PREFIX/\?\$" /proc/$pid/environ 2>/dev/null && return 0
    done
    return 1
}
wait_quiet() { for _ in $(seq 1 ${1:-60}); do [ -z "$(prefix_ns)" ] && return 0; sleep 1; done; return 1; }

control() {
    wait_quiet 30 || { fail "control: prefix busy before start"; return; }
    fr "$STOCK" '$B/wine C:\\nsprobe.exe 12 0; echo rc=$?' >"$L/ctl-A.log" &
    wait_line "$L/ctl-A.log" "winpid=" || { fail "control: A did not start"; return; }
    fr "$STOCK" 'timeout 20 $B/wine C:\\nsprobe.exe 0 7; echo rc=$?' >"$L/ctl-B.log"
    wait
    grep -q "rc=0" "$L/ctl-A.log" && pass "control: A survived" || fail "control: A died ($(tail -1 "$L/ctl-A.log"))"
    grep -q "nsprobe done" "$L/ctl-B.log" && fail "control: B ran (expected refusal; guard missing?)" \
        || pass "control: B refused cross-namespace ($(tail -1 "$L/ctl-B.log"))"
}

cross() {
    wait_quiet 30 || { fail "cross: prefix busy before start"; return; }
    fr "$PROTO" '$B/wine C:\\nsprobe.exe 25 0; echo rc=$?' >"$L/x-A.log" &
    wait_line "$L/x-A.log" "winpid=" || { fail "cross: A did not start"; return; }
    fr "$PROTO" '$B/wine C:\\nsprobe.exe 1 7; echo rc=$?' >"$L/x-B.log"
    grep -q "nsprobe done" "$L/x-B.log" && grep -q "rc=7" "$L/x-B.log" && pass "cross: B output + exit 7" \
        || fail "cross: B ($(tr '\n' '|' <"$L/x-B.log"))"
    fr "$PROTO" '$B/winepath -w /tmp; echo rc=$?' >"$L/x-path.log"
    grep -qi '^z:\\tmp' "$L/x-path.log" && grep -q rc=0 "$L/x-path.log" && pass "cross: winepath" \
        || fail "cross: winepath ($(tr '\n' '|' <"$L/x-path.log"))"
    fr "$PROTO" '$B/wine reg query "HKCU\\Software\\Wine" >/dev/null; echo rc=$?' >"$L/x-reg.log"
    grep -q rc=0 "$L/x-reg.log" && pass "cross: reg query" || fail "cross: reg ($(cat "$L/x-reg.log"))"
    broker_alive && pass "cross: broker visible (idle probe responds)" || fail "cross: broker not visible to broker_alive"
    local ns; ns=$(prefix_ns); echo "$ns" >"$L/x-ns.txt"
    [ "$(cut -d' ' -f1 <<<"$ns" | sort -u | wc -l)" = 1 ] && pass "cross: one pid ns ($(tr '\n' ' ' <<<"$ns"))" \
        || fail "cross: several pid ns: $(tr '\n' ' ' <<<"$ns")"
    local t0=$SECONDS
    fr "$PROTO" '$B/wineserver -w; echo rc=$?' >"$L/x-w.log"
    grep -q "rc=0" "$L/x-A.log" 2>/dev/null || wait
    grep -q "rc=0" "$L/x-A.log" && pass "cross: A survived to its normal exit" || fail "cross: A ($(tail -1 "$L/x-A.log"))"
    [ $((SECONDS - t0)) -ge 5 ] && grep -q rc=0 "$L/x-w.log" && pass "cross: wineserver -w waited $((SECONDS - t0))s" \
        || fail "cross: wineserver -w returned after $((SECONDS - t0))s ($(cat "$L/x-w.log"))"
    wait
}

concurrent() {
    wait_quiet 30 || { fail "concurrent: prefix busy before start"; return; }
    broker_alive && { fail "concurrent: broker still alive (no idle exit)"; return; }
    local key; key=$(stat -c '%d %i' "$PREFIX" | awk '{printf "%x-%x", $1, $2}')
    local blog=$PREFIX/.wine-wayland-broker/broker-c2.log
    local before; before=$(fr "$PROTO" "grep -c 'broker up' $blog 2>/dev/null || echo 0")
    fr "$PROTO" '$B/wine C:\\nsprobe.exe 6 0; echo rc=$?' >"$L/c-1.log" &
    fr "$PROTO" '$B/wine C:\\nsprobe.exe 6 0; echo rc=$?' >"$L/c-2.log" &
    wait_line "$L/c-1.log" "winpid=" && wait_line "$L/c-2.log" "winpid="
    local ns; ns=$(prefix_ns)
    wait
    local after; after=$(fr "$PROTO" "grep -c 'broker up' $blog")
    grep -q rc=0 "$L/c-1.log" && grep -q rc=0 "$L/c-2.log" && pass "concurrent: both rc=0" \
        || fail "concurrent: $(tail -1 "$L/c-1.log") / $(tail -1 "$L/c-2.log")"
    [ $((after - before)) = 1 ] && pass "concurrent: one election" || fail "concurrent: $((after - before)) elections"
    [ "$(cut -d' ' -f1 <<<"$ns" | sort -u | wc -l)" = 1 ] && [ "$(grep -Ec ' wineserver(-real)?$' <<<"$ns")" = 1 ] \
        && [ "$(grep -c ' nsprobe.exe$' <<<"$ns")" = 2 ] \
        && pass "concurrent: one ns, one wineserver, two probes" || fail "concurrent: $(tr '\n' ' ' <<<"$ns")"
}

idle() {
    wait_quiet 30
    for _ in $(seq 1 40); do broker_alive || break; sleep 1; done
    broker_alive && fail "idle: broker still alive" || pass "idle: broker exited"
}

# A long probe owns the broker; B's proxied probe gets SIGINT via `timeout` -> must end early, A untouched;
# then C's `wineserver -k` must end A long before its 40 s.
signals() {
    wait_quiet 30 || { fail "signals: prefix busy before start"; return; }
    fr "$PROTO" '$B/wine C:\\nsprobe.exe 40 0; echo rc=$?' >"$L/s-A.log" &
    wait_line "$L/s-A.log" "winpid=" || { fail "signals: A did not start"; return; }
    local t0=$SECONDS
    fr "$PROTO" 'timeout -s INT 3 $B/wine C:\\nsprobe.exe 30 0; echo rc=$?' >"$L/s-B.log"
    local dt=$((SECONDS - t0))
    [ $dt -lt 15 ] && ! grep -q "nsprobe done" "$L/s-B.log" && pass "signals: SIGINT ended B after ${dt}s ($(tail -1 "$L/s-B.log"))" \
        || fail "signals: B ran ${dt}s ($(tr '\n' '|' <"$L/s-B.log"))"
    [ "$(prefix_ns | grep -c ' nsprobe.exe$')" = 1 ] && pass "signals: A still running" || fail "signals: probes: $(prefix_ns | tr '\n' ' ')"
    t0=$SECONDS
    fr "$PROTO" '$B/wineserver -k; echo rc=$?' >"$L/s-k.log"
    wait
    dt=$((SECONDS - t0))
    [ $dt -lt 20 ] && ! grep -q "nsprobe done" "$L/s-A.log" && pass "signals: wineserver -k ended A after ${dt}s" \
        || fail "signals: -k took ${dt}s ($(tr '\n' '|' <"$L/s-A.log") / $(cat "$L/s-k.log"))"
}

# Through Bottles itself (program NsProbe, registered in the test bottle beforehand, args "40 0"):
#   desk = the exact Exec line Bottles writes into a generated desktop entry (fresh instance per launch);
#   gui  = bottles-cli run via `flatpak enter` into a separate long-lived instance -> the same WineCommand path the GUI
#          runs in-process, in a namespace that is neither caller's. Not a real GUI click: that stays a user step.
desk() { flatpak run --command=bottles-cli com.usebottles.bottles run -p NsProbe -b "$BOTTLE" -- >/dev/null 2>&1; }
gui_holder() {
    local before; before=$(flatpak ps --columns=instance | sort)
    flatpak run --command=sleep com.usebottles.bottles 120 >/dev/null 2>&1 &
    for _ in $(seq 1 20); do
        local inst; inst=$(comm -13 <(echo "$before") <(flatpak ps --columns=instance | sort) | head -1)
        [ -n "$inst" ] && { echo "$inst"; return 0; }
        sleep 0.5
    done
    return 1
}
# flatpak enter does not apply the app's environment (bare `bottles-cli` -> "No such file"; with PATH only -> Xdp typelib
# ImportError), so copy the holder's own process environment (child of the instance's bwrap) verbatim.
gui() {
    local child; child=$(flatpak ps --columns=instance,child-pid | awk -v i="$1" '$1 == i {print $2}')
    local app; app=$(pgrep -P "$child" | head -1)
    xargs -0 -a "/proc/$app/environ" sh -c 'i=$1; shift; exec flatpak enter "$i" env -i "$@" /app/bin/bottles-cli run -p NsProbe -b "$0" --' \
        "$BOTTLE" "$1" >"$L/gui-enter.log" 2>&1
}
probes_one_ns() {  # $1 label, $2 expected nsprobe count
    local ns; ns=$(prefix_ns); echo "$ns" >"$L/$1-ns.txt"
    [ "$(cut -d' ' -f1 <<<"$ns" | sort -u | wc -l)" = 1 ] && [ "$(grep -c ' nsprobe.exe$' <<<"$ns")" = "$2" ] \
        && [ "$(grep -Ec ' wineserver(-real)?$' <<<"$ns")" = 1 ] && pass "$1: $2 probes, one ns, one wineserver" \
        || fail "$1: $(tr '\n' ' ' <<<"$ns")"
}
bottles() {
    for order in desk-desk gui-desk desk-gui; do
        wait_quiet 60 || { fail "$order: prefix busy before start"; return; }
        for _ in $(seq 1 40); do broker_alive || break; sleep 1; done
        local h=""
        case $order in *gui*) h=$(gui_holder) || { fail "$order: no holder instance"; continue; };; esac
        case $order in desk-*) desk & ;; gui-*) gui "$h" & ;; esac
        for _ in $(seq 1 60); do [ "$(prefix_ns | grep -c ' nsprobe.exe$')" -ge 1 ] && break; sleep 0.5; done
        case $order in *-desk) desk & ;; *-gui) gui "$h" & ;; esac
        for _ in $(seq 1 60); do [ "$(prefix_ns | grep -c ' nsprobe.exe$')" -ge 2 ] && break; sleep 0.5; done
        probes_one_ns "$order" 2
        if ! fr "$PROTO" '$B/wineserver -k' >/dev/null; then
            echo "NOTE $order: direct cleanup profile refused; waiting for probes to finish normally"
        fi
        wait
        [ -n "$h" ] && flatpak kill "$h" 2>/dev/null
    done
}

case ${1:-all} in
    control) control;; cross) cross; idle;; concurrent) concurrent; idle;; signals) signals; idle;; bottles) bottles; idle;;
    all) control; cross; idle; concurrent; idle; signals; idle; bottles; idle;;
    *) echo "unknown mode $1" >&2; exit 2;;
esac
echo "gate: $fails failure(s)"
exit $((fails > 0))
