#!/bin/bash
# The agent with real windows in an isolated Plasma session (see tests/agent_session_test.py).
#   tests/run_agent_session_test.sh [build dir]
# Needs kwin_wayland, plasmashell, kcalc, kate and python3-dbus; never touches the real desktop.
set -e
ROOT=$(cd "$(dirname "$0")/.." && pwd)
BUILD=$(realpath "${1:-$ROOT/build}")
S=$(mktemp -d)
trap 'rm -rf "$S"' EXIT
mkdir -p "$S/bin"
# No systemd user manager on the test bus: `systemctl --user` must not reach the real one.
printf '#!/bin/sh\nexit 1\n' > "$S/bin/systemctl"
chmod +x "$S/bin/systemctl"
export PATH="$S/bin:$PATH" QML_IMPORT_PATH="$BUILD/qml" JARVIS_BUILD="$BUILD"
exec "$ROOT/tests/plasma-session.sh" "$S" python3 "$ROOT/tests/agent_session_test.py"
