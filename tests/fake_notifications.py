"""A stand-in for the desktop notification server on the private test bus.

It owns org.freedesktop.Notifications, records every Notify call and can
press an action button the way Plasma does (the ActionInvoked signal).
"""
import os

from gi.repository import Gio, GLib

from fake_kwin import _ensure_loop

XML = '''<node><interface name="org.freedesktop.Notifications">
<method name="Notify">
<arg type="s" direction="in"/><arg type="u" direction="in"/><arg type="s" direction="in"/><arg type="s" direction="in"/>
<arg type="s" direction="in"/><arg type="as" direction="in"/><arg type="a{sv}" direction="in"/><arg type="i" direction="in"/>
<arg type="u" direction="out"/></method>
<method name="CloseNotification"><arg type="u" direction="in"/></method>
<method name="GetCapabilities"><arg type="as" direction="out"/></method>
<signal name="ActionInvoked"><arg type="u"/><arg type="s"/></signal>
<signal name="NotificationClosed"><arg type="u"/><arg type="u"/></signal>
</interface></node>'''


class FakeNotifications:
    def __init__(self):
        _ensure_loop()
        self.sent = []  # dicts: id, app, title, body, actions, timeout
        flags = Gio.DBusConnectionFlags.AUTHENTICATION_CLIENT | Gio.DBusConnectionFlags.MESSAGE_BUS_CONNECTION
        self.conn = Gio.DBusConnection.new_for_address_sync(os.environ['DBUS_SESSION_BUS_ADDRESS'], flags, None, None)
        info = Gio.DBusNodeInfo.new_for_xml(XML).interfaces[0]
        self.conn.register_object('/org/freedesktop/Notifications', info, self._call, None, None)
        self.conn.call_sync('org.freedesktop.DBus', '/org/freedesktop/DBus', 'org.freedesktop.DBus', 'RequestName',
                            GLib.Variant('(su)', ('org.freedesktop.Notifications', 4)), None,
                            Gio.DBusCallFlags.NONE, 3000, None)

    def _call(self, conn, sender, path, interface, method, params, invocation):
        args = params.unpack()
        if method == 'Notify':
            app, replaces, icon, title, body, actions, hints, timeout = args
            nid = replaces or len(self.sent) + 1
            self.sent.append(dict(id=nid, app=app, title=title, body=body, timeout=timeout,
                                  actions=dict(zip(actions[0::2], actions[1::2])), hints=hints))
            invocation.return_value(GLib.Variant('(u)', (nid,)))
        elif method == 'GetCapabilities':
            invocation.return_value(GLib.Variant('(as)', (['actions', 'body'],)))
        else:
            invocation.return_value(None)

    def find(self, text):
        return [n for n in self.sent if text in n['body'] or text in n['title']]

    def invoke(self, nid, action):
        """The user pressed a button (or clicked the notification for "default")."""
        self.conn.emit_signal(None, '/org/freedesktop/Notifications', 'org.freedesktop.Notifications',
                              'ActionInvoked', GLib.Variant('(us)', (nid, action)))
