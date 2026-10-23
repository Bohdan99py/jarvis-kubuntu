"""A stand-in for the Jarvis window on the private test bus.

It owns org.jarvis.Desktop1 (what jarvisd asks to show a part of Jarvis) and
records Open() calls. Its connection can also call jarvisd, as one caller with
one unique name, to test per-caller limits.
"""
import os

from gi.repository import Gio, GLib

from fake_kwin import _ensure_loop

XML = '''<node><interface name="org.jarvis.Desktop1">
<method name="Open"><arg type="s" direction="in"/><arg type="s" direction="in"/></method>
<method name="ShowOverlay"><arg type="s" direction="in"/></method>
<method name="ShowMain"/>
<method name="ShowQuick"/>
</interface></node>'''


class FakeDesktop:
    def __init__(self, own_name=True):
        _ensure_loop()
        self.opened = []
        flags = Gio.DBusConnectionFlags.AUTHENTICATION_CLIENT | Gio.DBusConnectionFlags.MESSAGE_BUS_CONNECTION
        self.conn = Gio.DBusConnection.new_for_address_sync(os.environ['DBUS_SESSION_BUS_ADDRESS'], flags, None, None)
        info = Gio.DBusNodeInfo.new_for_xml(XML).interfaces[0]
        self.registration = self.conn.register_object('/org/jarvis/Desktop1', info, self._call, None, None)
        self.owns = own_name
        if own_name:
            self._bus('RequestName', GLib.Variant('(su)', ('org.jarvis.Desktop1', 4)))

    def _bus(self, method, args):
        return self.conn.call_sync('org.freedesktop.DBus', '/org/freedesktop/DBus', 'org.freedesktop.DBus',
                                   method, args, None, Gio.DBusCallFlags.NONE, 3000, None)

    def _call(self, conn, sender, path, interface, method, params, invocation):
        args = params.unpack()
        self.opened.append((args[0], args[1]) if method == 'Open' else (method, ''))
        invocation.return_value(None)

    def integration(self, method, signature='()', args=()):
        """Calls org.jarvis.Integration1 from this connection; raises GLib.Error on a D-Bus error."""
        reply = self.conn.call_sync('org.jarvis.Daemon1', '/org/jarvis/Integration1', 'org.jarvis.Integration1', method,
                                    GLib.Variant(signature, tuple(args)), None, Gio.DBusCallFlags.NONE, 3000, None)
        return reply.unpack()[0]

    def close(self):
        self.conn.unregister_object(self.registration)
        if self.owns:
            self._bus('ReleaseName', GLib.Variant('(s)', ('org.jarvis.Desktop1',)))
        self.conn.close_sync(None)
