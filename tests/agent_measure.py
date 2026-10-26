#!/usr/bin/env python3
"""Measurements of the agent in a running jarvisd (0.22 release notes).

  JARVIS_DBUS_SERVICE=org.jarvis.Test python3 tests/agent_measure.py COMMAND...

Asks each COMMAND over D-Bus as the chat does and prints, per command, the time
from Ask to Reply, then the memory and processor use of jarvisd and of its
llama-server child while it works and after it is idle, and the graphics
card's memory (nvidia-smi, when there is one). Changes the real system like
Jarvis would; nothing here goes to the network.
"""
import json
import os
import subprocess
import sys
import threading
import time

import dbus
import dbus.mainloop.glib
from gi.repository import GLib

SERVICE = os.environ.get('JARVIS_DBUS_SERVICE', 'org.jarvis.Daemon1')


def pid_of(name, parent=None):
    out = subprocess.run(['pgrep', '-x', name] + (['-P', str(parent)] if parent else []), capture_output=True, text=True).stdout.split()
    return int(out[0]) if out else None


def proc(pid):
    """RSS in MB and the CPU seconds used so far."""
    try:
        with open(f'/proc/{pid}/status') as f:
            rss = next(int(l.split()[1]) for l in f if l.startswith('VmRSS:')) / 1024
        with open(f'/proc/{pid}/stat') as f:
            fields = f.read().rsplit(')', 1)[1].split()
        cpu = (int(fields[11]) + int(fields[12])) / os.sysconf('SC_CLK_TCK')
        return rss, cpu
    except (OSError, StopIteration):
        return None, None


def vram():
    try:
        out = subprocess.run(['nvidia-smi', '--query-gpu=memory.used,utilization.gpu', '--format=csv,noheader,nounits'],
                             capture_output=True, text=True, timeout=5).stdout.strip().split(',')
        return int(out[0]), int(out[1])
    except (OSError, ValueError, IndexError, subprocess.TimeoutExpired):
        return None, None


dbus.mainloop.glib.DBusGMainLoop(set_as_default=True)
bus = dbus.SessionBus()
daemon_pid = int(bus.call_blocking('org.freedesktop.DBus', '/org/freedesktop/DBus', 'org.freedesktop.DBus',
                                   'GetConnectionUnixProcessID', 's', [SERVICE]))
jarvis = dbus.Interface(bus.get_object(SERVICE, '/org/jarvis/Daemon1'), 'org.jarvis.Daemon1')
replies = {}
bus.add_signal_receiver(lambda i, text, **kw: replies.__setitem__(int(i), str(text)), signal_name='Reply',
                        dbus_interface='org.jarvis.Daemon1', bus_name=SERVICE)

peak = {'gpu': 0, 'vram': 0}
sampling = threading.Event()


def sampler():
    while not sampling.is_set():
        mem, util = vram()
        if mem is not None:
            peak['vram'] = max(peak['vram'], mem)
            peak['gpu'] = max(peak['gpu'], util)
        time.sleep(0.25)


rss0, cpu0 = proc(daemon_pid)
mem0, _ = vram()
print(json.dumps({'event': 'idle', 'jarvisd_rss_mb': round(rss0, 1), 'vram_mb': mem0}))
threading.Thread(target=sampler, daemon=True).start()
for text in sys.argv[1:]:
    t0 = time.monotonic()
    rid = int(jarvis.Ask(text))
    loop = GLib.MainLoop()

    def check():
        if rid in replies or time.monotonic() - t0 > 120:
            loop.quit()
            return False
        return True
    GLib.timeout_add(5, check)
    loop.run()
    ms = (time.monotonic() - t0) * 1000
    llama = pid_of('llama-server', daemon_pid)
    lrss, lcpu = proc(llama) if llama else (None, None)
    print(json.dumps({'cmd': text, 'ms': round(ms), 'reply': replies.get(rid, '<timeout>')[:300],
                      'jarvisd_rss_mb': round(proc(daemon_pid)[0], 1), 'llama_rss_mb': lrss and round(lrss, 1)}, ensure_ascii=False))
sampling.set()
rss1, cpu1 = proc(daemon_pid)
llama = pid_of('llama-server', daemon_pid)
lrss, lcpu = proc(llama) if llama else (None, None)
time.sleep(3)
_, cpu2 = proc(daemon_pid)
_, lcpu2 = proc(llama) if llama else (None, None)
mem_after, util_after = vram()
print(json.dumps({'event': 'after', 'jarvisd_rss_mb': round(rss1, 1), 'jarvisd_cpu_s_total': round(cpu1 - cpu0, 2),
                  'jarvisd_cpu_pct_idle_3s': round((cpu2 - cpu1) / 3 * 100, 1),
                  'llama_rss_mb': lrss and round(lrss, 1),
                  'llama_cpu_pct_idle_3s': round((lcpu2 - lcpu) / 3 * 100, 1) if llama else None,
                  'vram_peak_mb': peak['vram'], 'gpu_util_peak_pct': peak['gpu'], 'vram_after_mb': mem_after, 'gpu_util_after_pct': util_after}))
