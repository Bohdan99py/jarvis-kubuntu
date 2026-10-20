#!/usr/bin/env python3
"""Speak a text aloud; stop only our own speech.

Usage: speak.py [ru|en|fr] < text — without a language the text's script decides.

With the natural voice installed (Piper, see voice.py tts) the text is spoken by a
neural voice on this computer, sentence by sentence as it is synthesised;
otherwise through Speech Dispatcher. Markdown, code and links are not read out."""
import json
import os
import re
import signal
import subprocess
import sys
import threading
from pathlib import Path

DEFAULT_VOICES = {"ru": "ru_RU-dmitri-medium", "en": "en_GB-alan-medium", "fr": "fr_FR-tom-medium"}


def voice_root():
    base = os.environ.get("XDG_DATA_HOME") or os.path.expanduser("~/.local/share")
    return Path(base) / "jarvis" / "voice"


def language(text):
    """The answer's own script decides the voice: Cyrillic -> Russian, otherwise English."""
    cyrillic = sum(1 for c in text if 'Ѐ' <= c <= 'ӿ')
    latin = sum(1 for c in text if c.isascii() and c.isalpha())
    return 'ru' if cyrillic >= latin else 'en'


def speakable(text, lang):
    """What a person would read out: no code, markup, tables or URLs."""
    code = {"ru": "Код показан в чате.", "fr": "Le code est affiché dans le chat."}.get(lang, "The code is shown in the chat.")
    text = re.sub(r"```.*?(```|$)", f" {code} ", text, flags=re.S)
    text = re.sub(r"`([^`]*)`", r"\1", text)
    text = re.sub(r"!\[([^\]]*)\]\([^)]*\)", r"\1", text)
    text = re.sub(r"\[([^\]]+)\]\([^)]*\)", r"\1", text)
    text = re.sub(r"https?://\S+", "", text)
    text = re.sub(r"^\s*\|?\s*:?-{2,}.*$", "", text, flags=re.M)        # table rules
    text = re.sub(r"^\s*\|(.*?)\|?\s*$", r"\1.", text, flags=re.M)     # a table row reads as a sentence
    text = re.sub(r"\s*\|\s*", ", ", text)                              # table cells
    text = re.sub(r"^\s{0,3}#{1,6}\s*", "", text, flags=re.M)           # headings
    text = re.sub(r"^\s*(?:[-*+•]|\d+[.)])\s+", "", text, flags=re.M)   # list markers
    text = re.sub(r"(\*\*|__|\*|_|~~)(\S.*?\S|\S)\1", r"\2", text)
    text = re.sub(r"[⏰📝📎✓→←]", "", text)
    text = re.sub(r"\n\s*\n+", ".\n", text)
    text = re.sub(r"([.!?:])\.(\s)", r"\1\2", text)
    text = re.sub(r"[ \t]+", " ", text)
    return text.strip()


def piper_settings(root):
    try:
        return json.loads((root / "piper" / "settings.json").read_text(encoding="utf-8"))
    except (OSError, ValueError):
        return {}


def speak_piper(text, lang, root):
    """Runs inside the voice environment (where piper is installed)."""
    from piper import PiperVoice, SynthesisConfig
    settings = piper_settings(root)
    name = settings.get(lang) or DEFAULT_VOICES.get(lang, "")
    model = root / "piper" / f"{name}.onnx"
    if not model.exists():
        return False
    rate = float(settings.get("rate", 1.0))
    voice = PiperVoice.load(str(model))
    config = SynthesisConfig(length_scale=1.0 / max(0.5, min(2.0, rate)))
    player = None
    stopped = threading.Event()

    def stop(*_):
        stopped.set()
        if player and player.poll() is None:
            player.kill()
        sys.exit(0)

    signal.signal(signal.SIGTERM, stop)
    signal.signal(signal.SIGINT, stop)
    try:
        # One player for the whole answer: sentences play while the next ones are synthesised.
        for chunk in voice.synthesize(text, config):
            if stopped.is_set():
                break
            if player is None:
                player = subprocess.Popen(["aplay", "-q", "-t", "raw", "-f", "S16_LE", "-r", str(chunk.sample_rate),
                                           "-c", str(chunk.sample_channels), "-"], stdin=subprocess.PIPE)
            player.stdin.write(chunk.audio_int16_bytes)
            player.stdin.flush()
    except BrokenPipeError:
        return True
    finally:
        if player:
            try:
                player.stdin.close()
            except BrokenPipeError:
                pass
            player.wait(timeout=600)
    return True


def speak_dispatcher(text, lang):
    import speechd
    finished = threading.Event()
    signal.signal(signal.SIGTERM, lambda *_: finished.set())
    signal.signal(signal.SIGINT, lambda *_: finished.set())
    client = speechd.SSIPClient('Jarvis', component='responses')
    try:
        client.set_data_mode(speechd.DataMode.TEXT)
        client.set_language(lang)
        client.speak(text, callback=lambda *_args, **_kwargs: finished.set(),
                     event_types=(speechd.CallbackType.END, speechd.CallbackType.CANCEL))
        return 0 if finished.wait(300) else 1
    finally:
        client.cancel()
        client.close()


def main():
    args = [a for a in sys.argv[1:] if not a.startswith("--")]
    forced = args[0] if args and args[0] in ('ru', 'en', 'fr') else None
    text = sys.stdin.read(6000)
    if not text.strip():
        return 0
    lang = forced or language(text)
    spoken = speakable(text, lang)
    if not spoken:
        return 0
    root = voice_root()
    python = root / "venv" / "bin" / "python"
    if "--piper" in sys.argv:
        return 0 if speak_piper(spoken, lang, root) else 3
    if "--no-piper" not in sys.argv and (root / "piper" / "ready").exists() and python.exists():
        # The neural voice lives in the voice environment; hand the text over to it.
        child = subprocess.Popen([str(python), os.path.abspath(__file__), lang, "--piper"], stdin=subprocess.PIPE)

        def forward(*_):
            child.terminate()  # "stop" must silence the voice, not only this process
            raise SystemExit(0)

        signal.signal(signal.SIGTERM, forward)
        signal.signal(signal.SIGINT, forward)
        child.communicate(spoken.encode())
        if child.returncode != 3:
            return child.returncode
    return speak_dispatcher(spoken, lang)


if __name__ == '__main__':
    def terminate(signum, frame):
        raise SystemExit(0)
    signal.signal(signal.SIGTERM, terminate)
    try:
        sys.exit(main())
    except SystemExit:
        raise
    except Exception as error:
        print(str(error), file=sys.stderr)
        sys.exit(1)
