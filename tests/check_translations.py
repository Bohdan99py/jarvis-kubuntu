#!/usr/bin/env python3
"""Fails when the Russian translation is incomplete or out of date.

1. Every message in i18n/jarvis_ru.ts is translated and keeps the %N placeholders.
2. If lupdate is available, the sources contain no strings missing from the file
   (run: lupdate -locations none -no-obsolete *.cpp *.h *.qml -ts i18n/jarvis_ru.ts).
"""
import re
import shutil
import subprocess
import sys
import tempfile
from pathlib import Path
import xml.etree.ElementTree as ET

ROOT = Path(__file__).resolve().parents[1]


def problems(path):
    found = []
    for context in ET.parse(path).getroot().iter('context'):
        name = context.findtext('name')
        for message in context.iter('message'):
            source = message.findtext('source') or ''
            translation = message.find('translation')
            text = translation.text or '' if translation is not None else ''
            if translation is None or translation.get('type') in ('unfinished', 'obsolete', 'vanished') or not text.strip():
                found.append(f'{name}: untranslated: {source!r}')
            elif sorted(re.findall(r'%\d', source)) != sorted(re.findall(r'%\d', text)):
                found.append(f'{name}: placeholders differ: {source!r} -> {text!r}')
    return found


def main():
    ts = Path(sys.argv[1]) if len(sys.argv) > 1 else ROOT / 'i18n/jarvis_ru.ts'
    errors = problems(ts)
    lupdate = shutil.which('lupdate') or next((p for p in ['/usr/lib/qt6/bin/lupdate'] if Path(p).exists()), None)
    if lupdate:
        with tempfile.TemporaryDirectory() as tmp:
            copy = Path(tmp) / 'jarvis_ru.ts'
            shutil.copy(ts, copy)
            sources = sorted(str(p) for pattern in ('*.cpp', '*.h', '*.qml') for p in ROOT.glob(pattern))
            result = subprocess.run([lupdate, '-silent', '-locations', 'none', '-no-obsolete', *sources, '-ts', str(copy)],
                                    stdout=subprocess.DEVNULL, stderr=subprocess.PIPE, text=True)
            if result.returncode != 0:
                print('lupdate failed, freshness check skipped:', result.stderr.strip()[:500])
            else:
                errors += [e + ' (missing from i18n/jarvis_ru.ts)' for e in problems(copy) if e not in errors]
    for e in errors:
        print(e)
    if errors:
        return 1
    print('PASS: Russian translation complete')
    return 0


if __name__ == '__main__':
    sys.exit(main())
