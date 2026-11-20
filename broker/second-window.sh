#!/bin/bash
# Second GUI process in one prefix: start Notepad A, then Notepad B, then ask hungprobe.exe whether each window answers.
# Usage: second-window.sh <runner-name> <x11|wayland> [tag]   env SW_ENV='A=1 B=2' adds sandbox env (e.g. the bottle's)
# One plain `flatpak run` instance, no broker (calls the runner's real loader when the runner is brokered).
# The graphics driver is forced via HKCU\Software\Wine\Drivers Graphics: `flatpak run --env=DISPLAY=` does NOT clear
# DISPLAY (it stays :0), so without the key a run silently picks X11 or Wayland by the build's default order.
# Touches only bottle Wine-Wayland-Test. Log: data/broker-proto/gate-logs/second-<tag>.log
set -uo pipefail
runner=$1 drv=$2 tag=${3:-$1-$2}
D=$HOME/.var/app/com.usebottles.bottles/data
PFX=$D/bottles/bottles/Wine-Wayland-Test
BIN=$D/bottles/runners/$runner/bin
W=$BIN/wine; [ -x "$BIN/wine-real" ] && W=$BIN/wine-real
WS=$BIN/wineserver; [ -x "$BIN/wineserver-real" ] && WS=$BIN/wineserver-real
LOG=$D/broker-proto/gate-logs/second-$tag.log
flatpak run --command=sh --env=WINEPREFIX="$PFX" --env=WINEDEBUG=-all --env=WINEDLLOVERRIDES=mscoree,mshtml=d \
    $(for e in ${SW_ENV-}; do printf -- '--env=%s ' "$e"; done) com.usebottles.bottles -c "
    '$W' wineboot -u; '$WS' -w
    '$W' reg add 'HKCU\\Software\\Wine\\Drivers' /v Graphics /d $drv /f >/dev/null
    '$W' notepad & sleep 8
    '$W' notepad & sleep 12
    '$W' 'C:\\hungprobe.exe'
    sleep 10
    '$WS' -k" >"$LOG" 2>&1 &
# Host side (hyprctl is not in the sandbox): once the probe printed, look for Hyprland's ANR dialog and take a screenshot.
for _ in $(seq 1 240); do grep -aq "^notepad windows=" "$LOG" 2>/dev/null && break; sleep 0.5; done
sleep 3
anr=$(hyprctl clients -j | grep -c 'Not Responding')
grim "$D/broker-proto/gate-logs/second-$tag.png" 2>/dev/null
wait
echo "== $tag anr-dialogs=$anr"; grep -a "^notepad" "$LOG"
