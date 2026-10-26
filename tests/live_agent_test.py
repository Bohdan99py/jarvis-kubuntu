#!/usr/bin/env python3
"""0.22.1: laptop tasks through the real user path on the real sound server.

A second jarvisd from the build runs next to the installed one under its own
bus name (JARVIS_DBUS_SERVICE), with an empty home for its config and memory,
but with the real PipeWire. Requests go in through D-Bus Ask — the call the
Jarvis window makes — and the result is read back with pactl, not from the
reply. The volume and mute state of the default output are saved first and put
back at the end, whatever happens.

    python3 tests/live_agent_test.py [build-dir]

It changes the real volume for a few seconds; nothing else is touched.
"""
import functools
import json
import os
import re
import signal
import subprocess
import sys
import tempfile
import threading
import time
from pathlib import Path

print = functools.partial(print, flush=True)

build = Path(sys.argv[1] if len(sys.argv) > 1 else Path(__file__).resolve().parent.parent / 'build')
service = 'org.jarvis.LiveTest' + str(os.getpid())
scratch = Path(tempfile.mkdtemp(prefix='jarvis-live-'))
env = os.environ.copy()
env.update(JARVIS_DBUS_SERVICE=service, XDG_DATA_HOME=str(scratch / 'data'), XDG_CONFIG_HOME=str(scratch / 'config'),
           ANTHROPIC_API_KEY='', JARVIS_AGENT_TASK=str(scratch / 'agent-task.json'))
config = scratch / 'config/jarvis/config.json'
config.parent.mkdir(parents=True)
config.write_text(json.dumps(dict(api_key='', language='ru', reply_language='auto', learn_dialog=False, track_activity=False,
                                  track_titles=False, share_activity=False, agent_mode='local', desktop_integration=False)))


def pactl(*args):
    # LC_ALL=C: "Mute: yes", not "Mute: да".
    return subprocess.run(['pactl', *args], capture_output=True, text=True, timeout=5, env=dict(os.environ, LC_ALL='C')).stdout


def volume():
    m = re.search(r'(\d+)%', pactl('get-sink-volume', '@DEFAULT_SINK@'))
    return int(m.group(1)) if m else -1


def muted():
    return 'yes' in pactl('get-sink-mute', '@DEFAULT_SINK@')


def call(method, *args):
    return subprocess.check_output(['gdbus', 'call', '--session', '--dest', service, '--object-path', '/org/jarvis/Daemon1',
                                    '--method', 'org.jarvis.Daemon1.' + method, *args], text=True, stderr=subprocess.DEVNULL)


replies = {}
routes = {}
monitors = []


def listen():
    # gdbus writes to a pipe in blocks: line buffering, or the replies arrive at exit.
    mon = subprocess.Popen(['stdbuf', '-oL', 'gdbus', 'monitor', '--session', '--dest', service], stdout=subprocess.PIPE, text=True,
                           start_new_session=True)
    monitors.append(mon)
    for line in mon.stdout:
        m = re.search(r"\.Reply \(uint64 (\d+), ['\"](.*)['\"]\)$", line.strip())
        if m:
            replies[int(m.group(1))] = m.group(2).replace('\\n', '\n')
        r = re.search(r"\.Routed \(uint64 (\d+), '([^']*)'\)", line)
        if r:
            routes.setdefault(int(r.group(1)), []).append(r.group(2))


def ask(text, timeout=12):
    rid = int(re.search(r'uint64 (\d+)', call('Ask', text)).group(1))
    deadline = time.time() + timeout
    while rid not in replies and time.time() < deadline:
        time.sleep(0.05)
    return replies.get(rid, '<no reply>'), routes.get(rid, [])


sink = pactl('get-default-sink').strip()
saved_volume, saved_mute = volume(), muted()
print(f'default output {sink}: {saved_volume}%, muted={saved_mute} (put back at the end)')
# A signal still puts the volume back (the finally below runs).
for sig in (signal.SIGTERM, signal.SIGINT, signal.SIGHUP):
    signal.signal(sig, lambda *_: sys.exit(2))
proc = subprocess.Popen([str(build / 'jarvisd')], env=env, stdout=open(scratch / 'jarvisd.log', 'w'), stderr=subprocess.STDOUT,
                        start_new_session=True)
results = []
try:
    for _ in range(100):
        try:
            call('Version')
            break
        except subprocess.CalledProcessError:
            time.sleep(0.1)
    threading.Thread(target=listen, daemon=True).start()
    time.sleep(0.5)
    pactl('set-sink-mute', '@DEFAULT_SINK@', '0')
    pactl('set-sink-volume', '@DEFAULT_SINK@', '46%')

    # (request, what pactl must show afterwards, what the reply must say, must it change something)
    checks = [
        ('put sound on 20', lambda: volume() == 20, 'Volume 20%', True),
        ('нет, 30', lambda: volume() == 30, '30%', True),
        ('Turn it down', lambda: volume() == 20, 'Volume 20%', True),
        ('верни как было', lambda: volume() == 30, '30%', True),
        ('Сделай звук на двадцать пять', lambda: volume() == 25, '25%', True),
        ('чуть тише', lambda: volume() == 20, '20%', True),
        ('Как поставить громкость 20%?', lambda: volume() == 20, 'ничего не меняю', False),
        ('Не меняй громкость', lambda: volume() == 20, 'не трогаю', False),
        ('Если я попрошу поставить громкость 40%, что ты сделаешь?', lambda: volume() == 20, 'Если попросите', False),
        ('Mute the sound', lambda: muted(), 'Muted', True),
        ('Unmute', lambda: not muted(), 'Unmuted', True),
        ('громкость 300', lambda: volume() == 20, 'не ставлю', False),
    ]
    for text, state, said, changes in checks:
        reply, route = ask(text)
        time.sleep(0.2)
        ok_state, ok_reply = state(), said in reply
        honest = "can't" not in reply and 'не могу' not in reply and 'wpctl' not in reply
        results.append((text, ok_state, ok_reply, honest, route))
        print(f"{'ok ' if ok_state and ok_reply and honest else 'FAIL'} {text!r}: volume {volume()}%, muted={muted()}, route={route}\n     → {reply[:160]}")
finally:
    pactl('set-sink-volume', sink, f'{saved_volume}%')
    pactl('set-sink-mute', sink, '1' if saved_mute else '0')
    proc.terminate()
    proc.wait(10)
    for mon in monitors:
        mon.terminate()
    print(f'put back: {volume()}%, muted={muted()}')

failed = [r for r in results if not (r[1] and r[2] and r[3])]
print('PASS: live agent through D-Bus Ask' if not failed else f'FAILED: {len(failed)} of {len(results)}')
sys.exit(1 if failed else 0)
