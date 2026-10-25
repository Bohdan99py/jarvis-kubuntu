#!/bin/bash
# The Jarvis Dock in an isolated Plasma session (see tests/dock_session_test.py).
#   tests/run_dock_test.sh [build dir]
# Needs kwin_wayland, plasmashell, spectacle and python3-dbus; never touches the real desktop.
set -e
ROOT=$(cd "$(dirname "$0")/.." && pwd)
BUILD=$(realpath "${1:-$ROOT/build}")
S=$(mktemp -d)
trap 'rm -rf "$S"' EXIT
mkdir -p "$S/data/plasma/plasmoids"
cp -r "$ROOT"/plasmoids/org.jarvis.* "$S/data/plasma/plasmoids/"
export QML_IMPORT_PATH="$BUILD/qml" JARVIS_BUILD="$BUILD" JARVIS_SHOT="${JARVIS_SHOT:-$BUILD/dock-test-failure.png}"
exec "$ROOT/tests/plasma-session.sh" "$S" python3 "$ROOT/tests/dock_session_test.py"
