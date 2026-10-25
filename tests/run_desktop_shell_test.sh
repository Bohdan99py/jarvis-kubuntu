#!/bin/bash
# JARVIS DESKTOP on, on again, a new login and the restore in an isolated Plasma session
# (see tests/desktop_shell_session_test.py).   tests/run_desktop_shell_test.sh [build dir]
# Needs kwin_wayland, plasmashell and kquitapp6; never touches the real desktop.
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
exec "$ROOT/tests/plasma-session.sh" "$S" python3 "$ROOT/tests/desktop_shell_session_test.py" "$BUILD"
