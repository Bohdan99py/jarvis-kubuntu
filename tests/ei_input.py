#!/usr/bin/env python3
"""Mouse and keyboard input for the isolated Plasma session (tests/plasma-session.sh).

KWin emulates input for a client that asks on D-Bus (org.kde.KWin.EIS.RemoteDesktop,
the same path the remote-desktop portal uses) and speaks libei on the returned
socket. Only inside the nested session: the script refuses to run unless
WAYLAND_DISPLAY is the test socket, so it can never click on the real desktop.

    ei_input.py click X Y [right|middle]   move to (X, Y) in logical pixels, click
    ei_input.py move X Y
    ei_input.py scroll X Y STEPS            wheel at (X, Y): STEPS > 0 down / right, < 0 up / left
    ei_input.py key NAME…                   esc, enter, down, up, tab, ctrl+j, super…
    ei_input.py type TEXT                   latin letters, digits, space, - . /
"""
import ctypes
import os
import select
import sys
import time

if os.environ.get("WAYLAND_DISPLAY") != "jtest":
    sys.exit("ei_input.py: only inside tests/plasma-session.sh (WAYLAND_DISPLAY=jtest)")

import dbus  # noqa: E402

EI_EVENT_CONNECT, EI_EVENT_DISCONNECT, EI_EVENT_SEAT_ADDED = 1, 2, 3
EI_EVENT_DEVICE_ADDED, EI_EVENT_DEVICE_REMOVED, EI_EVENT_DEVICE_PAUSED, EI_EVENT_DEVICE_RESUMED = 5, 6, 7, 8
CAP_POINTER, CAP_ABSOLUTE, CAP_KEYBOARD, CAP_SCROLL, CAP_BUTTON = 1, 2, 4, 16, 32
BTN = {"left": 0x110, "right": 0x111, "middle": 0x112}
# evdev key codes
KEYS = {"esc": 1, "enter": 28, "ctrl": 29, "shift": 42, "alt": 56, "space": 57, "tab": 15, "backspace": 14,
        "up": 103, "down": 108, "left": 105, "right": 106, "super": 125, "minus": 12, "dot": 52, "slash": 53,
        "home": 102, "end": 107, "pageup": 104, "pagedown": 109}
for i, ch in enumerate("1234567890"):
    KEYS[ch] = 2 + i
for row, start in (("qwertyuiop", 16), ("asdfghjkl", 30), ("zxcvbnm", 44)):
    for i, ch in enumerate(row):
        KEYS[ch] = start + i
CHARS = {" ": "space", "-": "minus", ".": "dot", "/": "slash"}

ei = ctypes.CDLL("libei.so.1")
P = ctypes.c_void_p
for name, res, args in (
    ("ei_new_sender", P, [P]), ("ei_setup_backend_fd", ctypes.c_int, [P, ctypes.c_int]),
    ("ei_configure_name", None, [P, ctypes.c_char_p]), ("ei_get_fd", ctypes.c_int, [P]),
    ("ei_dispatch", None, [P]), ("ei_get_event", P, [P]), ("ei_event_get_type", ctypes.c_int, [P]),
    ("ei_event_get_seat", P, [P]), ("ei_event_get_device", P, [P]), ("ei_event_unref", P, [P]),
    ("ei_device_ref", P, [P]), ("ei_device_has_capability", ctypes.c_bool, [P, ctypes.c_int]),
    ("ei_device_start_emulating", None, [P, ctypes.c_uint32]), ("ei_device_stop_emulating", None, [P]),
    ("ei_device_pointer_motion_absolute", None, [P, ctypes.c_double, ctypes.c_double]),
    ("ei_device_button_button", None, [P, ctypes.c_uint32, ctypes.c_bool]),
    ("ei_device_scroll_discrete", None, [P, ctypes.c_int32, ctypes.c_int32]),
    ("ei_device_keyboard_key", None, [P, ctypes.c_uint32, ctypes.c_bool]),
    ("ei_device_frame", None, [P, ctypes.c_uint64]), ("ei_now", ctypes.c_uint64, [P]),
):
    getattr(ei, name).restype = res
    getattr(ei, name).argtypes = args
ei.ei_seat_bind_capabilities.restype = None  # variadic, NULL-terminated


def connect():
    kwin = dbus.SessionBus().get_object("org.kde.KWin", "/org/kde/KWin/EIS/RemoteDesktop")
    fd, _cookie = kwin.connectToEIS(CAP_POINTER | CAP_ABSOLUTE | CAP_KEYBOARD | CAP_SCROLL | CAP_BUTTON,
                                    dbus_interface="org.kde.KWin.EIS.RemoteDesktop")
    ctx = ei.ei_new_sender(None)
    ei.ei_configure_name(ctx, b"jarvis-test-input")
    if ei.ei_setup_backend_fd(ctx, fd.take()) != 0:
        sys.exit("ei_input.py: libei setup failed")
    return ctx


def pump(ctx, devices, until, timeout=3.0):
    """Dispatch events until until(devices) is true."""
    end = time.monotonic() + timeout
    while not until(devices) and time.monotonic() < end:
        select.select([ei.ei_get_fd(ctx)], [], [], 0.1)
        ei.ei_dispatch(ctx)
        while True:
            ev = ei.ei_get_event(ctx)
            if not ev:
                break
            kind = ei.ei_event_get_type(ev)
            if kind == EI_EVENT_SEAT_ADDED:
                ei.ei_seat_bind_capabilities(P(ei.ei_event_get_seat(ev)), ctypes.c_int(CAP_POINTER), ctypes.c_int(CAP_ABSOLUTE),
                                             ctypes.c_int(CAP_KEYBOARD), ctypes.c_int(CAP_BUTTON), ctypes.c_int(CAP_SCROLL), P(None))
            elif kind == EI_EVENT_DEVICE_RESUMED:
                dev = ei.ei_event_get_device(ev)
                ei.ei_device_ref(dev)
                ei.ei_device_start_emulating(dev, 1)
                devices.append(dev)
            elif kind == EI_EVENT_DISCONNECT:
                sys.exit("ei_input.py: KWin closed the connection")
            ei.ei_event_unref(ev)
    return until(devices)


def device(devices, cap):
    return next((d for d in devices if ei.ei_device_has_capability(d, cap)), None)


def main():
    if len(sys.argv) < 2:
        sys.exit(__doc__)
    ctx = connect()
    devices = []
    want = CAP_KEYBOARD if sys.argv[1] in ("key", "type") else CAP_ABSOLUTE
    if not pump(ctx, devices, lambda ds: device(ds, want) is not None):
        sys.exit("ei_input.py: no emulated device from KWin")
    dev = device(devices, want)

    def frame(d):
        ei.ei_device_frame(d, ei.ei_now(ctx))
        ei.ei_dispatch(ctx)
        time.sleep(0.03)

    cmd = sys.argv[1]
    if cmd == "scroll":
        x, y, steps = float(sys.argv[2]), float(sys.argv[3]), int(sys.argv[4])
        ei.ei_device_pointer_motion_absolute(dev, x, y)
        frame(dev)
        wheel = device(devices, CAP_SCROLL) or dev
        for _ in range(abs(steps)):
            ei.ei_device_scroll_discrete(wheel, 0, 120 if steps > 0 else -120)
            frame(wheel)
    elif cmd in ("click", "move"):
        x, y = float(sys.argv[2]), float(sys.argv[3])
        ei.ei_device_pointer_motion_absolute(dev, x, y)
        frame(dev)
        if cmd == "click":
            button = BTN[sys.argv[4] if len(sys.argv) > 4 else "left"]
            buttons = device(devices, CAP_BUTTON) or dev
            for press in (True, False):
                ei.ei_device_button_button(buttons, button, press)
                frame(buttons)
    elif cmd == "key":
        for combo in sys.argv[2:]:
            codes = [KEYS[k] for k in combo.lower().split("+")]
            for c in codes:
                ei.ei_device_keyboard_key(dev, c, True)
                frame(dev)
            for c in reversed(codes):
                ei.ei_device_keyboard_key(dev, c, False)
                frame(dev)
    elif cmd == "type":
        for ch in " ".join(sys.argv[2:]):
            name = CHARS.get(ch, ch.lower())
            codes = ([KEYS["shift"]] if ch.isupper() else []) + [KEYS[name]]
            for c in codes:
                ei.ei_device_keyboard_key(dev, c, True)
                frame(dev)
            for c in reversed(codes):
                ei.ei_device_keyboard_key(dev, c, False)
                frame(dev)
    else:
        sys.exit(__doc__)
    for d in devices:
        ei.ei_device_stop_emulating(d)
    ei.ei_dispatch(ctx)
    time.sleep(0.1)


main()
