"""A stand-in for xdg-desktop-portal's GlobalShortcuts on the private test bus.

It owns org.freedesktop.portal.Desktop, answers CreateSession, ListShortcuts and
BindShortcuts through Request objects like the real portal, records what was
bound, and can press the shortcut (the Activated signal).
"""
import os
import threading

from gi.repository import Gio, GLib

XML = '''<node>
<interface name="org.freedesktop.portal.GlobalShortcuts">
<method name="CreateSession"><arg type="a{sv}" direction="in"/><arg type="o" direction="out"/></method>
<method name="ListShortcuts"><arg type="o" direction="in"/><arg type="a{sv}" direction="in"/><arg type="o" direction="out"/></method>
<method name="BindShortcuts"><arg type="o" direction="in"/><arg type="a(sa{sv})" direction="in"/><arg type="s" direction="in"/><arg type="a{sv}" direction="in"/><arg type="o" direction="out"/></method>
<signal name="Activated"><arg type="o"/><arg type="s"/><arg type="t"/><arg type="a{sv}"/></signal>
<property name="version" type="u" access="read"/>
</interface>
<interface name="org.freedesktop.host.portal.Registry">
<method name="Register"><arg type="s" direction="in"/><arg type="a{sv}" direction="in"/></method>
</interface>
</node>'''

_loop = None


def _ensure_loop():
    global _loop
    if _loop is None:
        _loop = GLib.MainLoop()
        threading.Thread(target=_loop.run, daemon=True).start()


class FakePortal:
    PATH = '/org/freedesktop/portal/desktop'

    def __init__(self):
        _ensure_loop()
        self.registered = []
        self.bound = {}
        self.session = ''
        flags = Gio.DBusConnectionFlags.AUTHENTICATION_CLIENT | Gio.DBusConnectionFlags.MESSAGE_BUS_CONNECTION
        self.conn = Gio.DBusConnection.new_for_address_sync(os.environ['DBUS_SESSION_BUS_ADDRESS'], flags, None, None)
        node = Gio.DBusNodeInfo.new_for_xml(XML)
        self.registrations = [self.conn.register_object(self.PATH, iface, self._call, self._property, None)
                              for iface in node.interfaces]
        self.conn.call_sync('org.freedesktop.DBus', '/org/freedesktop/DBus', 'org.freedesktop.DBus', 'RequestName',
                            GLib.Variant('(su)', ('org.freedesktop.portal.Desktop', 4)), None, Gio.DBusCallFlags.NONE, 3000, None)

    def _property(self, conn, sender, path, interface, name):
        return GLib.Variant('u', 2)

    def _request(self, sender, token, results):
        path = '/org/freedesktop/portal/desktop/request/%s/%s' % (sender[1:].replace('.', '_'), token)
        def respond():
            self.conn.emit_signal(sender, path, 'org.freedesktop.portal.Request', 'Response', GLib.Variant('(ua{sv})', (0, results)))
            return False
        GLib.timeout_add(50, respond)
        return path

    def _shortcuts(self):
        return GLib.Variant('a(sa{sv})', [(i, {'description': GLib.Variant('s', p['description']),
                                                'trigger_description': GLib.Variant('s', 'Ctrl+J')}) for i, p in self.bound.items()])

    def _call(self, conn, sender, path, interface, method, params, invocation):
        args = params.unpack()
        if method == 'Register':
            self.registered.append(args[0])
            invocation.return_value(None)
        elif method == 'CreateSession':
            self.session = '/org/freedesktop/portal/desktop/session/%s/%s' % (sender[1:].replace('.', '_'), args[0]['session_handle_token'])
            invocation.return_value(GLib.Variant('(o)', (self._request(sender, args[0]['handle_token'], {'session_handle': GLib.Variant('s', self.session)}),)))
        elif method == 'ListShortcuts':
            invocation.return_value(GLib.Variant('(o)', (self._request(sender, args[1]['handle_token'], {'shortcuts': self._shortcuts()}),)))
        elif method == 'BindShortcuts':
            for shortcut_id, props in args[1]:
                self.bound[shortcut_id] = props
            invocation.return_value(GLib.Variant('(o)', (self._request(sender, args[3]['handle_token'], {'shortcuts': self._shortcuts()}),)))

    def press(self, shortcut_id='overlay', token='test-token'):
        self.conn.emit_signal(None, self.PATH, 'org.freedesktop.portal.GlobalShortcuts', 'Activated',
                              GLib.Variant('(osta{sv})', (self.session, shortcut_id, 1, {'activation_token': GLib.Variant('s', token)})))

    def close(self):
        for r in self.registrations:
            self.conn.unregister_object(r)
        self.conn.close_sync(None)
