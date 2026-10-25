#!/usr/bin/env python3
"""JARVIS DESKTOP on and off in a real (nested, isolated) Plasma 6 session (0.20).

    tests/run_desktop_shell_test.sh [build dir]

Runs inside tests/plasma-session.sh with the widgets in the scratch data dir and a
stub `systemctl` first in PATH (no systemd user manager on the test bus: the
desktop shell then stops plasmashell with kquitapp6, on the test bus only). jarvisd
is not running. Checks, through `jarvis --desktop-mode jarvis` and
`jarvis --restore-desktop` — the commands people use:
  1. turning it on adds one dock panel and one of each Jarvis widget, and removes
     the standard panels;
  2. turning it on again (what an update or a second click does) adds nothing;
  3. after a plasmashell restart (a new login) it is all still there, once;
  4. --restore-desktop puts the standard panels back exactly and leaves no
     Jarvis widget behind.
"""
import json
import os
import subprocess
import sys
import time
from pathlib import Path

if os.environ.get('WAYLAND_DISPLAY') != 'jtest':
    sys.exit('desktop_shell_session_test.py: run it through tests/plasma-session.sh')
BUILD = Path(sys.argv[1]).resolve()
RUNTIME = Path(os.environ['XDG_RUNTIME_DIR'])


def plasma(script):
    out = subprocess.run(['qdbus6', 'org.kde.plasmashell', '/PlasmaShell', 'org.kde.PlasmaShell.evaluateScript', script],
                         capture_output=True, text=True, timeout=20).stdout.strip()
    return json.loads(out) if out else None


def layout():
    """{panels: [[widget types]], desktop: {type: count}} — the Jarvis parts and the panels."""
    return plasma('''
var out = { panels: [], desktop: {} };
var ps = panels();
for (var i = 0; i < ps.length; ++i) out.panels.push(ps[i].widgets().map(function (w) { return w.type; }));
var ds = desktops();
for (var j = 0; j < ds.length; ++j) { var ws = ds[j].widgets(); for (var k = 0; k < ws.length; ++k) { var t = ws[k].type; out.desktop[t] = (out.desktop[t] || 0) + 1; } }
print(JSON.stringify(out));''')


def plasma_up(seconds=30):
    end = time.monotonic() + seconds
    while time.monotonic() < end:
        if subprocess.run(['gdbus', 'call', '--session', '-d', 'org.kde.plasmashell', '-o', '/PlasmaShell', '-m',
                           'org.freedesktop.DBus.Peer.Ping'], capture_output=True).returncode == 0:
            time.sleep(4)
            return True
        time.sleep(0.3)
    return False


def jarvis(*args):
    r = subprocess.run([str(BUILD / 'jarvis'), *args], capture_output=True, text=True, timeout=180)
    print('\n'.join('    | ' + l for l in (r.stdout + r.stderr).strip().splitlines()[-6:]))
    return r.returncode


def fail(message):
    print('FAIL:', message)
    sys.exit(1)


def jarvis_counts(lay):
    docks = sum(1 for p in lay['panels'] if 'org.jarvis.dock' in p)
    widgets = {t: n for t, n in lay['desktop'].items() if t.startswith('org.jarvis.')}
    return docks, widgets


original = layout()
if not original or not original['panels']:
    fail(f'no standard layout to start from: {original}')
print('standard layout:', original['panels'])

# 1. On.
if jarvis('--desktop-mode', 'jarvis') != 0:
    fail('jarvis --desktop-mode jarvis failed')
time.sleep(2)
on = layout()
docks, widgets = jarvis_counts(on)
if docks != 1 or any(n != 1 for n in widgets.values()) or len(widgets) != 6:
    fail(f'turning it on: {docks} dock panel(s), widgets {widgets}')
if len(on['panels']) != 1 or not {'org.kde.plasma.systemtray', 'org.kde.plasma.digitalclock'} <= set(on['panels'][0]):
    fail(f'turning it on: the panels are {on["panels"]} (one dock panel with the tray and the clock expected)')
print('ok: on — one dock panel (with the tray and the clock), one of each Jarvis widget, the standard panels gone')

# 2. On again.
if jarvis('--desktop-mode', 'jarvis') != 0:
    fail('the second jarvis --desktop-mode jarvis failed')
time.sleep(2)
again = layout()
if jarvis_counts(again) != (docks, widgets) or len(again['panels']) != 1:
    fail(f'turning it on twice duplicated something: {again}')
print('ok: on again — nothing added twice')

# 3. A new login.
for pid in subprocess.run(['pgrep', '-u', str(os.getuid()), '-x', 'plasmashell'], capture_output=True, text=True).stdout.split():
    try:
        if f'XDG_RUNTIME_DIR={RUNTIME}'.encode() in Path(f'/proc/{pid}/environ').read_bytes().split(b'\0'):
            subprocess.run(['kill', pid])
    except OSError:
        pass
time.sleep(2)
subprocess.Popen(['plasmashell', '--no-respawn'], stdout=open(RUNTIME / 'plasmashell2.log', 'w'), stderr=subprocess.STDOUT,
                 stdin=subprocess.DEVNULL, start_new_session=True)
if not plasma_up():
    fail('plasmashell did not come back')
after = layout()
if jarvis_counts(after) != (docks, widgets) or len(after['panels']) != 1:
    fail(f'after a plasmashell restart: {after}')
print('ok: after a plasmashell restart — the same dock panel and widgets, once')

# 4. Back to the standard desktop.
if jarvis('--restore-desktop') != 0:
    fail('jarvis --restore-desktop failed')
if not plasma_up():
    fail('plasmashell did not come back after the restore')
back = layout()
if back['panels'] != original['panels']:
    fail(f'the restore did not put the panels back: {back["panels"]} instead of {original["panels"]}')
if jarvis_counts(back) != (0, {}):
    fail(f'Jarvis parts left after the restore: {back}')
print('ok: --restore-desktop — the standard panels are back exactly, no Jarvis widget left')
print('PASS: JARVIS DESKTOP on, on again, a new login, restore — in an isolated Plasma session, without jarvisd')
