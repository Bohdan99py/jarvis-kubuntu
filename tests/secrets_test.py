#!/usr/bin/env python3
"""The API keys and the system password store (0.21), on a fake Secret Service.

Runs `jarvis_core_tests secrets` on this private bus (start it with
dbus-run-session --config-file=tests/test-bus.conf), next to tests/fake_secrets.py:
the upgrade moves the keys out of config.json and speech.json, a locked store
gives no key until it is unlocked through its prompt, --prepare-downgrade puts
them back, and the fake store holds exactly what Jarvis wrote. Never touches the
user's real keyring: it refuses to run on a bus that already has one.
Usage: secrets_test.py <jarvis_core_tests>
"""
import os
import subprocess
import sys
import tempfile
from pathlib import Path

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
from fake_secrets import FakeSecrets  # noqa: E402


def main():
    tests = sys.argv[1]
    owner = subprocess.run(['gdbus', 'call', '--session', '--dest', 'org.freedesktop.DBus', '--object-path', '/org/freedesktop/DBus',
                            '--method', 'org.freedesktop.DBus.NameHasOwner', 'org.freedesktop.secrets'], capture_output=True, text=True)
    if 'true' in owner.stdout:
        print('refusing: this bus already has a password store (run under dbus-run-session --config-file=tests/test-bus.conf)')
        return 1
    store = FakeSecrets()
    try:
        env = dict(os.environ)
        env.pop('JARVIS_SECRET_SERVICE', None)
        env['ANTHROPIC_API_KEY'] = ''
        env.pop('ELEVENLABS_API_KEY', None)
        with tempfile.TemporaryDirectory() as tmp:
            result = subprocess.run([tests, 'secrets'], env=env, capture_output=True, text=True, timeout=120)
            print(result.stdout + result.stderr, end='')
            if result.returncode != 0:
                return 1
        # What the scenario left: the Claude key removed, the ElevenLabs key stored once.
        if store.secret('anthropic-api-key') is not None or store.secret('elevenlabs-api-key') != 'eleven0123456789abcdef':
            print('FAIL: unexpected items in the store:', {k: v['attributes'] for k, v in store.items.items()})
            return 1
        labels = sorted(it['label'] for it in store.items.values())
        if labels != ['Jarvis: ElevenLabs API key']:
            print('FAIL: item labels', labels)
            return 1
    finally:
        store.close()
    print('PASS: keys in the password store')
    return 0


if __name__ == '__main__':
    sys.exit(main())
