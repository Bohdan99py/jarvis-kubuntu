#!/usr/bin/env python3
"""Speak through a dedicated Speech Dispatcher connection; stop only our own speech."""
import signal
import sys
import threading

def language(text):
    """The answer's own script decides the voice: Cyrillic -> Russian, otherwise English."""
    cyrillic = sum(1 for c in text if '\u0400' <= c <= '\u04ff')
    latin = sum(1 for c in text if c.isascii() and c.isalpha())
    return 'ru' if cyrillic >= latin else 'en'

def main():
    import speechd
    finished = threading.Event()
    stopped = threading.Event()
    signal.signal(signal.SIGTERM, lambda *_: (stopped.set(), finished.set()))
    signal.signal(signal.SIGINT, lambda *_: (stopped.set(), finished.set()))
    client = speechd.SSIPClient('Jarvis', component='responses')
    try:
        client.set_data_mode(speechd.DataMode.TEXT)
        text = sys.stdin.read(6000)
        if not text.strip():
            return 0
        client.set_language(language(text))
        client.speak(text, callback=lambda *_args, **_kwargs: finished.set(),
                     event_types=(speechd.CallbackType.END, speechd.CallbackType.CANCEL))
        if not finished.wait(300):
            return 1
        return 0
    finally:
        client.cancel()
        client.close()

if __name__ == '__main__':
    try:
        sys.exit(main())
    except Exception as error:
        print(str(error), file=sys.stderr)
        sys.exit(1)
