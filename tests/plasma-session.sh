#!/bin/bash
# An isolated Plasma 6 session for trying the desktop shell without touching the
# real desktop: a virtual KWin (no window), its own plasmashell, a private session
# bus without service activation (no portals that would crash without a display
# and fill the real session with crash reports) and private XDG directories.
#
#   tests/plasma-session.sh <scratch dir> <command…>
#
# The command runs inside the session (WAYLAND_DISPLAY, the bus and XDG_* are
# set): install the widgets into <scratch dir>/data/plasma/plasmoids, export
# QML_IMPORT_PATH=build/qml before calling, start build/jarvisd, run
# `build/jarvis --desktop-mode jarvis`, take pictures with `spectacle -b -n -f -o
# shot.png`. Everything stops when the command ends. The virtual screens:
# JTEST_WIDTH × JTEST_HEIGHT (1600×900), JTEST_SCALE (1), JTEST_OUTPUTS (1).
set -e
S=$(realpath -m "$1"); shift
ROOT=$(cd "$(dirname "$0")/.." && pwd)
mkdir -p "$S/config" "$S/data" "$S/cache" "$S/state"
R=$(mktemp -d)   # short: Wayland socket paths are limited to 108 bytes
chmod 700 "$R"
export XDG_CONFIG_HOME="$S/config" XDG_DATA_HOME="$S/data" XDG_CACHE_HOME="$S/cache" XDG_STATE_HOME="$S/state" XDG_RUNTIME_DIR="$R"
export XDG_CURRENT_DESKTOP=KDE KDE_FULL_SESSION=true KDE_SESSION_VERSION=6 XDG_SESSION_TYPE=wayland
unset WAYLAND_DISPLAY DISPLAY SESSION_MANAGER
exec dbus-run-session --config-file="$ROOT/tests/test-bus.conf" -- bash -c '
kwin_wayland --virtual --width "${JTEST_WIDTH:-1600}" --height "${JTEST_HEIGHT:-900}" --scale "${JTEST_SCALE:-1}" \
  --output-count "${JTEST_OUTPUTS:-1}" --socket jtest --no-lockscreen > "$XDG_RUNTIME_DIR/kwin.log" 2>&1 &
for i in $(seq 50); do [ -S "$XDG_RUNTIME_DIR/jtest" ] && break; sleep 0.2; done
export WAYLAND_DISPLAY=jtest QT_QPA_PLATFORM=wayland
/usr/lib/x86_64-linux-gnu/libexec/kactivitymanagerd > "$XDG_RUNTIME_DIR/kamd.log" 2>&1 &
kded6 > "$XDG_RUNTIME_DIR/kded.log" 2>&1 &
sleep 1
plasmashell --no-respawn > "$XDG_RUNTIME_DIR/plasmashell.log" 2>&1 &
for i in $(seq 100); do gdbus call --session -d org.kde.plasmashell -o /PlasmaShell -m org.freedesktop.DBus.Peer.Ping >/dev/null 2>&1 && break; sleep 0.2; done
sleep 4
"$@"
status=$?
kill $(jobs -p) 2>/dev/null
# Whatever the command started in the background (a jarvisd, an application) belongs to
# this session too: every process of ours whose XDG_RUNTIME_DIR is this session'"'"'s goes
# (nothing else — the real desktop has another runtime directory).
sleep 1
for p in $(pgrep -u "$(id -u)"); do
  [ "$p" = "$$" ] || [ "$p" = "$PPID" ] && continue   # this shell and dbus-run-session end by themselves
  { tr "\0" "\n" < /proc/$p/environ; } 2>/dev/null | grep -qx "XDG_RUNTIME_DIR=$XDG_RUNTIME_DIR" && kill "$p" 2>/dev/null
done
exit $status
' bash "$@"
