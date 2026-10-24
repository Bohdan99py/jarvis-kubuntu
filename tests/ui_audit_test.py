#!/usr/bin/env python3
"""Static audit of every interactive element of Jarvis's QML (0.19.3).

A control that looks interactive must do something:
  1. every button, menu item, switch and check box has a handler
     (onClicked / onTriggered / onToggled / onActivated …) or a bound action;
  2. every MouseArea / TapHandler handles a click, a press or a tap — or is a
     hover sensor whose id something reads (containsMouse, hovered);
  3. in the dock (a Plasma panel) tool tips are Plasma's own windows, never Qt
     Quick tool tips inside the panel, which land on the very button and
     swallow its click (0.19.2's "Start does nothing");
  4. the dock's popups are applet popups that close on focus loss with a grace
     period — never stays-on-top (libplasma makes those a second panel) and
     never hideOnWindowDeactivate (a click on the panel hid them at once);
  5. every i18nd() call passes as many arguments as its text has %N.
Usage: ui_audit_test.py  (from anywhere; reads the source tree)
"""
import re
import sys
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]
FILES = sorted(list(ROOT.glob('*.qml')) + list(ROOT.glob('hud/*.qml')) + list(ROOT.glob('desktop/*.qml'))
               + list(ROOT.glob('plasmoids/*/contents/ui/*.qml')))

BUTTONS = ('HoloButton', 'HudButton', 'DarkButton', 'QQC2.Button', 'QQC2.ToolButton', 'QQC2.MenuItem', 'QQC2.CheckBox',
           'QQC2.Switch', 'Button', 'ToolButton', 'MenuItem', 'DarkMenuItem', 'CheckBox', 'Switch', 'RoundButton',
           'PlasmaExtras.MenuItem', 'OptionSwitch', 'DarkCombo', 'ComboBox', 'QQC2.ComboBox', 'SpinBox', 'QQC2.Slider', 'Slider')
HANDLERS = re.compile(r'\b(onClicked|onTriggered|onToggled|onActivated|onAccepted|onValueModified|onMoved|onPressed|onReleased|'
                      r'onTapped|onDoubleClicked|onPressAndHold|onCheckedChanged|onCurrentIndexChanged|onValueChanged|action|'
                      r'onDoubleTapped|onSingleTapped|onLongPressed)\s*[:{]')
problems = []


def blocks(text, name):
    """(start line, body) of every `name {` instance (balanced braces; strings skipped)."""
    out = []
    first = re.search(r'^[A-Z][\w.]*\s*\{', text, re.M)
    for m in re.finditer(r'(?<![\w.])' + re.escape(name) + r'\s*\{', text):
        before = text[text.rfind('\n', 0, m.start()) + 1:m.start()]
        # A definition, not a use: `component Foo: Bar {`, the file's root (the file is
        # the component), or a template inside `Component {` (its user connects the action).
        if re.search(r'component\s+\w+\s*:\s*$', before) or 'Component {' in before or (first and first.start() == m.start()):
            continue
        i, depth, quote = m.end(), 1, None
        while i < len(text) and depth:
            c = text[i]
            if quote:
                if c == '\\':
                    i += 1
                elif c == quote:
                    quote = None
            elif c in '"\'':
                quote = c
            elif c == '{':
                depth += 1
            elif c == '}':
                depth -= 1
            i += 1
        out.append((text.count('\n', 0, m.start()) + 1, text[m.end():i - 1]))
    return out


def own(body):
    """The block's own lines: nested object blocks removed (a child's handler is not ours)."""
    out, depth = [], 0
    for line in body.split('\n'):
        stripped = re.sub(r'"(?:[^"\\]|\\.)*"', '""', line)
        if depth == 0:
            out.append(line)
        depth += stripped.count('{') - stripped.count('}')
        depth = max(depth, 0)
    return '\n'.join(out)


for path in FILES:
    rel = str(path.relative_to(ROOT))
    text = path.read_text(encoding='utf-8')
    # The component definitions (component Foo: Bar {…}) declare signals; instances are what counts.
    for name in BUTTONS:
        for line, body in blocks(text, name):
            mine = own(body)
            if HANDLERS.search(mine) or re.search(r'\bmodel\s*:', mine) and 'Combo' in name:
                continue
            if re.search(r'\b(visible|enabled)\s*:\s*false\b', mine) or re.search(r'Kirigami\.FormData\.isSection', mine):
                continue
            # A check box or slider bound to a setting by alias (cfg_x: id.checked) is wired by its page.
            ident = re.search(r'\bid\s*:\s*(\w+)', mine)
            if ident and re.search(r'property alias cfg_\w+\s*:\s*' + ident.group(1) + r'\.', text):
                continue
            if ident and re.search(r'\b' + ident.group(1) + r'\.(checked|value|currentIndex)\b', text.replace(mine, '')):
                continue
            problems.append(f'{rel}:{line}: {name} without an action (looks interactive, does nothing)')
    for name in ('MouseArea', 'TapHandler'):
        for line, body in blocks(text, name):
            mine = own(body)
            if HANDLERS.search(mine) or re.search(r'\benabled\s*:\s*false\b', mine):
                continue
            ident = re.search(r'\bid\s*:\s*(\w+)', mine)
            if name == 'MouseArea' and ident and re.search(r'\b' + ident.group(1) + r'\.(containsMouse|pressed|mouseX|mouseY)\b', text):
                continue  # a hover sensor
            # An input shield, said so in a comment on its line ("clicks … do not reach …").
            line_text = text.split('\n')[line - 1]
            if name == 'MouseArea' and re.search(r'//.*(do not reach|block|shield)', line_text):
                continue
            problems.append(f'{rel}:{line}: {name} that handles nothing')
    # i18nd arguments.
    for m in re.finditer(r'\bi18nd\("jarvis-desktop",\s*"((?:[^"\\]|\\.)*)"((?:[^()]|\([^()]*\))*)\)', text):
        wanted = len(set(re.findall(r'%(\d)', m.group(1))))
        given = 0
        rest = m.group(2).strip()
        if rest.startswith(','):
            depth, given = 0, 1
            for c in rest[1:]:
                depth += c in '([{'
                depth -= c in ')]}'
                given += c == ',' and depth == 0
        if wanted != given:
            line = text.count('\n', 0, m.start()) + 1
            problems.append(f'{rel}:{line}: i18nd "{m.group(1)}" wants {wanted} argument(s), gets {given}')

# The dock: Plasma's tool tips, applet popups.
dock = ROOT / 'plasmoids/org.jarvis.dock/contents/ui'
for path in dock.glob('*.qml'):
    rel = str(path.relative_to(ROOT))
    text = path.read_text(encoding='utf-8')
    if path.name in ('main.qml', 'DockTask.qml') and re.search(r'QQC2\.ToolTip\.(visible|text)', text):
        problems.append(f'{rel}: a Qt Quick tool tip inside the panel covers the button and swallows its click; use PlasmaCore.ToolTipArea')
    for line, body in blocks(text, 'PlasmaCore.Dialog'):
        mine = own(body)
        if 'WindowStaysOnTopHint' in mine:
            problems.append(f'{rel}:{line}: a stays-on-top dialog becomes a panel surface on Wayland (the 0.19.2 launcher bug)')
        if re.search(r'hideOnWindowDeactivate\s*:\s*true', mine):
            problems.append(f'{rel}:{line}: hideOnWindowDeactivate in a panel popup: the click on the panel hides it at once')
        if 'AppletPopup' not in mine:
            problems.append(f'{rel}:{line}: a panel popup must be an AppletPopup (KWin places and focuses it)')
main = (dock / 'main.qml').read_text(encoding='utf-8')
if 'function onActivated()' not in main:
    problems.append('dock: the Meta key / applet shortcut (Plasmoid.activated) does not open the launcher')
if not re.search(r'onActiveChanged:.*closer', main):
    problems.append('dock: popups do not close on focus loss')

if problems:
    print('\n'.join('FAIL: ' + p for p in problems))
    sys.exit(1)
print(f'PASS: {len(FILES)} QML files — every control has an action, the dock\'s tool tips and popups are Plasma\'s, i18nd arguments match')
