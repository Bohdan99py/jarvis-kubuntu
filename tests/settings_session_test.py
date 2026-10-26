#!/usr/bin/env python3
"""The settings window (0.21) in a real (nested, isolated) Plasma 6 session.

Runs inside tests/plasma-session.sh through tests/run_settings_test.sh; the real
desktop, keyring and systemd are never touched (no Secret Service on the test bus,
a stub systemctl). Everything is done with the keyboard, the way a person who does
not use a mouse would: Ctrl+F, the search, Enter, Tab, Space, arrows.

  0. the window fits the screen (1366×768 and the same at 125 % and 150 %);
  1. "dock height" → the row; ↑ four times: the dock's panel grows by 4 px in
     Plasma and config.json says the same; a change made in Plasma shows up in
     the window (the panel back to 44 px by a Plasma script → config.json);
     "icon size in the dock", → three times: the dock's own setting changes;
  2. "headphones" finds Sound; Enter, Space opens KDE's System Settings;
  3. the quick-commands key: Meta+D is refused (KWin uses it), Ctrl+Alt+K is set
     in kglobalaccel;
  4. the Claude key: a malformed key is refused with a message and nothing is saved;
     an unknown well-formed key is checked with Anthropic only on request, not saved;
  5. "analysis of the screen" off: saved at once;
  6. "Reset this page…" on Desktop: the theme is back to Classic, the key, memory
     and other settings stay;
  7. the window restarted: the values are still there;
  8. jarvisd stopped: the window says so and still saves.
Screenshots go to $JARVIS_SHOTS for a person to look at.
"""
import json
import os
import re
import signal
import subprocess
import sys
import tempfile
import time
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]
BUILD = Path(sys.argv[1] if len(sys.argv) > 1 else ROOT / 'build')
EI = [sys.executable, str(ROOT / 'tests/ei_input.py')]
RUNTIME = Path(os.environ['XDG_RUNTIME_DIR'])
CONFIG = Path(os.environ['XDG_CONFIG_HOME']) / 'jarvis/config.json'
DATA = Path(os.environ['XDG_DATA_HOME']) / 'jarvis'
KWIN_LOG = RUNTIME / 'kwin.log'
if os.environ.get('WAYLAND_DISPLAY') != 'jtest':
    sys.exit('settings_session_test.py: run it through tests/run_settings_test.sh')
SHOTS = Path(os.environ.get('JARVIS_SHOTS', tempfile.mkdtemp()))
SHOTS.mkdir(parents=True, exist_ok=True)
SCALE = os.environ.get('JTEST_SCALE', '1')
KEY = 'sk-ant-api03-session-test-key-0123456789'


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
    return found or []


def windows():
    return kwin_script('var out = [];\nvar ws = workspace.windowList();\nfor (var i = 0; i < ws.length; ++i) { var w = ws[i]; var g = w.frameGeometry;\n'
                       ' out.push({cls: String(w.resourceClass), caption: String(w.caption), active: w === workspace.activeWindow, x: g.x, y: g.y, w: g.width, h: g.height}); }\n'
                       'PRINT(JSON.stringify(out));\n')


def screen():
    return kwin_script('var g = workspace.clientArea(KWin.MaximizeArea, workspace.screens[0], workspace.currentDesktop);'
                       ' PRINT(JSON.stringify({x: g.x, y: g.y, w: g.width, h: g.height}));\n')


def ei(*args):
    subprocess.run([*EI, *map(str, args)], check=True)


def shot(name):
    print('step:', name, flush=True)
    path = SHOTS / f'{name}-x{SCALE}.png'
    subprocess.run(['spectacle', '-b', '-n', '-a', '-o', str(path)], capture_output=True, timeout=30)
    return path


def fail(message):
    print('FAIL:', message)
    shot('failure')
    print('screenshots:', SHOTS)
    sys.exit(1)


def wait(cond, seconds=6.0, step=0.25):
    end = time.monotonic() + seconds
    while time.monotonic() < end:
        r = cond()
        if r:
            return r
        time.sleep(step)
    return cond()


def config():
    try:
        return json.loads(CONFIG.read_text())
    except (OSError, ValueError):
        return {}


def settings_window():
    return [w for w in windows() if w['caption'].startswith('Jarvis settings')]


def search(words):
    ei('key', 'ctrl+f')
    time.sleep(0.3)
    ei('key', 'ctrl+a', 'backspace')
    ei('type', words)
    time.sleep(0.6)


def reveal(words):
    search(words)
    ei('key', 'enter')
    time.sleep(1.0)


def dock_panel():
    out = plasma('var r = {};\nvar ps = panels();\nfor (var i = 0; i < ps.length; ++i) { var ws = ps[i].widgets();\n'
                 ' for (var j = 0; j < ws.length; ++j) if (ws[j].type === "org.jarvis.dock") r = { height: ps[i].height, id: ps[i].id }; }\n'
                 'print(JSON.stringify(r));')
    return json.loads(out or '{}')


GUI = None


def session_kill(name):
    """Stops the processes called `name` of this nested session only (never the real desktop's)."""
    for pid in os.listdir('/proc'):
        if not pid.isdigit():
            continue
        try:
            if Path(f'/proc/{pid}/comm').read_text().strip() == name and \
                    f'XDG_RUNTIME_DIR={RUNTIME}'.encode() in Path(f'/proc/{pid}/environ').read_bytes().split(b'\0'):
                os.kill(int(pid), signal.SIGTERM)
        except OSError:
            pass


def start_gui():
    global GUI
    GUI = subprocess.Popen([str(BUILD / 'jarvis'), '--open', 'settings:desktop'], stdout=open(RUNTIME / 'jarvis.log', 'a'), stderr=subprocess.STDOUT)
    if not wait(lambda: settings_window(), 25):
        fail('the settings window did not open')
    time.sleep(2.5)


def stop_gui():
    GUI.send_signal(signal.SIGTERM)
    GUI.wait(timeout=10)


# ---- Set up: a 0.20 user's settings, JARVIS DESKTOP, the service ----
CONFIG.parent.mkdir(parents=True, exist_ok=True)
CONFIG.write_text(json.dumps({'api_key': KEY, 'language': 'en', 'learn_dialog': False, 'awareness_level': 'manual',
                              'awareness_screen': True, 'desktop': {'theme': 'mark7', 'dockSize': 44}}))
DATA.mkdir(parents=True, exist_ok=True)
(DATA / 'knowledge.json').write_text('{"facts": [{"slot": "name", "value": "Test"}]}')
# What the user taught: the facts. (Between 8 and 12 the service also notes the morning
# plan in the same file — that is not memory being touched.)
def memory():
    return json.loads((DATA / 'knowledge.json').read_text()).get('facts')
memory_before = memory()
daemon = subprocess.Popen([str(BUILD / 'jarvisd')], stdout=open(RUNTIME / 'jarvisd.log', 'w'), stderr=subprocess.STDOUT)
time.sleep(1.5)
if subprocess.run([str(BUILD / 'jarvis'), '--desktop-mode', 'jarvis'], capture_output=True, text=True, timeout=120).returncode != 0:
    fail('JARVIS DESKTOP could not be turned on')
if not wait(lambda: dock_panel().get('height'), 20):
    fail('no dock panel')
if config().get('config_version') != 2 or config().get('api_key') != KEY or config().get('learn_dialog') is not False:
    fail(f'upgrade lost something: {config()}')

start_gui()
# 0. Fits the screen.
area, win = screen(), settings_window()[0]
if win['x'] < area['x'] or win['y'] < area['y'] or win['x'] + win['w'] > area['x'] + area['w'] + 1 or win['y'] + win['h'] > area['y'] + area['h'] + 1:
    fail(f'the window {win} does not fit the screen {area}')
shot('00-desktop')

# 1. Dock height from the window → Plasma and config.json.
before = dock_panel()['height']
reveal('dock height')
shot('01-dock-row')
for _ in range(4):
    ei('key', 'up')
    time.sleep(0.4)
if not wait(lambda: dock_panel()['height'] == before + 4, 8):
    fail(f'the dock panel did not grow: {before} → {dock_panel()}')
if not wait(lambda: config().get('desktop', {}).get('dockSize') == before + 4):
    fail(f'config.json has another dock height: {config().get("desktop")}')
shot('02-dock-taller')
# …and from Plasma (the panel editor, a script here) → config.json and the window.
plasma('var ps = panels(); for (var i = 0; i < ps.length; ++i) { var ws = ps[i].widgets(); for (var j = 0; j < ws.length; ++j) if (ws[j].type === "org.jarvis.dock") ps[i].height = 50; }')
# Plasma writes its files a few seconds later.
if not wait(lambda: config().get('desktop', {}).get('dockSize') == 50, 40):
    fail(f'a height set in Plasma did not reach config.json: {config().get("desktop")}')
shot('03-dock-from-plasma')

# …and one of the dock's own settings (its context menu has the same): the icon size.
def dock_config(key):
    return plasma('var v = "";\nvar ps = panels();\nfor (var i = 0; i < ps.length; ++i) { var ws = ps[i].widgets();\n'
                  ' for (var j = 0; j < ws.length; ++j) if (ws[j].type === "org.jarvis.dock") { ws[j].currentConfigGroup = ["General"]; v = String(ws[j].readConfig("%s")); } }\n'
                  'print(v);' % key).strip()
icons = int(dock_config('iconSize') or 62)
reveal('icon size in the dock')
for _ in range(3):
    ei('key', 'right')
    time.sleep(0.3)
if not wait(lambda: dock_config('iconSize') == str(icons + 3), 8):
    fail(f'the dock icon size did not reach the dock: {icons} → {dock_config("iconSize")}')
shot('03b-dock-icons')

# 2. A synonym, then KDE's own module.
search('headphones')
shot('04-search-headphones')
ei('key', 'enter')
time.sleep(1.0)
shot('05-sound-row')
ei('key', 'space')
if not wait(lambda: [w for w in windows() if 'systemsettings' in w['cls']], 20):
    fail('System Settings did not open')
shot('06-kde-sound')
session_kill('systemsettings')
time.sleep(1)
# Back to the settings window, the way a person would: open it again.
subprocess.run([str(BUILD / 'jarvis'), '--open', 'settings'], capture_output=True, timeout=20)
kwin_script('var ws = workspace.windowList(); for (var i = 0; i < ws.length; ++i) if (String(ws[i].caption).indexOf("Jarvis settings") === 0) workspace.activeWindow = ws[i]; PRINT("[]");\n')
if not wait(lambda: [w for w in settings_window() if w['active']], 6):
    fail('the settings window did not come back to the front')

# 3. A key that KWin uses is refused; a free one is set.
reveal('key for quick commands')
ei('key', 'space')
time.sleep(0.8)
ei('key', 'super+d')
time.sleep(0.8)
shot('07-key-conflict')
ei('key', 'ctrl+alt+k')
time.sleep(0.8)
shot('08-key-free')
ei('key', 'tab', 'tab', 'tab', 'space')
time.sleep(1.0)
keys = subprocess.run(['gdbus', 'call', '--session', '-d', 'org.kde.kglobalaccel', '-o', '/kglobalaccel', '-m', 'org.kde.KGlobalAccel.shortcut',
                       "['org.jarvis.Jarvis.desktop', 'Quick', '', '']"], capture_output=True, text=True).stdout
if str(0x04000000 | 0x08000000 | 0x4b) not in keys:
    fail(f'Ctrl+Alt+K was not set for quick commands: {keys}')
shot('09-key-set')

# 4. A malformed key: refused, nothing saved.
reveal('claude api key')
ei('type', 'not a key')
ei('key', 'enter')
time.sleep(0.8)
shot('10-bad-key')
if config().get('api_key') != KEY:
    fail('a malformed key replaced the saved one')
# A well-formed but unknown key, checked with Anthropic on request (GET /v1/models/…, free):
# the answer (401, or "cannot be reached" offline) is for a person to read in the screenshot.
reveal('claude api key')
ei('key', 'ctrl+a', 'backspace')
ei('type', 'sk-ant-api03-not-a-real-key-0000000000')
reveal('check the connection')
ei('key', 'tab', 'space')
time.sleep(8)
shot('10b-connection-check')
if config().get('api_key') != KEY:
    fail('checking the connection saved the typed key')

# 5. Screen analysis off.
reveal('analysis of the screen')
ei('key', 'space')
if not wait(lambda: config().get('awareness_screen') is False):
    fail('screen analysis was not turned off')
shot('11-screen-off')

# 6. Reset the Desktop page: the look only.
reveal('theme')                    # the Desktop page
ei('key', 'ctrl+f', 'tab', 'tab')  # search → the pages → "Reset this page…"
time.sleep(0.4)
shot('12-reset-focus')
ei('key', 'space')
time.sleep(1.0)
ei('key', 'shift+tab')             # the focus starts on Cancel; the action is before it
time.sleep(0.4)
shot('12-reset-dialog')
ei('key', 'space')
if not wait(lambda: config().get('desktop', {}).get('theme') == 'classic', 8):
    fail(f'the Desktop reset did not bring the classic theme back: {config().get("desktop")}')
time.sleep(1.0)
shot('13-reset-done')
c = config()
if c.get('api_key') != KEY or c.get('learn_dialog') is not False or memory() != memory_before:
    fail(f'the Desktop reset touched more than the look: {c}')
# The dialog listed the dock height too: back to 44 px, in Plasma as well.
if not wait(lambda: dock_panel()['height'] == 44 and config().get('desktop', {}).get('dockSize') == 44, 10):
    fail(f'the reset did not bring the dock back to 44 px: {dock_panel()}, {config().get("desktop")}')
ei('key', 'enter')

# 7. Restart: everything is still there.
stop_gui()
start_gui()
c = config()
if c.get('desktop', {}).get('theme') != 'classic' or c.get('awareness_screen') is not False or c.get('desktop', {}).get('dockSize') != 44:
    fail(f'values lost after a restart: {c}')
shot('14-after-restart')

# 7b. "What Jarvis can do" (0.22.1): found by the search, the catalogue with what is
# available here; its own search filters it; an example goes into the main window's
# message field and changes nothing.
before = config()
reveal('what can you do')
time.sleep(4)  # the service checks sound, Bluetooth, KWin… for the availability
shot('14b-capabilities')
win = settings_window()[0]
# The page's search field sits under the heading (offsets in the window at scale 1).
ei('click', win['x'] + 684, win['y'] + 153)
ei('type', 'mute')
time.sleep(0.8)
shot('14c-capabilities-search')
ei('key', 'tab', 'space')
if not wait(lambda: [w for w in windows() if w['caption'] == 'J.A.R.V.I.S.' and w['active']], 8):
    fail('an example did not bring up the main window')
time.sleep(1.0)
shot('14d-example-in-field')
if config() != before:
    fail('browsing the capabilities changed a setting')
stop_gui()  # the main window took the focus: back to the settings window alone for step 8
start_gui()

# 8. jarvisd stopped: the window says so and still saves.
daemon.send_signal(signal.SIGTERM)
daemon.wait(timeout=10)
time.sleep(3)
reveal('sound with reminders')
ei('key', 'space')
if not wait(lambda: config().get('reminder_sound') is False):
    fail('a setting was not saved while jarvisd was stopped')
shot('15-daemon-stopped')
stop_gui()
print('PASS: settings window — fits the screen, dock height both ways, synonym search and KDE Sound, key conflict and a new key,'
      ' malformed API key refused, screen analysis off, Desktop reset keeps key and memory, restart, without jarvisd; scale', SCALE)
print('screenshots:', SHOTS)
