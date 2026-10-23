#!/usr/bin/env python3
"""The graphics card choice of `jarvis --gpu-info` on fake hybrid laptops.

Usage: graphics_test.py <path to jarvis>
A fake /sys (JARVIS_SYSFS_ROOT) describes the GPUs; the report must name them,
mark the boot GPU, and set exactly the PRIME offload variables for the choice.
"""
import os
import re
import subprocess
import sys
import tempfile
from pathlib import Path

JARVIS = sys.argv[1]


def fake_sys(root, cards):
    """cards: [(slot, vendor, device, driver, boot)] → /sys/class/drm/cardN like the kernel's."""
    for n, (slot, vendor, device, driver, boot) in enumerate(cards):
        dev = root / 'devices' / slot
        (dev / 'power').mkdir(parents=True)
        (dev / 'vendor').write_text(f'0x{vendor}\n')
        (dev / 'device').write_text(f'0x{device}\n')
        (dev / 'boot_vga').write_text('1\n' if boot else '0\n')
        (dev / 'power' / 'runtime_status').write_text('suspended\n')
        (root / 'drivers' / driver).mkdir(parents=True, exist_ok=True)
        (dev / 'driver').symlink_to(root / 'drivers' / driver)
        card = root / 'class' / 'drm' / f'card{n}'
        card.mkdir(parents=True)
        (card / 'device').symlink_to(dev)
        # Connectors are not cards.
        (root / 'class' / 'drm' / f'card{n}-eDP-1').mkdir()


def report(cards, *args, env=None, config=None, keep=None):
    with tempfile.TemporaryDirectory() as tmp:
        root = Path(tmp) / 'sys'
        fake_sys(root, cards)
        if config:
            (Path(tmp) / 'config' / 'Jarvis').mkdir(parents=True)
            (Path(tmp) / 'config' / 'Jarvis' / 'jarvis.conf').write_text(config)
        environment = {k: v for k, v in os.environ.items()
                       if k not in ('__NV_PRIME_RENDER_OFFLOAD', '__GLX_VENDOR_LIBRARY_NAME', '__VK_LAYER_NV_optimus', 'DRI_PRIME')}
        environment.update(JARVIS_SYSFS_ROOT=str(root), XDG_CONFIG_HOME=str(Path(tmp) / 'config'),
                           QT_QPA_PLATFORM='offscreen', **(env or {}))
        out = subprocess.run([JARVIS, '--gpu-info', *args], env=environment, capture_output=True, text=True, timeout=60)
        assert out.returncode == 0, out.stderr
        if keep is not None:
            conf = Path(tmp) / 'config' / 'Jarvis' / 'jarvis.conf'
            keep.append(conf.read_text() if conf.exists() else '')
        return out.stdout


NVIDIA_HYBRID = [('0000:01:00.0', '10de', '25a2', 'nvidia', False), ('0000:05:00.0', '1002', '1681', 'amdgpu', True)]
AMD_HYBRID = [('0000:00:02.0', '8086', '46a6', 'i915', True), ('0000:03:00.0', '1002', '73ff', 'amdgpu', False)]
SINGLE = [('0000:05:00.0', '1002', '1681', 'amdgpu', True)]


def check(condition, message, text):
    if not condition:
        print('FAIL:', message)
        print(text)
        sys.exit(1)


r = report(NVIDIA_HYBRID, '--gpu', 'auto')
# Names come from the PCI ID database when it is installed ("NVIDIA GeForce RTX 3050
# Mobile"), else from the ids ("NVIDIA GPU 25a2").
check(re.search(r'card0 NVIDIA (GeForce RTX 3050 Mobile|GPU 25a2) — nvidia, 0000:01:00\.0\n', r), 'NVIDIA card listed', r)
check(re.search(r'card1 AMD (Radeon 680M[^—]*|GPU 1681) — amdgpu, 0000:05:00\.0, boot\n', r), 'AMD card listed as boot GPU', r)
check('card0-eDP-1' not in r, 'connectors are not cards', r)
check('Offload variables set by Jarvis: none' in r, 'auto leaves the environment alone', r)

r = report(NVIDIA_HYBRID, '--gpu', 'dedicated')
check('__NV_PRIME_RENDER_OFFLOAD=1' in r and '__GLX_VENDOR_LIBRARY_NAME=nvidia' in r
      and '__VK_LAYER_NV_optimus=NVIDIA_only' in r and 'DRI_PRIME' not in r, 'NVIDIA offload for the dedicated card', r)

r = report(NVIDIA_HYBRID, '--gpu', 'integrated', env={'__NV_PRIME_RENDER_OFFLOAD': '1'})
check('Offload variables set by Jarvis: none (cleared: __NV_PRIME_RENDER_OFFLOAD)' in r, 'integrated undoes an inherited offload', r)

r = report(AMD_HYBRID, '--gpu', 'dedicated')
check('DRI_PRIME=pci-0000_03_00_0' in r and '__NV_PRIME' not in r, 'Mesa offload names the exact device', r)

r = report(SINGLE, '--gpu', 'dedicated')
check('No second graphics card was found' in r and 'Offload variables set by Jarvis: none' in r, 'one card: nothing to offload', r)

# A previous Jarvis's own variables are not the user's: a restart drops them.
r = report(NVIDIA_HYBRID, '--gpu', 'auto', env={'DRI_PRIME': 'pci-0000_05_00_0', 'JARVIS_GRAPHICS_SET': 'DRI_PRIME'})
check('Offload variables set by Jarvis: none' in r, 'inherited Jarvis variables dropped', r)

# The saved choice is used, and a diagnostic run leaves no trial marker behind.
saved = []
r = report(NVIDIA_HYBRID, config='[graphics]\ngpu=dedicated\n', keep=saved)
check('Choice: card dedicated' in r and '__NV_PRIME_RENDER_OFFLOAD=1' in r, 'saved choice applied', r)
check('probe' not in saved[0], 'no trial marker from --gpu-info', saved[0])

# A choice whose Jarvis died before its first frame is undone, with a notice.
saved = []
r = report(NVIDIA_HYBRID, config='[graphics]\ngpu=dedicated\nprobe=dedicated/auto@999999999\n', keep=saved)
check('Choice: card auto' in r and 'Notice: Jarvis could not draw its window' in r, 'failed choice undone', r)
check('gpu=auto' in saved[0] and 'probe' not in saved[0], 'settings reset', saved[0])

# A Jarvis that is still running (a tray instance before its first frame) is no failure.
with tempfile.TemporaryDirectory() as tmp:
    fake = Path(tmp) / 'jarvis'
    fake.write_bytes(Path('/bin/sleep').read_bytes())
    fake.chmod(0o755)
    running = subprocess.Popen([str(fake), '30'])
    try:
        r = report(NVIDIA_HYBRID, config=f'[graphics]\ngpu=dedicated\nprobe=dedicated/auto@{running.pid}\n')
        check('Choice: card dedicated' in r and 'Notice' not in r, 'running Jarvis keeps its choice', r)
    finally:
        running.kill()

print('PASS: graphics card choice')
