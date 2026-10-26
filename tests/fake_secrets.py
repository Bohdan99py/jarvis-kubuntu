"""A stand-in for the freedesktop Secret Service (org.freedesktop.secrets) on the private test bus.

It keeps items in memory, answers OpenSession ("plain"), SearchItems, ReadAlias,
Unlock (through a Prompt whose Completed signal says "not dismissed"),
CreateItem (replacing an item with the same attributes), GetSecret and Delete,
like ksecretd or GNOME Keyring would. A test hook, org.jarvis.test.FakeSecrets.Lock,
locks every item, so the "locked" path can be checked too.
"""
import os
import threading

from gi.repository import Gio, GLib

ROOT = '/org/freedesktop/secrets'
COLLECTION = ROOT + '/collection/login'

XML = '''<node>
<interface name="org.freedesktop.Secret.Service">
<method name="OpenSession"><arg type="s" direction="in"/><arg type="v" direction="in"/><arg type="v" direction="out"/><arg type="o" direction="out"/></method>
<method name="SearchItems"><arg type="a{ss}" direction="in"/><arg type="ao" direction="out"/><arg type="ao" direction="out"/></method>
<method name="Unlock"><arg type="ao" direction="in"/><arg type="ao" direction="out"/><arg type="o" direction="out"/></method>
<method name="ReadAlias"><arg type="s" direction="in"/><arg type="o" direction="out"/></method>
</interface>
<interface name="org.jarvis.test.FakeSecrets">
<method name="Lock"/>
</interface>
<interface name="org.freedesktop.Secret.Collection">
<method name="CreateItem"><arg type="a{sv}" direction="in"/><arg type="(oayays)" direction="in"/><arg type="b" direction="in"/><arg type="o" direction="out"/><arg type="o" direction="out"/></method>
</interface>
<interface name="org.freedesktop.Secret.Item">
<method name="GetSecret"><arg type="o" direction="in"/><arg type="(oayays)" direction="out"/></method>
<method name="Delete"><arg type="o" direction="out"/></method>
</interface>
<interface name="org.freedesktop.Secret.Session">
<method name="Close"/>
</interface>
<interface name="org.freedesktop.Secret.Prompt">
<method name="Prompt"><arg type="s" direction="in"/></method>
<signal name="Completed"><arg type="b"/><arg type="v"/></signal>
</interface>
</node>'''

_loop = None


def _ensure_loop():
    global _loop
    if _loop is None:
        _loop = GLib.MainLoop()
        threading.Thread(target=_loop.run, daemon=True).start()


class FakeSecrets:
    def __init__(self):
        _ensure_loop()
        flags = Gio.DBusConnectionFlags.AUTHENTICATION_CLIENT | Gio.DBusConnectionFlags.MESSAGE_BUS_CONNECTION
        self.conn = Gio.DBusConnection.new_for_address_sync(os.environ['DBUS_SESSION_BUS_ADDRESS'], flags, None, None)
        self.node = Gio.DBusNodeInfo.new_for_xml(XML)
        self.items = {}      # path -> {'attributes', 'label', 'secret', 'locked'}
        self.prompts = {}    # path -> paths to unlock
        self.next = 1
        self.registrations = []
        self._register(ROOT, 'org.freedesktop.Secret.Service')
        self._register(ROOT, 'org.jarvis.test.FakeSecrets')
        self._register(COLLECTION, 'org.freedesktop.Secret.Collection')
        self.conn.call_sync('org.freedesktop.DBus', '/org/freedesktop/DBus', 'org.freedesktop.DBus', 'RequestName',
                            GLib.Variant('(su)', ('org.freedesktop.secrets', 4)), None, Gio.DBusCallFlags.NONE, 3000, None)

    def _register(self, path, interface):
        self.registrations.append(self.conn.register_object(path, self.node.lookup_interface(interface), self._call, None, None))

    def _new_path(self, kind):
        path = '%s/%s/%d' % (ROOT, kind, self.next)
        self.next += 1
        return path

    def _call(self, conn, sender, path, interface, method, params, invocation):
        args = params.unpack()
        if method == 'OpenSession':
            if args[0] != 'plain':
                invocation.return_dbus_error('org.freedesktop.DBus.Error.NotSupported', 'only plain')
                return
            session = self._new_path('session')
            self._register(session, 'org.freedesktop.Secret.Session')
            invocation.return_value(GLib.Variant('(vo)', (GLib.Variant('s', ''), session)))
        elif method == 'Close':
            invocation.return_value(None)
        elif method == 'ReadAlias':
            invocation.return_value(GLib.Variant('(o)', (COLLECTION if args[0] == 'default' else '/',)))
        elif method == 'SearchItems':
            wanted = args[0]
            found = [p for p, it in self.items.items() if all(it['attributes'].get(k) == v for k, v in wanted.items())]
            invocation.return_value(GLib.Variant('(aoao)', ([p for p in found if not self.items[p]['locked']],
                                                            [p for p in found if self.items[p]['locked']])))
        elif method == 'Unlock':
            locked = [p for p in args[0] if p in self.items and self.items[p]['locked']]
            if not locked:
                invocation.return_value(GLib.Variant('(aoo)', (args[0], '/')))
                return
            prompt = self._new_path('prompt')
            self.prompts[prompt] = locked
            self._register(prompt, 'org.freedesktop.Secret.Prompt')
            invocation.return_value(GLib.Variant('(aoo)', ([], prompt)))
        elif method == 'Prompt':
            # Like a wallet: unlocking one item opens the whole collection.
            unlocked = self.prompts.pop(path, [])
            for it in self.items.values():
                it['locked'] = False
            invocation.return_value(None)
            def complete():
                self.conn.emit_signal(None, path, 'org.freedesktop.Secret.Prompt', 'Completed',
                                      GLib.Variant('(bv)', (False, GLib.Variant('ao', unlocked))))
                return False
            GLib.timeout_add(30, complete)
        elif method == 'CreateItem':
            props, secret, replace = args
            attributes = dict(props.get('org.freedesktop.Secret.Item.Attributes', {}))
            existing = [p for p, it in self.items.items() if it['attributes'] == attributes]
            if existing and replace:
                item = existing[0]
            else:
                item = self._new_path('collection/login')
                self._register(item, 'org.freedesktop.Secret.Item')
            self.items[item] = {'attributes': attributes, 'label': props.get('org.freedesktop.Secret.Item.Label', ''),
                                'secret': bytes(secret[2]).decode(), 'locked': False}
            invocation.return_value(GLib.Variant('(oo)', (item, '/')))
        elif method == 'GetSecret':
            it = self.items.get(path)
            if not it or it['locked']:
                invocation.return_dbus_error('org.freedesktop.Secret.Error.IsLocked', 'locked')
                return
            invocation.return_value(GLib.Variant('((oayays))', ((args[0], b'', it['secret'].encode(), 'text/plain'),)))
        elif method == 'Delete':
            self.items.pop(path, None)
            invocation.return_value(GLib.Variant('(o)', ('/',)))
        elif method == 'Lock':
            for it in self.items.values():
                it['locked'] = True
            invocation.return_value(None)

    def secret(self, account):
        for it in self.items.values():
            if it['attributes'].get('account') == account and it['attributes'].get('application') == 'org.jarvis.Jarvis':
                return it['secret']
        return None

    def close(self):
        for r in self.registrations:
            self.conn.unregister_object(r)
        self.conn.close_sync(None)
