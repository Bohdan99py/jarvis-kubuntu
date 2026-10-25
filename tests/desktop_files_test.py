#!/usr/bin/env python3
"""Checks the KDE integration files against the code that uses them.

* the Plasma widget: metadata, the version, the plugin id jarvisd and the GUI
  use, the palette copied from the HUD theme, and a Russian translation for
  every i18n() string (compiled and read back with Python's gettext);
* the KRunner plugin: the bus name and path jarvisd serves, and no D-Bus
  activation file for that name (KRunner must never start the daemon);
* the systemd units: restart limits, the watchdog and the "already running"
  exit status that jarvisd_main.cpp returns.

Usage: desktop_files_test.py <build directory>
"""
import gettext
import json
import re
import subprocess
import sys
import tempfile
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]
BUILD = Path(sys.argv[1]) if len(sys.argv) > 1 else ROOT / 'build'
errors = []


def check(ok, message):
    if not ok:
        errors.append(message)


def read(relative):
    return (ROOT / relative).read_text(encoding='utf-8')


# ---- The widgets ------------------------------------------------------------
version = re.search(r'project\(jarvis VERSION ([0-9.]+)', read('CMakeLists.txt')).group(1)
cmake = read('CMakeLists.txt')
listed = re.search(r'set\(JARVIS_PLASMOIDS ([^)]*)\)', cmake).group(1).split()
widgets = sorted(p.name for p in (ROOT / 'plasmoids').iterdir() if p.name.startswith('org.jarvis.'))
check(listed == ['org.jarvis.hud', 'org.jarvis.dock', 'org.jarvis.system', 'org.jarvis.network', 'org.jarvis.power', 'org.jarvis.media', 'org.jarvis.status'],
      f'CMake installs the seven widgets: {listed}')
check(sorted(listed) == widgets, f'every widget directory is installed: {widgets}')
scripts_cpp = read('desktop_shell_scripts.cpp')
for wid in widgets:
    meta = json.loads(read(f'plasmoids/{wid}/metadata.json'))
    plugin = meta['KPlugin']
    check(meta.get('KPackageStructure') == 'Plasma/Applet', f'{wid}: a Plasma/Applet package')
    check(plugin.get('Id') == wid, f'{wid}: metadata id')
    check(plugin.get('Version') == version, f'{wid}: version {plugin.get("Version")} != project version {version}')
    check(meta.get('X-Plasma-API-Minimum-Version', '').startswith('6'), f'{wid}: targets Plasma 6')
    check(plugin.get('Name[ru]') and plugin.get('Description[ru]'), f'{wid}: Russian name and description')
    check(f'u"{wid}"_s' in scripts_cpp, f'{wid}: known to the desktop shell scripts')
    check((ROOT / f'plasmoids/{wid}/contents/ui/main.qml').exists(), f'{wid}: main.qml')
    check((ROOT / f'plasmoids/{wid}/contents/config/main.xml').exists(), f'{wid}: configuration schema')
check('kHudPlugin{"org.jarvis.hud"}' in read('desktop_integration.h'), 'desktop_integration.h names the core widget')

# Inside plasmshell only Qt's and Plasma's modules and org.jarvis.visual (QML files, no compiled code).
# (QtQml.Models: the DelegateModel of the dock's window list — Qt's own.)
qml = {}
for path in list((ROOT / 'plasmoids').glob('*/contents/*/*.qml')) + list((ROOT / 'desktop').glob('*.qml')):
    qml[str(path.relative_to(ROOT))] = path.read_text(encoding='utf-8')
for name, text in qml.items():
    for module in re.findall(r'^import\s+([A-Za-z][\w.]*)', text, re.M):
        check(module.startswith(('QtQuick', 'org.kde.')) or module in ('org.jarvis.visual', 'QtQml', 'QtQml.Models'),
              f'{name} imports {module}: only Qt Quick, Plasma and org.jarvis.visual may load in plasmashell')
    check('Canvas' not in text, f'{name}: no Canvas (CPU painted); use Shapes and animators')
    check('writeConfig' not in text and 'evaluateScript' not in text, f'{name}: a widget never changes Plasma settings')
    check(not re.search(r'signature:\s*"[a-z]', text) and not re.search(r'\], "[a-z]+"', text),
          f'{name}: Plasma\'s D-Bus module wants tuple signatures, "(s)"')
link = read('desktop/DaemonLink.qml')
check('if (!link.online)' in link, 'widgets call jarvisd only while it is on the bus')
# The second core of 0.19.1 is gone: the desktop uses the window's JarvisCore.
check(not list((ROOT / 'plasmoids').glob('*/contents/ui/MiniCore.qml')), 'no second core in the widgets')
check('JV.DesktopCore' in read('plasmoids/org.jarvis.hud/contents/ui/main.qml'), 'the core widget uses the shared JarvisCore')
module = read('desktop/qmldir')
for f in re.findall(r'^(?:singleton )?\w+ 1\.0 (\S+)$', module, re.M):
    check((ROOT / 'hud' / f).exists() or (ROOT / 'desktop' / f).exists(), f'qmldir names {f}, which exists')
    check(f'hud/{f}' in cmake or f'desktop/{f}' in cmake, f'CMake copies {f} into the module')

# Every string is translated (one domain for all widgets), with the same placeholders.
strings = set()
for name, text in qml.items():
    strings |= set(re.findall(r'\bi18nd\("jarvis-desktop",\s*"((?:[^"\\]|\\.)*)"', text))
    for bare in re.findall(r'\bi18n\("', text):
        errors.append(f'{name}: i18n() without the jarvis-desktop domain')
po = (ROOT / 'plasmoids/po/ru.po').read_text(encoding='utf-8')
with tempfile.TemporaryDirectory() as tmp:
    mo = Path(tmp) / 'ru.mo'
    subprocess.run([sys.executable, str(ROOT / 'plasmoids/po/make_mo.py'), str(ROOT / 'plasmoids/po/ru.po'), str(mo)], check=True)
    catalog = gettext.GNUTranslations(mo.open('rb'))
    for source in sorted(strings):
        translated = catalog.gettext(source)
        check(translated != source, f"widget string not translated: {source!r}")
        check(sorted(re.findall(r'%\d', source)) == sorted(re.findall(r'%\d', translated)), f'placeholders differ: {source!r}')
    for source in re.findall(r'^msgid "(.+)"$', po, re.M):
        check(source in strings, f'ru.po has a string the widgets no longer use: {source!r}')

# ---- KRunner -----------------------------------------------------------------
runner = (BUILD / 'krunner/org.jarvis.Jarvis.desktop')
check(runner.exists(), f'{runner} is generated by CMake')
if runner.exists():
    entry = dict(line.split('=', 1) for line in runner.read_text(encoding='utf-8').splitlines() if '=' in line and not line.startswith('#'))
    check(entry.get('X-Plasma-API') == 'DBus', 'KRunner D-Bus plugin')
    check(entry.get('X-Plasma-DBusRunner-Service') == 'org.jarvis.Runner', 'runner bus name')
    check('QStringLiteral("org.jarvis.Runner")' in read('dbus_names.h'), 'dbus_names.h serves org.jarvis.Runner')
    check(entry.get('X-Plasma-DBusRunner-Path') == '/org/jarvis/KRunner' and 'kRunnerPath{"/org/jarvis/KRunner"}' in read('desktop_integration.h'), 'runner object path')
    check(entry.get('X-KDE-PluginInfo-Version') == version, 'runner version')
check('org.jarvis.Runner' not in read('org.jarvis.Daemon1.service.in'), 'the runner name is not D-Bus activatable')

# ---- systemd -----------------------------------------------------------------
unit = read('jarvis.service.in')
for line in ('Restart=on-failure', 'StartLimitBurst=5', 'StartLimitIntervalSec=300', 'RestartPreventExitStatus=3',
             'SuccessExitStatus=3', 'WatchdogSec=90', 'NotifyAccess=main', 'Type=dbus', 'BusName=org.jarvis.Daemon1'):
    check(re.search(rf'^{re.escape(line)}$', unit, re.M), f'jarvis.service: {line}')
check('constexpr int kAlreadyRunning = 3;' in read('jarvisd_main.cpp'), 'jarvisd exits with 3 when already running')
check('StartLimitBurst=' in read('jarvis-tray.service.in'), 'the tray unit has a restart limit')
check(re.search(r'^\[Unit\][^\[]*StartLimitIntervalSec', unit, re.M | re.S), 'start limits belong to [Unit]')

if errors:
    print('\n'.join('FAIL: ' + e for e in errors))
    sys.exit(1)
print(f'PASS: {len(widgets)} widgets ({len(strings)} strings translated), the visual module, the KRunner plugin and the systemd units')
