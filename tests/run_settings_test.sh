#!/bin/bash
# The settings window in an isolated Plasma session (see tests/settings_session_test.py).
#   tests/run_settings_test.sh [build dir]
# Screens: JTEST_WIDTH, JTEST_HEIGHT, JTEST_SCALE (tests/plasma-session.sh); screenshots: JARVIS_SHOTS.
# Needs kwin_wayland, plasmashell, spectacle and python3-dbus; never touches the real desktop.
set -e
ROOT=$(cd "$(dirname "$0")/.." && pwd)
BUILD=$(realpath "${1:-$ROOT/build}")
S=$(mktemp -d)
trap 'rm -rf "$S"' EXIT
mkdir -p "$S/data/plasma/plasmoids" "$S/bin"
cp -r "$ROOT"/plasmoids/org.jarvis.* "$S/data/plasma/plasmoids/"
# No systemd user manager on the test bus: `systemctl --user` must not reach the real one.
printf '#!/bin/sh\nexit 1\n' > "$S/bin/systemctl"
chmod +x "$S/bin/systemctl"
export PATH="$S/bin:$PATH" QML_IMPORT_PATH="$BUILD/qml"
exec "$ROOT/tests/plasma-session.sh" "$S" python3 "$ROOT/tests/settings_session_test.py" "$BUILD"
