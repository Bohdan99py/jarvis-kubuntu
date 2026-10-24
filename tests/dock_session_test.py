#!/usr/bin/env python3
"""The Jarvis Dock's Start button in a real (nested, isolated) Plasma 6 session.

Runs inside tests/plasma-session.sh (its own KWin, plasmashell and session bus;
the real desktop is never touched):

    QML_IMPORT_PATH=build/qml tests/plasma-session.sh <scratch> python3 tests/dock_session_test.py

with the widgets copied to <scratch>/data/plasma/plasmoids first (run_dock_test.sh
does both). It puts a panel with the dock on the bottom edge, then — clicking
and typing through KWin's input emulation (tests/ei_input.py) — checks that:
  1. a click on Start opens the launcher (an applet popup above the panel);
  2. Escape closes it, and a second click opens it again;
  3. typing "konsole" and Enter starts the real Konsole (a new window);
  4. the Meta key opens the launcher too.
KWin reports its windows through a short KWin script (printed to its log).
"""
import json
import os
import re
import subprocess
import sys
import tempfile
import time
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]
EI = [sys.executable, str(ROOT / 'tests/ei_input.py')]
KWIN_LOG = Path(os.environ['XDG_RUNTIME_DIR']) / 'kwin.log'
if os.environ.get('WAYLAND_DISPLAY') != 'jtest':
    sys.exit('dock_session_test.py: run it through tests/plasma-session.sh')


def qdbus(*args):
    return subprocess.run(['qdbus6', *args], capture_output=True, text=True, timeout=20).stdout.strip()


def plasma(script):
    return qdbus('org.kde.plasmashell', '/PlasmaShell', 'org.kde.PlasmaShell.evaluateScript', script)


def windows():
    """KWin's windows: [{cls, caption, dock, popup, x, y, w, h}]."""
    marker = f'JWIN{time.monotonic_ns()}'
    with tempfile.NamedTemporaryFile('w', suffix='.js', delete=False) as f:
        f.write('var out = [];\nvar ws = workspace.windowList();\nfor (var i = 0; i < ws.length; ++i) { var w = ws[i]; var g = w.frameGeometry;\n'
                ' out.push({cls: String(w.resourceClass), caption: String(w.caption), dock: !!w.dock, popup: !!w.appletPopup || !!w.popupWindow,'
                ' x: g.x, y: g.y, w: g.width, h: g.height}); }\n'
                f'print("{marker}" + JSON.stringify(out));\n')
        path = f.name
    sid = qdbus('org.kde.KWin', '/Scripting', 'org.kde.kwin.Scripting.loadScript', path, marker)
    qdbus('org.kde.KWin', f'/Scripting/Script{sid}', 'org.kde.kwin.Script.run')
    found = None
    for _ in range(30):
        time.sleep(0.1)
        m = re.search(marker + r'(\[.*\])', KWIN_LOG.read_text(errors='replace'))
        if m:
            found = json.loads(m.group(1))
            break
    qdbus('org.kde.KWin', '/Scripting', 'org.kde.kwin.Scripting.unloadScript', marker)
    os.unlink(path)
    if found is None:
        sys.exit('FAIL: KWin did not report its windows')
    return found


def launcher_open():
    return [w for w in windows() if 'plasmashell' in w['cls'] and w['popup'] and not w['dock'] and w['h'] > 200]


def fail(message):
    print('FAIL:', message)
    subprocess.run(['spectacle', '-b', '-n', '-f', '-o', os.environ.get('JARVIS_SHOT', '/dev/null')], capture_output=True)
    sys.exit(1)


# The panel: the dock alone first (no Jarvis core button, no workspaces), the tray, the clock.
plasma('''
var ps = panels(); for (var i = 0; i < ps.length; ++i) ps[i].remove();
var p = new Panel("org.kde.panel"); p.location = "bottom"; p.height = 44; p.floating = true; p.lengthMode = "fit"; p.alignment = "center";
var d = p.addWidget("org.jarvis.dock"); d.currentConfigGroup = ["General"];
d.writeConfig("showJarvis", false); d.writeConfig("showProfiles", false); d.writeConfig("filterByScreen", false);
p.addWidget("org.kde.plasma.systemtray"); p.addWidget("org.kde.plasma.digitalclock");''')
time.sleep(5)
if os.environ.get('JARVIS_DEBUG'):
    print('windows:', windows())
    subprocess.run([*EI, 'key', 'super']); time.sleep(1.5)
    print('after Meta:', windows())
    sys.exit(0)
panel = next((w for w in windows() if w['cls'] == 'plasmashell' and w['dock']), None)
if not panel:
    fail('no panel window')
y = panel['y'] + panel['h'] // 2

# 1. Start is the dock's first button: find it from the panel's left edge.
start_x = None
for x in range(panel['x'] + 8, panel['x'] + 120, 8):
    subprocess.run([*EI, 'click', str(x), str(y)], check=True)
    time.sleep(1.0)
    if launcher_open():
        start_x = x
        break
if start_x is None:
    fail('a click on the dock\'s Start button opened nothing')
popup = launcher_open()[0]
# (The panel's window includes the floating panel's gap: "above" = above its middle.)
if not popup['y'] + popup['h'] <= panel['y'] + panel['h'] // 2:
    fail(f'the launcher is not above the panel: {popup} / {panel}')
print(f'ok: Start at x={start_x} opens the launcher ({popup["w"]}×{popup["h"]}, above the panel)')

# 2. Escape closes; a click opens again.
subprocess.run([*EI, 'key', 'esc'], check=True)
time.sleep(0.8)
if launcher_open():
    fail('Escape did not close the launcher')
subprocess.run([*EI, 'click', str(start_x), str(y)], check=True)
time.sleep(1.0)
if not launcher_open():
    fail('the second click did not open the launcher again')
print('ok: Escape closes it, a click opens it again')

# 3. Search and start a real application.
before = sum(1 for w in windows() if 'konsole' in w['cls'].lower())
subprocess.run([*EI, 'type', 'konsole'], check=True)
time.sleep(1.5)
subprocess.run([*EI, 'key', 'enter'], check=True)
for _ in range(40):
    time.sleep(0.25)
    if sum(1 for w in windows() if 'konsole' in w['cls'].lower()) > before:
        break
else:
    fail('typing "konsole" and Enter started nothing')
if launcher_open():
    fail('the launcher stayed open after starting the application')
print('ok: "konsole" + Enter starts Konsole and closes the launcher')

# 4. Meta.
subprocess.run([*EI, 'key', 'super'], check=True)
time.sleep(1.2)
if not launcher_open():
    fail('the Meta key did not open the launcher')
subprocess.run([*EI, 'key', 'esc'], check=True)
print('ok: Meta opens the launcher')
print('PASS: the dock\'s Start button in an isolated Plasma session')
