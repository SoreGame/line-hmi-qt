#!/bin/sh
# Wait until DP-1 (or --output) is connected and 1920x1080 is the active mode.
set -eu

output=DP-1
wait_sec=30
log=/dev/null

while [ $# -gt 0 ]; do
    case "$1" in
        --output) output="${2:?}"; shift 2 ;;
        --wait) wait_sec="${2:?}"; shift 2 ;;
        --log) log="${2:?}"; shift 2 ;;
        *) echo "usage: $0 [--output DP-1] [--wait 30] [--log file]" >&2; exit 2 ;;
    esac
done

say() { echo "$(date '+%F %T') [$output] $*" >>"${log}"; }

output_connected() {
    xrandr --query 2>/dev/null | grep -q "^${output} connected"
}

mode_active() {
    xrandr --query 2>/dev/null | awk -v o="${output}" '
        $1 == o && $2 == "connected" { p = 1; next }
        p && $1 ~ /^[A-Za-z]/ { exit }
        p && $1 ~ /^1920x1080/ && index($0, "*") { found = 1 }
        END { exit found ? 0 : 1 }
    '
}

apply_mode() {
    output_connected || return 1
    if ! xrandr --output "${output}" --primary --mode 1920x1080 --rate 60 >>"${log}" 2>&1; then
        if ! xrandr --output "${output}" --primary --mode 1920x1080 >>"${log}" 2>&1; then
            xrandr --output "${output}" --primary --auto >>"${log}" 2>&1 || return 1
        fi
    fi
    mode_active
}

if mode_active; then
    say "already 1920x1080"
    exit 0
fi

deadline=$(( $(date +%s) + wait_sec ))
n=0
while [ "$(date +%s)" -lt "${deadline}" ]; do
    if apply_mode; then
        say "set 1920x1080 after ${n} tries"
        exit 0
    fi
    if output_connected; then
        say "connected but mode not set (try ${n})"
    else
        say "disconnected (try ${n})"
    fi
    n=$((n + 1))
    sleep 0.5
done

say "timeout after ${wait_sec}s — panel still not 1920x1080"
exit 1
