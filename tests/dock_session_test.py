#!/usr/bin/env python3
"""The Jarvis Dock in a real (nested, isolated) Plasma 6 session.

Runs inside tests/plasma-session.sh (its own KWin, plasmashell and session bus;
the real desktop is never touched):

    QML_IMPORT_PATH=build/qml tests/plasma-session.sh <scratch> python3 tests/dock_session_test.py

with the widgets copied to <scratch>/data/plasma/plasmoids first (run_dock_test.sh
does both).
The panel is the one JARVIS DESKTOP makes (the dock, Plasma's tray and clock).
Clicking and typing go through KWin's input emulation (tests/ei_input.py);
KWin reports its windows through a short KWin script (printed to its log).

  1. Start: ten openings and closings by click, ten by Meta and Escape; the
     launcher is an applet popup above the panel, near the button, inside the
     screen; a click outside closes it; it opens again after that;
  2. search "konsole" + Enter starts the real Konsole and closes the launcher;
  3. the dock switches windows: a click activates an application's window, a
     second click minimizes it, a third brings it back;
  4. pinning from the task's context menu (keyboard) lands in the dock's
     configuration and survives a plasmashell restart (a new login);
  5. the right part: the glass is Jarvis's under Plasma's tray and clock too
     (0.20: it was Plasma's light panel background), and every popup opened
     there lies inside the screen without covering the panel;
  6. with jarvisd hung (SIGSTOP) and after it quits, Start and the dock still work;
  7. with far more pinned applications than the screen holds, the panel stays
     on the screen and the clock is still there and opens its popup.
Steps 1–5 and 7 run without jarvisd.
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
RUNTIME = Path(os.environ['XDG_RUNTIME_DIR'])
KWIN_LOG = RUNTIME / 'kwin.log'
if os.environ.get('WAYLAND_DISPLAY') != 'jtest':
    sys.exit('dock_session_test.py: run it through tests/plasma-session.sh')
SHOTS = Path(os.environ.get('JARVIS_SHOTS', tempfile.mkdtemp()))
SHOTS.mkdir(parents=True, exist_ok=True)


def qdbus(*args):
    return subprocess.run(['qdbus6', *args], capture_output=True, text=True, timeout=20).stdout.strip()


def plasma(script):
    return qdbus('org.kde.plasmashell', '/PlasmaShell', 'org.kde.PlasmaShell.evaluateScript', script)


def kwin_script(body):
    marker = f'JOUT{time.monotonic_ns()}'
    with tempfile.NamedTemporaryFile('w', suffix='.js', delete=False) as f:
        f.write(body.replace('PRINT(', f'print("{marker}" + '))
        path = f.name
    sid = qdbus('org.kde.KWin', '/Scripting', 'org.kde.kwin.Scripting.loadScript', path, marker)
    qdbus('org.kde.KWin', f'/Scripting/Script{sid}', 'org.kde.kwin.Script.run')
    found = None
    for _ in range(30):
        time.sleep(0.1)
        m = re.search(marker + r'(.*)', KWIN_LOG.read_text(errors='replace'))
        if m:
            found = json.loads(m.group(1))
            break
    qdbus('org.kde.KWin', '/Scripting', 'org.kde.kwin.Scripting.unloadScript', marker)
    os.unlink(path)
    if found is None:
        sys.exit('FAIL: KWin did not answer its script')
    return found


def windows():
    """KWin's windows: [{cls, caption, dock, popup, tip, min, active, x, y, w, h}]."""
    return kwin_script('var out = [];\nvar ws = workspace.windowList();\nfor (var i = 0; i < ws.length; ++i) { var w = ws[i]; var g = w.frameGeometry;\n'
                       ' out.push({cls: String(w.resourceClass), caption: String(w.caption), dock: !!w.dock, popup: !!w.appletPopup || !!w.popupWindow,'
                       ' tip: !!w.tooltip, min: !!w.minimized, active: w === workspace.activeWindow, x: g.x, y: g.y, w: g.width, h: g.height}); }\n'
                       'PRINT(JSON.stringify(out));\n')


def screen():
    g = kwin_script('var g = workspace.screens[0].geometry; PRINT(JSON.stringify({x: g.x, y: g.y, w: g.width, h: g.height}));\n')
    return g


def ei(*args):
    subprocess.run([*EI, *map(str, args)], check=True)


def popups():
    return [w for w in windows() if 'plasmashell' in w['cls'] and w['popup'] and not w['dock'] and not w['tip'] and w['h'] > 60]


def launcher_open():
    return [w for w in popups() if w['h'] > 200 and w['w'] > 300]


def shot(name):
    path = SHOTS / f'{name}.png'
    subprocess.run(['spectacle', '-b', '-n', '-f', '-o', str(path)], capture_output=True, timeout=30)
    return path


def fail(message):
    print('FAIL:', message)
    shot('failure')
    print('screenshots:', SHOTS)
    sys.exit(1)


def wait(cond, seconds=4.0, step=0.25):
    end = time.monotonic() + seconds
    while time.monotonic() < end:
        r = cond()
        if r:
            return r
        time.sleep(step)
    return cond()


def dock_config(key):
    return plasma('var ps = panels(); for (var i = 0; i < ps.length; ++i) { var ws = ps[i].widgets(); for (var j = 0; j < ws.length; ++j)'
                  ' if (ws[j].type === "org.jarvis.dock") { ws[j].currentConfigGroup = ["General"]; print(JSON.stringify(ws[j].readConfig("%s"))); } }' % key)


def restart_plasmashell():
    """A new login, as far as the panel is concerned: plasmashell reads its layout again."""
    for pid in subprocess.run(['pgrep', '-u', str(os.getuid()), '-x', 'plasmashell'], capture_output=True, text=True).stdout.split():
        try:
            env = Path(f'/proc/{pid}/environ').read_bytes().split(b'\0')
        except OSError:
            continue
        if f'XDG_RUNTIME_DIR={RUNTIME}'.encode() in env:   # this session's only
            subprocess.run(['kill', pid])
    time.sleep(2)
    subprocess.Popen(['plasmashell', '--no-respawn'], stdout=open(RUNTIME / 'plasmashell2.log', 'w'), stderr=subprocess.STDOUT,
                     stdin=subprocess.DEVNULL, start_new_session=True)
    for _ in range(100):
        if subprocess.run(['gdbus', 'call', '--session', '-d', 'org.kde.plasmashell', '-o', '/PlasmaShell', '-m',
                           'org.freedesktop.DBus.Peer.Ping'], capture_output=True).returncode == 0:
            break
        time.sleep(0.2)
    time.sleep(5)


def luminance(path, box):
    from PIL import Image
    im = Image.open(path).convert('L').crop(box)
    values = sorted(im.tobytes())
    return values[len(values) // 2]


# The panel JARVIS DESKTOP makes (desktop_shell_scripts.cpp, applyScript): the dock with its
# defaults (the Jarvis core button and the workspaces too, with no jarvisd behind them), the tray, the clock.
PINS = ['applications:org.kde.dolphin.desktop', 'applications:org.kde.kate.desktop']
plasma('''
var ps = panels(); for (var i = 0; i < ps.length; ++i) ps[i].remove();
var p = new Panel("org.kde.panel"); p.location = "bottom"; p.height = 44; p.floating = true; p.lengthMode = "fit"; p.alignment = "center";
var d = p.addWidget("org.jarvis.dock"); d.currentConfigGroup = ["General"];
d.writeConfig("launchers", %s); d.writeConfig("filterByScreen", false); d.writeConfig("magnification", false);
p.addWidget("org.kde.plasma.systemtray"); p.addWidget("org.kde.plasma.digitalclock");''' % json.dumps(PINS))
time.sleep(5)
S = screen()
panel = next((w for w in windows() if w['cls'] == 'plasmashell' and w['dock']), None)
if not panel:
    fail('no panel window')
y = panel['y'] + panel['h'] // 2
print(f'screen {S["w"]}×{S["h"]}, panel {panel["w"]}×{panel["h"]} at {panel["x"]},{panel["y"]}')

# 1. Start: the second button (after the Jarvis core); found from the panel's left edge.
start_x = None
for x in range(panel['x'] + 8, panel['x'] + 200, 6):
    ei('click', x, y)
    if wait(launcher_open, 1.2):
        start_x = x
        break
    for p in popups():   # the core button's own popup, if any: close it
        ei('key', 'esc')
if start_x is None:
    fail('a click on the dock\'s Start button opened nothing')
pop = launcher_open()[0]
if not pop['y'] + pop['h'] <= panel['y'] + panel['h'] // 2:
    fail(f'the launcher is not above the panel: {pop} / {panel}')
if pop['x'] < S['x'] or pop['y'] < S['y'] or pop['x'] + pop['w'] > S['x'] + S['w']:
    fail(f'the launcher is not inside the screen: {pop} / {S}')
if not pop['x'] <= start_x <= pop['x'] + pop['w']:
    fail(f'the launcher is not by its button (x={start_x}): {pop}')
start_x += 12   # well inside the button
ei('key', 'esc')
if wait(lambda: not launcher_open(), 2) is False:
    fail('Escape did not close the launcher')
for n in range(10):
    ei('click', start_x, y)
    if not wait(launcher_open, 2):
        fail(f'click {n + 1}: the launcher did not open')
    ei('click', start_x, y)
    if not wait(lambda: not launcher_open(), 2):
        fail(f'click {n + 1}: a second click on Start did not close the launcher')
    time.sleep(0.4)
print(f'ok: Start at x={start_x} opens the launcher above the panel, inside the screen; 10 × click open / click close')
for n in range(10):
    ei('key', 'super')
    if not wait(launcher_open, 2):
        fail(f'Meta {n + 1}: the launcher did not open')
    ei('key', 'esc')
    if not wait(lambda: not launcher_open(), 2):
        fail(f'Meta {n + 1}: Escape did not close the launcher')
print('ok: 10 × Meta opens it, Escape closes it')
ei('click', start_x, y)
wait(launcher_open, 2)
ei('click', S['x'] + S['w'] // 2, S['y'] + 60)   # the desktop, far from the launcher
if not wait(lambda: not launcher_open(), 2):
    fail('a click outside did not close the launcher')
time.sleep(0.5)
ei('click', start_x, y)
if not wait(launcher_open, 2):
    fail('the launcher did not open again after a click outside')
print('ok: a click outside closes it, and it opens again')

# 2. Search and start a real application.
before = sum(1 for w in windows() if 'konsole' in w['cls'].lower())
ei('type', 'konsole')
time.sleep(1.5)
ei('key', 'enter')
if not wait(lambda: sum(1 for w in windows() if 'konsole' in w['cls'].lower()) > before, 10):
    fail('typing "konsole" and Enter started nothing')
if launcher_open():
    fail('the launcher stayed open after starting the application')
print('ok: "konsole" + Enter starts Konsole and closes the launcher')

# 3. Window switching. The dock: [core] [Start] | [dolphin] [kate] [konsole]; the cell is
# the panel's thickness less the padding, the gap 4 px: the tasks are found by their tool tips' order.
subprocess.Popen(['dolphin'], stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL)
if not wait(lambda: any('dolphin' in w['cls'] for w in windows()), 10):
    fail('Dolphin did not start')
time.sleep(1.5)


def window(cls):
    return next((w for w in windows() if cls in w['cls']), None)


# The tasks lie right of Start: probe cell by cell until a click activates Dolphin.
dolphin_x = None
for x in range(start_x + 30, start_x + 200, 8):
    if window('dolphin')['active']:
        # Make something else active first, so the click has to switch.
        ei('click', S['x'] + S['w'] // 2, S['y'] + 20)
        time.sleep(0.5)
    ei('click', x, y)
    time.sleep(0.8)
    for p in popups():
        ei('key', 'esc')
    if window('dolphin')['active'] and not window('dolphin')['min']:
        dolphin_x = x
        break
if dolphin_x is None:
    fail('no click on the dock activated Dolphin')
ei('click', dolphin_x, y)
if not wait(lambda: window('dolphin')['min'], 3):
    fail('a click on the active application did not minimize it')
ei('click', dolphin_x, y)
if not wait(lambda: window('dolphin')['active'] and not window('dolphin')['min'], 3):
    fail('a click on the minimized application did not bring it back')
print(f'ok: the dock activates Dolphin (x={dolphin_x}), minimizes it, brings it back')

# 4. Pin Konsole from its context menu (keyboard), then a new "login".
if 'org.kde.konsole' in dock_config('launchers'):
    fail('Konsole is pinned before the test pinned it')
konsole_x = None
for x in range(dolphin_x + 30, dolphin_x + 260, 8):
    ei('click', x, y)
    time.sleep(0.8)
    for p in popups():
        ei('key', 'esc')
    if (window('konsole') or {}).get('active'):
        konsole_x = x
        break
if konsole_x is None:
    fail('Konsole has no button in the dock')
# Two windows of one application: a click lists them (an applet popup), Escape closes the list.
ei('click', konsole_x, y, 'middle')
if not wait(lambda: sum(1 for w in windows() if 'konsole' in w['cls'].lower()) >= 2, 10):
    fail('a middle click on Konsole did not open a second window')
time.sleep(1)
ei('click', konsole_x, y)
if not wait(popups, 3):
    fail('a click on an application with two windows did not list them')
ei('key', 'esc')
if not wait(lambda: not popups(), 3):
    fail('Escape did not close the window list')
print('ok: a middle click opens a second Konsole; a click lists both windows; Escape closes the list')
ei('click', konsole_x, y, 'right')
time.sleep(1)
ei('key', 'down')     # New window
ei('key', 'down')     # Pin to the dock
ei('key', 'enter')
if not wait(lambda: 'org.kde.konsole' in dock_config('launchers'), 4):
    fail('"Pin to the dock" did not pin Konsole: ' + dock_config('launchers'))
restart_plasmashell()
if 'org.kde.konsole' not in dock_config('launchers'):
    fail('the pin did not survive a plasmashell restart: ' + dock_config('launchers'))
if not wait(lambda: next((w for w in windows() if w['cls'] == 'plasmashell' and w['dock']), None), 10):
    fail('no panel after the restart')
print('ok: "Pin to the dock" from the menu pins Konsole; the pin survives a plasmashell restart')

# 5. The right part: Jarvis's glass under the tray and the clock, and their popups.
panel = next(w for w in windows() if w['cls'] == 'plasmashell' and w['dock'])
y = panel['y'] + panel['h'] // 2
time.sleep(1)
path = shot('panel')
right = panel['x'] + panel['w']
# The last 30 % of the panel: the tray and the clock; the median pixel is the background.
lum = luminance(path, (int(right - panel['w'] * 0.3), panel['y'] + 10, int(right - 12), panel['y'] + panel['h'] - 10))
if lum > 110:
    fail(f'the tray and the clock stand on a light background (median luminance {lum}): not the Jarvis glass')
print(f'ok: the tray and the clock stand on the Jarvis glass (median luminance {lum})')
seen = 0
for x in range(right - 20, right - int(panel['w'] * 0.3), -18):
    ei('click', x, y)
    time.sleep(1.0)
    for p in popups():
        seen += 1
        if p['x'] < S['x'] or p['y'] < S['y'] or p['x'] + p['w'] > S['x'] + S['w'] or p['y'] + p['h'] > S['y'] + S['h']:
            fail(f'a popup of the right part leaves the screen: {p} / {S}')
        if p['y'] + p['h'] > panel['y'] + panel['h'] // 2:
            fail(f'a popup of the right part covers the panel: {p} / {panel}')
    ei('key', 'esc')
    time.sleep(0.4)
if seen < 2:
    fail(f'the right part opened {seen} popup(s): its clicks do not reach the tray and the clock')
print(f'ok: {seen} popups of the tray and the clock, each inside the screen and above the panel')

# 6. jarvisd: running, then hung (stopped with SIGSTOP: every call to it waits for its time-out),
# then gone. The core button talks to it; Start and the windows must not wait for it.
JARVISD = Path(os.environ.get('JARVIS_BUILD', ROOT / 'build')) / 'jarvisd'
if JARVISD.exists():
    import signal
    # With jarvisd up the dock adds the workspace buttons (and the panel grows and moves); they
    # switch desktops through jarvisd, so they stay out here and the buttons keep their places.
    plasma('var ps = panels(); for (var i = 0; i < ps.length; ++i) { var ws = ps[i].widgets(); for (var j = 0; j < ws.length; ++j)'
           ' if (ws[j].type === "org.jarvis.dock") { ws[j].currentConfigGroup = ["General"]; ws[j].writeConfig("showProfiles", false); } }')
    daemon = subprocess.Popen([str(JARVISD)], stdout=open(RUNTIME / 'jarvisd.log', 'w'), stderr=subprocess.STDOUT, stdin=subprocess.DEVNULL)
    if not wait(lambda: 'true' in subprocess.run(['gdbus', 'call', '--session', '-d', 'org.freedesktop.DBus', '-o', '/org/freedesktop/DBus', '-m',
                                                  'org.freedesktop.DBus.NameHasOwner', 'org.jarvis.Daemon1'], capture_output=True, text=True).stdout, 15):
        fail('jarvisd did not come up on the test bus')
    time.sleep(3)
    daemon.send_signal(signal.SIGSTOP)
    panel = next(w for w in windows() if w['cls'] == 'plasmashell' and w['dock'])
    y = panel['y'] + panel['h'] // 2
    ei('click', start_x - 44, y)          # the core button: a call into the hung jarvisd
    time.sleep(0.5)
    for n in range(3):
        t = time.monotonic()
        ei('click', start_x, y)
        if not wait(launcher_open, 2):
            fail(f'with jarvisd hung, Start did not open ({n + 1})')
        ei('key', 'esc')
        if not wait(lambda: not launcher_open(), 2):
            fail(f'with jarvisd hung, Escape did not close the launcher ({n + 1})')
    ei('click', dolphin_x, y)
    time.sleep(1)
    ei('click', dolphin_x, y)
    if not wait(lambda: window('dolphin') and window('dolphin')['active'] and not window('dolphin')['min'], 3):
        ei('click', dolphin_x, y)
        if not wait(lambda: window('dolphin')['active'] and not window('dolphin')['min'], 3):
            fail('with jarvisd hung, the dock does not switch to Dolphin')
    daemon.send_signal(signal.SIGCONT)
    daemon.terminate()
    daemon.wait(10)
    ei('key', 'super')
    if not wait(launcher_open, 2):
        fail('after jarvisd quit, Meta did not open the launcher')
    ei('key', 'esc')
    print('ok: with jarvisd hung (SIGSTOP) and after it quit, Start opens and closes, the dock switches windows')
else:
    print(f'skip: no {JARVISD} (jarvisd hung / gone)')

# 7. Far more applications than the screen holds.
many = sorted(p.name for p in Path('/usr/share/applications').glob('*.desktop')
              if 'NoDisplay=true' not in p.read_text(errors='replace'))[:60]
plasma('var ps = panels(); for (var i = 0; i < ps.length; ++i) { var ws = ps[i].widgets(); for (var j = 0; j < ws.length; ++j)'
       ' if (ws[j].type === "org.jarvis.dock") { ws[j].currentConfigGroup = ["General"]; ws[j].writeConfig("launchers", %s); } }'
       % json.dumps(['applications:' + a for a in many]))
time.sleep(4)
panel = next(w for w in windows() if w['cls'] == 'plasmashell' and w['dock'])
if panel['x'] < S['x'] or panel['x'] + panel['w'] > S['x'] + S['w']:
    fail(f'with {len(many)} pinned applications the panel leaves the screen: {panel}')
right = panel['x'] + panel['w']
# (0.20, found here: a glass without Plasma's background under it left a grown panel with its
# old input region — the clock past the old end ignored clicks.)
ei('click', right - 30, panel['y'] + panel['h'] // 2)
if not wait(lambda: popups(), 3):
    shot('overflow')
    fail(f'with {len(many)} pinned applications the clock at the panel\'s end does not answer')
ei('key', 'esc')
print(f'ok: {len(many)} pinned applications: the panel stays on the screen ({panel["w"]} px), the clock still opens its popup')
print('PASS: the Jarvis Dock in an isolated Plasma session')
