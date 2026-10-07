"""A stand-in for KWin's scripting D-Bus API on the private test bus.

It owns org.kde.KWin, accepts loadScript/start/isScriptLoaded/unloadScript and
can report windows to jarvisd from its own connection, like the real script.
"""
import os
import threading

from gi.repository import Gio, GLib

XML = '''<node><interface name="org.kde.kwin.Scripting">
<method name="loadScript"><arg type="s" direction="in"/><arg type="s" direction="in"/><arg type="i" direction="out"/></method>
<method name="start"/>
<method name="isScriptLoaded"><arg type="s" direction="in"/><arg type="b" direction="out"/></method>
<method name="unloadScript"><arg type="s" direction="in"/><arg type="b" direction="out"/></method>
</interface></node>'''

_loop = None


def _ensure_loop():
    global _loop
    if _loop is None:
        _loop = GLib.MainLoop()
        threading.Thread(target=_loop.run, daemon=True).start()


class FakeKWin:
    def __init__(self):
        _ensure_loop()
        self.scripts = {}
        self.starts = 0
        flags = Gio.DBusConnectionFlags.AUTHENTICATION_CLIENT | Gio.DBusConnectionFlags.MESSAGE_BUS_CONNECTION
        self.conn = Gio.DBusConnection.new_for_address_sync(os.environ['DBUS_SESSION_BUS_ADDRESS'], flags, None, None)
        info = Gio.DBusNodeInfo.new_for_xml(XML).interfaces[0]
        self.registration = self.conn.register_object('/Scripting', info, self._call, None, None)
        self._bus('RequestName', GLib.Variant('(su)', ('org.kde.KWin', 4)))

    def _bus(self, method, args):
        return self.conn.call_sync('org.freedesktop.DBus', '/org/freedesktop/DBus', 'org.freedesktop.DBus',
                                   method, args, None, Gio.DBusCallFlags.NONE, 3000, None)

    def _call(self, conn, sender, path, interface, method, params, invocation):
        args = params.unpack()
        if method == 'loadScript':
            with open(args[0], encoding='utf-8') as f:
                self.scripts[args[1]] = f.read()
            invocation.return_value(GLib.Variant('(i)', (len(self.scripts),)))
        elif method == 'start':
            self.starts += 1
            invocation.return_value(None)
        elif method == 'isScriptLoaded':
            invocation.return_value(GLib.Variant('(b)', (args[0] in self.scripts,)))
        elif method == 'unloadScript':
            invocation.return_value(GLib.Variant('(b)', (self.scripts.pop(args[0], None) is not None,)))

    def report(self, caption, app_class, desktop):
        """What the KWin script does on focus change."""
        self.conn.call_sync('org.jarvis.Daemon1', '/org/jarvis/Daemon1', 'org.jarvis.Daemon1', 'WindowActivated',
                            GLib.Variant('(sss)', (caption, app_class, desktop)), None, Gio.DBusCallFlags.NONE, 3000, None)

    def close(self):
        self.conn.unregister_object(self.registration)
        self._bus('ReleaseName', GLib.Variant('(s)', ('org.kde.KWin',)))
        self.conn.close_sync(None)
