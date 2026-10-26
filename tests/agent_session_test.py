#!/usr/bin/env python3
"""The agent with real windows (0.22), in an isolated Plasma session.

Run it inside tests/plasma-session.sh (a virtual KWin, a private bus, private
XDG directories — the real desktop is never touched):

  tests/plasma-session.sh <scratch dir> python3 tests/agent_session_test.py

It starts build/jarvisd and the Jarvis window (--tray), then asks the daemon
over D-Bus, as the chat would:
  1. "открой калькулятор"         → KCalc starts; its window appears (checked by KWin)
  2. "открой калькулятор" again   → no second copy: Jarvis switches to the window
  3. "открой Kate" + "переключись на калькулятор" → the calculator's window is active
  4. "закрой калькулятор"         → asks first (default permission); "нет" keeps it; "да" closes it
  5. "стоп" with nothing running  → says so
The agent's task file shows the verified result of each step.
"""
import json
import os
import subprocess
import sys
import time
from pathlib import Path

import dbus
import dbus.mainloop.glib
from gi.repository import GLib

ROOT = Path(__file__).resolve().parent.parent
BUILD = Path(os.environ.get('JARVIS_BUILD', ROOT / 'build'))
RUNTIME = Path(os.environ['XDG_RUNTIME_DIR'])
DATA = Path(os.environ['XDG_DATA_HOME'])
CONFIG = Path(os.environ['XDG_CONFIG_HOME'])

if os.environ.get('WAYLAND_DISPLAY') != 'jtest':
    sys.exit('run inside tests/plasma-session.sh')


def fail(message):
    print('FAIL:', message)
    for log in ('jarvisd.log', 'jarvis.log'):
        p = RUNTIME / log
        if p.exists():
            print('---', log, '---')
            print(p.read_text(errors='replace')[-3000:])
    sys.exit(1)


def kwin_windows():
    """The windows as KWin sees them, through a one-shot script writing to a file."""
    out = RUNTIME / 'windows.json'
    out.unlink(missing_ok=True)
    script = RUNTIME / 'list-windows.js'
    script.write_text('var l=[];var ws=workspace.windowList();for(var i=0;i<ws.length;++i){var w=ws[i];'
                      'if(!w.normalWindow||w.skipTaskbar)continue;'
                      'l.push({app:String(w.desktopFileName||w.resourceClass),active:w===workspace.activeWindow});}'
                      'print("JWINDOWS "+JSON.stringify(l));')
    bus = dbus.SessionBus()

    def call(method, signature='', *args):
        return bus.call_blocking('org.kde.KWin', '/Scripting', 'org.kde.kwin.Scripting', method, signature, args)
    if call('isScriptLoaded', 's', 'jtest-windows'):
        call('unloadScript', 's', 'jtest-windows')
    call('loadScript', 'ss', str(script), 'jtest-windows')  # loadScript is overloaded: say which one
    call('start')
    time.sleep(0.6)
    call('unloadScript', 's', 'jtest-windows')
    log = (RUNTIME / 'kwin.log').read_text(errors='replace')
    lines = [l for l in log.splitlines() if 'JWINDOWS ' in l]
    return json.loads(lines[-1].split('JWINDOWS ', 1)[1]) if lines else []


def has_window(app):
    return any(w['app'] == app for w in kwin_windows())


def task():
    try:
        return json.loads((DATA / 'jarvis' / 'agent-task.json').read_text()).get('task', {})
    except (OSError, ValueError):
        return {}


# ---- Start jarvisd and the window ----
(CONFIG / 'jarvis').mkdir(parents=True, exist_ok=True)
(CONFIG / 'jarvis' / 'config.json').write_text(json.dumps({'config_version': 2, 'agent_mode': 'local', 'language': 'ru'}))
env = dict(os.environ, JARVIS_SECRET_SERVICE='off')
daemon = subprocess.Popen([str(BUILD / 'jarvisd')], env=env, stdout=open(RUNTIME / 'jarvisd.log', 'w'), stderr=subprocess.STDOUT)
gui = subprocess.Popen([str(BUILD / 'jarvis'), '--tray'], env=env, stdout=open(RUNTIME / 'jarvis.log', 'w'), stderr=subprocess.STDOUT)

dbus.mainloop.glib.DBusGMainLoop(set_as_default=True)
bus = dbus.SessionBus()
for _ in range(100):
    if bus.name_has_owner('org.jarvis.Daemon1') and bus.name_has_owner('org.jarvis.Desktop1'):
        break
    time.sleep(0.2)
else:
    fail('jarvisd or the window did not start')
jarvis = dbus.Interface(bus.get_object('org.jarvis.Daemon1', '/org/jarvis/Daemon1'), 'org.jarvis.Daemon1')
replies = {}
bus.add_signal_receiver(lambda i, text: replies.__setitem__(int(i), str(text)), signal_name='Reply',
                        dbus_interface='org.jarvis.Daemon1')


def ask(text, timeout=30):
    rid = int(jarvis.Ask(text))
    if rid == 0:
        fail(f'rejected: {text}')
    loop = GLib.MainLoop()
    deadline = time.monotonic() + timeout

    def check():
        if rid in replies or time.monotonic() > deadline:
            loop.quit()
            return False
        return True
    GLib.timeout_add(50, check)
    loop.run()
    if rid not in replies:
        fail(f'no reply to «{text}»')
    print(f'» {text}\n  {replies[rid]}')
    return replies[rid]


CALC = 'org.kde.kcalc'
r = ask('открой калькулятор')
if not has_window(CALC):
    fail('KCalc has no window')
if task().get('status') != 'verified':
    fail(f'the open step was not verified: {task()}')

r = ask('открой калькулятор')
if 'переключился' not in r and 'уже открыт' not in r:
    fail('a second «open» should switch to the window, not start another copy')
if sum(1 for w in kwin_windows() if w['app'] == CALC) != 1:
    fail('two calculators')

ask('открой Kate', timeout=40)
for _ in range(20):
    if has_window('org.kde.kate'):
        break
    time.sleep(0.5)
r = ask('переключись на калькулятор')
active = [w['app'] for w in kwin_windows() if w['active']]
if active != [CALC]:
    fail(f'the calculator is not active: {active}')

r = ask('закрой калькулятор')
if 'Подтвердите' not in r or not has_window(CALC):
    fail('closing must ask first')
r = ask('нет')
if not has_window(CALC):
    fail('«нет» closed it anyway')
ask('закрой калькулятор')
r = ask('да')
time.sleep(0.5)
if has_window(CALC) or 'закрыт' not in r:
    fail('«да» did not close the calculator (or did not check it)')

# Optional (JARVIS_TEST_BROWSER=1): the default browser starts without any network — run the
# session under `unshare -c -n --keep-caps` to show it needs none.
if os.environ.get('JARVIS_TEST_BROWSER') == '1':
    r = ask('Открой браузер', timeout=40)
    browsers = [w['app'] for w in kwin_windows() if 'firefox' in w['app'].lower() or 'chrom' in w['app'].lower()]
    print('  browser windows:', browsers)
    if not browsers or ('открыт' not in r and 'переключился' not in r):
        fail('the browser did not open')

r = ask('стоп')
if 'ничего не выполняю' not in r:
    fail('stop with nothing running')

for p in (gui, daemon):
    p.terminate()
print('PASS: open (window checked by KWin), no second copy, switch to a window, close only after «да», '
      'stop — real windows in an isolated Plasma session')
