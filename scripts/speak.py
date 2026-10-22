#!/usr/bin/env python3
"""Speak a text aloud with Jarvis's voice; stop only our own speech.

Usage:
  speak.py [ru|en|fr] < text   speak one answer (without a language the text's script decides)
  speak.py --serve             the speech server: keeps voice models loaded between answers
  speak.py --status            JSON: which voice engines are ready and which one speaks

Engines ("engine" in voice/speech.json):
  elevenlabs  ElevenLabs cloud voices — the most natural; the text goes to ElevenLabs;
  chatterbox  Chatterbox (Resemble AI), local neural speech, can copy a voice from a sample;
  piper       Piper, local neural speech, light and fast;
  system      Speech Dispatcher (espeak-ng);
  auto        the best one that is ready, in that order.
A failing engine hands the text to the next ready one, so Jarvis is never silent.

The one-shot form hands the text to a running speech server when there is one
(a socket in $XDG_RUNTIME_DIR, owner only), otherwise speaks by itself. It prints
JSON events: {"event": "engine", "name": …} and {"event": "error", "text": …}.
Markdown, code and links are not read out."""
import json
import math
import os
import queue
import re
import shutil
import signal
import socket
import struct
import subprocess
import sys
import threading
import time
from pathlib import Path

DEFAULT_VOICES = {"ru": "ru_RU-dmitri-medium", "en": "en_GB-alan-medium", "fr": "fr_FR-tom-medium"}
ENGINES = ("elevenlabs", "chatterbox", "piper", "system")

ELEVEN_API = "https://api.elevenlabs.io"
# "George": a warm British voice from the ElevenLabs library, available to every account.
ELEVEN_DEFAULT_VOICE = "JBFqnCBsd6RMkjVDRZzb"
ELEVEN_MODELS = ("eleven_multilingual_v2", "eleven_flash_v2_5", "eleven_turbo_v2_5", "eleven_v3")
# Only these models take a language code; the others recognise the language themselves.
ELEVEN_LANGUAGE_MODELS = ("eleven_flash_v2_5", "eleven_turbo_v2_5")
ELEVEN_RATE = 24000
MAX_TEXT = 6000


class SpeechError(Exception):
    """An engine could not speak; the message is for the user."""


def voice_root():
    base = os.environ.get("XDG_DATA_HOME") or os.path.expanduser("~/.local/share")
    return Path(base) / "jarvis" / "voice"


def socket_path():
    runtime = os.environ.get("XDG_RUNTIME_DIR")
    return Path(runtime) / "jarvis-speech.sock" if runtime else voice_root() / "speech.sock"


_out_lock = threading.Lock()


def send(event):
    """One JSON line on stdout; the level clock writes from its own thread."""
    line = json.dumps(event, ensure_ascii=False) + "\n"
    with _out_lock:
        sys.stdout.write(line)
        sys.stdout.flush()


def emit(kind, **data):
    send(dict(event=kind, **data))


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


def sentences(text, limit=280):
    """Sentence-sized pieces for engines that synthesise a whole piece at once."""
    parts, current = [], ""
    for sentence in re.split(r"(?<=[.!?…;:])\s+|\n+", text):
        sentence = sentence.strip()
        if not sentence:
            continue
        while len(sentence) > limit:  # a run-on sentence breaks at a comma or a space
            cut = max(sentence.rfind(", ", 0, limit), sentence.rfind(" ", 0, limit))
            cut = cut if cut > limit // 3 else limit
            parts.append(sentence[:cut + 1].strip())
            sentence = sentence[cut + 1:].strip()
        if current and len(current) + len(sentence) + 1 > limit:
            parts.append(current)
            current = sentence
        else:
            current = f"{current} {sentence}".strip()
    if current:
        parts.append(current)
    return parts


# ---- Settings ---------------------------------------------------------------------------

def read_json(path):
    try:
        value = json.loads(path.read_text(encoding="utf-8"))
        return value if isinstance(value, dict) else {}
    except (OSError, ValueError):
        return {}


def load_settings(root):
    """voice/speech.json (engine, ElevenLabs, Chatterbox) with piper/settings.json (voices, pace)."""
    settings = read_json(root / "speech.json")
    piper = read_json(root / "piper" / "settings.json")
    settings["voices"] = {lang: piper.get(lang) or DEFAULT_VOICES[lang] for lang in DEFAULT_VOICES}
    try:
        settings["rate"] = max(0.5, min(2.0, float(piper.get("rate", 1.0))))
    except (TypeError, ValueError):
        settings["rate"] = 1.0
    if settings.get("engine") not in ENGINES:
        settings["engine"] = "auto"
    for section in ("elevenlabs", "chatterbox"):
        if not isinstance(settings.get(section), dict):
            settings[section] = {}
    return settings


def eleven_key(settings):
    return (os.environ.get("ELEVENLABS_API_KEY") or settings["elevenlabs"].get("api_key") or "").strip()


def venv_python(root):
    return root / "venv" / "bin" / "python"


def chatterbox_python(root):
    return root / "chatterbox" / "venv" / "bin" / "python"


def ready(engine, settings, root):
    if engine == "elevenlabs":
        return bool(eleven_key(settings))
    if engine == "chatterbox":
        return (root / "chatterbox" / "ready").exists() and chatterbox_python(root).exists()
    if engine == "piper":
        return (root / "piper" / "ready").exists() and venv_python(root).exists()
    return True


def plan(settings, root):
    """Engines to try, in order: the chosen one, then the local fallbacks."""
    chosen = settings["engine"]
    order = list(ENGINES) if chosen == "auto" else [chosen] + [e for e in ("piper", "system") if e != chosen]
    return [e for e in order if ready(e, settings, root)]


def can_run_here(engine, root):
    """Piper and Chatterbox live in their own Python environments."""
    if engine == "piper":
        return Path(sys.prefix).resolve() == (root / "venv").resolve()
    if engine == "chatterbox":
        return Path(sys.prefix).resolve() == (root / "chatterbox" / "venv").resolve()
    return True


# ---- Playback ---------------------------------------------------------------------------

class Job:
    """One answer being spoken; cancel() silences it from any thread.
    `report` receives events for the client: engine, error and the sound level."""

    def __init__(self, report=None):
        self.cancelled = threading.Event()
        self.report = report
        self._lock = threading.Lock()
        self._closers = []

    def on_cancel(self, closer):
        with self._lock:
            self._closers.append(closer)
        if self.cancelled.is_set():
            closer()

    def cancel(self):
        self.cancelled.set()
        with self._lock:
            closers, self._closers = self._closers, []
        for closer in closers:
            try:
                closer()
            except Exception:
                pass


def levels(pcm, rate, channels=1, frame_ms=33):
    """Loudness of what is played, per ~1/30 s: [overall, low, mid, high], 0…1.
    Bands from two one-pole filters (no NumPy in the system Python): low below
    ~300 Hz, high above ~2.5 kHz, mid in between — enough to animate a voice."""
    import array
    samples = array.array("h")
    samples.frombytes(pcm[:len(pcm) - len(pcm) % 2])
    if channels > 1:
        samples = samples[::channels]
    step = max(1, int(rate * frame_ms / 1000))
    a_low = 1 - math.exp(-2 * math.pi * 300 / rate)
    a_high = 1 - math.exp(-2 * math.pi * 2500 / rate)
    low = high_lp = 0.0
    out = []
    for start in range(0, len(samples), step):
        s_all = s_low = s_mid = s_high = 0.0
        frame = samples[start:start + step]
        for x in frame:
            x /= 32768.0
            low += a_low * (x - low)
            high_lp += a_high * (x - high_lp)
            h = x - high_lp
            m = high_lp - low
            s_all += x * x
            s_low += low * low
            s_mid += m * m
            s_high += h * h
        n = len(frame) or 1
        # Speech RMS sits around 0.05–0.3: scaled so a normal voice reaches ~0.6–0.9.
        out.append([round(min(1.0, math.sqrt(v / n) * 3.2), 3) for v in (s_all, s_low, s_mid, s_high)])
    return out, step / rate


class LevelClock:
    """Sends the levels of the audio when it is heard, not when it is written:
    aplay plays at real time from the first write, so frame k sounds at start + k·dt."""

    def __init__(self, job):
        self.job = job
        self.frames = queue.Queue()
        self.start = None
        self.queued = 0.0
        self.alive = True
        threading.Thread(target=self._run, daemon=True).start()

    def feed(self, pcm, rate, channels):
        if self.start is None:
            self.start = time.monotonic() + 0.06  # the device's own buffering
        values, dt = levels(pcm, rate, channels)
        for v in values:
            self.frames.put((self.start + self.queued, v))
            self.queued += dt

    def _run(self):
        while self.alive:
            try:
                due, v = self.frames.get(timeout=0.2)
            except queue.Empty:
                continue
            wait = due - time.monotonic()
            if wait > 0 and self.job.cancelled.wait(wait):
                break
            if wait < -0.25:
                continue  # late (the machine was busy): skip rather than lag behind
            if self.job.report:
                self.job.report({"event": "level", "v": v})

    def stop(self):
        self.alive = False
        if self.job.report:
            self.job.report({"event": "level", "v": [0, 0, 0, 0]})


def play(chunks, job, rate, channels=1):
    """Plays raw 16-bit PCM from an iterator through one aplay, synthesising the
    next piece while the current one sounds. Returns False when cancelled."""
    pieces = queue.Queue(maxsize=3)
    failure = []

    def put(item):  # never blocks for good: a cancelled consumer stops taking
        while not job.cancelled.is_set():
            try:
                pieces.put(item, timeout=0.2)
                return True
            except queue.Full:
                continue
        return False

    def produce():
        try:
            for chunk in chunks:
                if not put(chunk):
                    break
        except Exception as error:  # reported by the consumer
            failure.append(error)
        finally:
            put(None)

    threading.Thread(target=produce, daemon=True).start()
    player = None
    clock = LevelClock(job) if job.report else None
    try:
        while True:
            try:
                chunk = pieces.get(timeout=0.2)
            except queue.Empty:
                if job.cancelled.is_set():
                    break
                continue
            if chunk is None or job.cancelled.is_set():
                break
            if player is None:
                player = subprocess.Popen(["aplay", "-q", "-t", "raw", "-f", "S16_LE", "-r", str(rate), "-c", str(channels), "-"],
                                          stdin=subprocess.PIPE, stderr=subprocess.DEVNULL)
                job.on_cancel(player.kill)
            player.stdin.write(chunk)
            player.stdin.flush()
            if clock:
                clock.feed(chunk, rate, channels)
    except (BrokenPipeError, ValueError):
        pass  # aplay was stopped
    finally:
        if player:
            try:
                player.stdin.close()
            except (BrokenPipeError, ValueError):
                pass
            try:
                player.wait(timeout=600)
            except subprocess.TimeoutExpired:
                player.kill()
        if clock:
            clock.stop()
    if failure and not job.cancelled.is_set():
        raise failure[0]
    return not job.cancelled.is_set()


# ---- Engines ----------------------------------------------------------------------------

def speak_elevenlabs(text, lang, settings, root, job):
    """Streams PCM from the ElevenLabs API: the first words sound while the rest arrives."""
    import urllib.error
    import urllib.request
    cfg = settings["elevenlabs"]
    key = eleven_key(settings)
    if not key:
        raise SpeechError("ElevenLabs: no API key")
    voice = str(cfg.get("voice_id") or ELEVEN_DEFAULT_VOICE)
    if not re.fullmatch(r"[A-Za-z0-9]{8,40}", voice):
        voice = ELEVEN_DEFAULT_VOICE
    model = cfg.get("model") if cfg.get("model") in ELEVEN_MODELS else ELEVEN_MODELS[0]

    def number(name, default, low=0.0, high=1.0):
        try:
            return max(low, min(high, float(cfg.get(name, default))))
        except (TypeError, ValueError):
            return default

    body = {"text": text, "model_id": model,
            "voice_settings": {"stability": number("stability", 0.5), "similarity_boost": number("similarity", 0.75),
                               "style": number("style", 0.0), "use_speaker_boost": True,
                               "speed": max(0.7, min(1.2, settings["rate"]))}}
    if model in ELEVEN_LANGUAGE_MODELS:
        body["language_code"] = lang
    request = urllib.request.Request(f"{ELEVEN_API}/v1/text-to-speech/{voice}/stream?output_format=pcm_{ELEVEN_RATE}",
                                     data=json.dumps(body).encode(), method="POST",
                                     headers={"xi-api-key": key, "Content-Type": "application/json",
                                              "Accept": "audio/pcm", "User-Agent": "Jarvis/0.16"})
    try:
        response = urllib.request.urlopen(request, timeout=30)
    except urllib.error.HTTPError as error:
        detail = ""
        try:
            detail = json.loads(error.read(65536)).get("detail", "")
            detail = detail.get("message", "") if isinstance(detail, dict) else str(detail)
        except Exception:
            pass
        reasons = {401: "the API key was rejected", 402: "the plan's character quota is used up",
                   429: "too many requests or the quota is used up"}
        raise SpeechError(f"ElevenLabs ({error.code}): {reasons.get(error.code, detail or error.reason)}"[:300]) from None
    except OSError as error:
        raise SpeechError(f"ElevenLabs is unreachable: {error}"[:300]) from None
    job.on_cancel(response.close)

    def stream():
        odd = b""
        with response:
            while not job.cancelled.is_set():
                try:
                    chunk = response.read1(16384)
                except (OSError, ValueError):
                    break  # closed by cancel()
                if not chunk:
                    break
                chunk, odd = odd + chunk, b""
                if len(chunk) % 2:  # whole samples only
                    chunk, odd = chunk[:-1], chunk[-1:]
                yield chunk

    return play(stream(), job, ELEVEN_RATE)


_piper_voices = {}
# One synthesis at a time per model: an interrupted answer's last sentence may still be in work.
_model_lock = threading.Lock()


def speak_piper(text, lang, settings, root, job):
    from piper import PiperVoice, SynthesisConfig
    model = root / "piper" / f"{settings['voices'].get(lang) or DEFAULT_VOICES.get(lang, '')}.onnx"
    if not model.exists():
        raise SpeechError(f"Piper: the voice {model.stem} is not installed")
    key = (str(model), model.stat().st_mtime)
    voice = _piper_voices.get(key)
    if voice is None:  # loaded once per server, not once per answer
        voice = _piper_voices[key] = PiperVoice.load(str(model))
    config = SynthesisConfig(length_scale=1.0 / settings["rate"])
    first = []

    def chunks():
        sentences = voice.synthesize(text, config)
        while not job.cancelled.is_set():
            with _model_lock:
                chunk = next(sentences, None)
            if chunk is None:
                return
            if not first:
                first.append((chunk.sample_rate, chunk.sample_channels))
            yield chunk.audio_int16_bytes

    iterator = chunks()
    try:
        head = next(iterator)
    except StopIteration:
        return True
    rate, channels = first[0]

    def all_chunks():
        yield head
        yield from iterator

    return play(all_chunks(), job, rate, channels)


_chatterbox = {}


def chatterbox_model(root, device):
    model = _chatterbox.get(device)
    if model is None:
        # The weights were downloaded at installation into Jarvis's folder; no network now.
        os.environ.setdefault("HF_HOME", str(root / "chatterbox" / "hf"))
        os.environ.setdefault("HF_HUB_OFFLINE", "1")
        os.environ.setdefault("HF_HUB_DISABLE_TELEMETRY", "1")
        import torch
        from chatterbox.mtl_tts import ChatterboxMultilingualTTS
        target = ("cuda" if torch.cuda.is_available() else "cpu") if device == "auto" else device
        torch.set_num_threads(max(1, (os.cpu_count() or 2) - 1))
        model = _chatterbox[device] = ChatterboxMultilingualTTS.from_pretrained(device=target)
    return model


def speak_chatterbox(text, lang, settings, root, job):
    import torch
    cfg = settings["chatterbox"]
    device = cfg.get("device") if cfg.get("device") in ("auto", "cpu", "cuda") else "auto"
    model = chatterbox_model(root, device)
    try:
        exaggeration = max(0.25, min(2.0, float(cfg.get("exaggeration", 0.5))))
        cfg_weight = max(0.0, min(1.0, float(cfg.get("cfg_weight", 0.5))))
    except (TypeError, ValueError):
        exaggeration, cfg_weight = 0.5, 0.5
    reference = next((p for p in (root / "chatterbox" / f"reference.{s}" for s in ("wav", "flac", "mp3", "ogg")) if p.exists()), None)
    if reference:
        marker = (str(reference), reference.stat().st_mtime, exaggeration)
        with _model_lock:
            if getattr(model, "_jarvis_reference", None) != marker:  # a new sample: prepare it once
                model.prepare_conditionals(str(reference), exaggeration=exaggeration)
                model._jarvis_reference = marker

    def chunks():
        for piece in sentences(text):
            if job.cancelled.is_set():
                return
            with _model_lock, torch.inference_mode():
                wav = model.generate(piece, language_id=lang, exaggeration=exaggeration, cfg_weight=cfg_weight)
            yield (wav.squeeze(0).clamp(-1, 1) * 32767).to(torch.int16).cpu().numpy().tobytes()

    return play(chunks(), job, model.sr)


def speak_system(text, lang, settings, root, job):
    try:
        import speechd
    except ImportError:
        speechd = None
    if speechd is None:  # inside a voice environment: the command-line client
        tool = shutil.which("spd-say")
        if not tool:
            raise SpeechError("Speech Dispatcher is not installed")
        process = subprocess.Popen([tool, "-w", "-l", lang, "-e"], stdin=subprocess.PIPE, stderr=subprocess.DEVNULL)
        job.on_cancel(lambda: (subprocess.run([tool, "-S"], timeout=5, check=False), process.kill()))
        process.communicate(text.encode())
        return not job.cancelled.is_set()
    finished = threading.Event()
    client = speechd.SSIPClient('Jarvis', component='responses')
    job.on_cancel(lambda: (client.cancel(), finished.set()))
    try:
        client.set_data_mode(speechd.DataMode.TEXT)
        client.set_language(lang)
        client.speak(text, callback=lambda *_args, **_kwargs: finished.set(),
                     event_types=(speechd.CallbackType.END, speechd.CallbackType.CANCEL))
        if not finished.wait(300):
            raise SpeechError("Speech Dispatcher did not finish")
        return not job.cancelled.is_set()
    finally:
        client.cancel()
        client.close()


SPEAKERS = {"elevenlabs": speak_elevenlabs, "chatterbox": speak_chatterbox, "piper": speak_piper, "system": speak_system}


def speak_local(text, lang, root, job, report, delegate=True):
    """Tries the planned engines in order. Engines of another Python environment
    run as a child process (`--engine`), unless `delegate` is off (the server)."""
    settings = load_settings(root)
    order = plan(settings, root)
    if not delegate and order and not can_run_here(order[0], root):
        return False  # the chosen voice lives in another environment: the client runs it
    for engine in order:
        if job.cancelled.is_set():
            return True
        if not can_run_here(engine, root):
            if not delegate:
                continue
            python = chatterbox_python(root) if engine == "chatterbox" else venv_python(root)
            child = subprocess.Popen([str(python), os.path.abspath(__file__), lang, "--engine", engine],
                                     stdin=subprocess.PIPE, stdout=subprocess.PIPE)
            job.on_cancel(child.terminate)  # "stop" must silence the voice, not only this process
            child.stdin.write(text.encode())
            child.stdin.close()
            for line in child.stdout:  # live: the sound level arrives while it plays
                try:
                    event = json.loads(line)
                except ValueError:
                    continue
                if event.get("event") in ("engine", "error", "level"):
                    report(event)
            child.wait()
            if child.returncode == 0 or job.cancelled.is_set():
                return True
            continue
        try:
            report({"event": "engine", "name": engine})
            SPEAKERS[engine](text, lang, settings, root, job)
            return True
        except Exception as error:  # the next engine takes over
            if job.cancelled.is_set():
                return True
            report({"event": "error", "text": str(error)[:300] or type(error).__name__})
    return False


# ---- Speech server ----------------------------------------------------------------------

def serve(root):
    """Keeps models in memory and speaks one answer at a time; a new answer (or the
    client hanging up) silences the current one. Ends when its parent closes stdin."""
    path = socket_path()
    probe = socket.socket(socket.AF_UNIX, socket.SOCK_STREAM)
    try:
        probe.connect(str(path))
        probe.close()
        emit("ready", text="already running")
        return 0
    except OSError:
        probe.close()
    path.parent.mkdir(parents=True, exist_ok=True, mode=0o700)
    try:
        path.unlink()
    except FileNotFoundError:
        pass
    server = socket.socket(socket.AF_UNIX, socket.SOCK_STREAM)
    old = os.umask(0o177)
    try:
        server.bind(str(path))
    finally:
        os.umask(old)
    os.chmod(path, 0o600)
    server.listen(4)

    def quit_on_eof():
        try:
            while sys.stdin.buffer.read(4096):
                pass
        finally:
            try:
                path.unlink()
            except OSError:
                pass
            os._exit(0)

    threading.Thread(target=quit_on_eof, daemon=True).start()
    state = {"job": None}
    state_lock, speech_lock = threading.Lock(), threading.Lock()

    def handle(conn):
        with conn:
            try:
                reader = conn.makefile("rb")
                request = json.loads(reader.readline(8 * MAX_TEXT + 1024) or b"{}")
                text = str(request.get("text", ""))[:MAX_TEXT]
                lang = request.get("lang") if request.get("lang") in DEFAULT_VOICES else language(text)
            except (OSError, ValueError, AttributeError):
                return
            job = Job()
            with state_lock:
                if state["job"]:
                    state["job"].cancel()
                state["job"] = job

            def watch():  # the client hung up: stop
                try:
                    while conn.recv(64):
                        pass
                except OSError:
                    pass
                job.cancel()

            threading.Thread(target=watch, daemon=True).start()

            send_lock = threading.Lock()

            def report(event):
                try:
                    with send_lock:
                        conn.sendall((json.dumps(event, ensure_ascii=False) + "\n").encode())
                except OSError:
                    pass

            job.report = report

            with speech_lock:
                ok = job.cancelled.is_set() or speak_local(text, lang, root, job, report, delegate=False)
                if not ok:  # nothing here can speak: the client tries by itself
                    report({"event": "unavailable"})
            report({"event": "done"})

    def warm():  # load the voice model now, not when the first answer arrives
        settings = load_settings(root)
        order = plan(settings, root)
        try:
            if order and order[0] == "chatterbox" and can_run_here("chatterbox", root):
                chatterbox_model(root, settings["chatterbox"].get("device") if settings["chatterbox"].get("device") in ("cpu", "cuda") else "auto")
            elif "piper" in order and can_run_here("piper", root):
                from piper import PiperVoice
                for lang in ("ru", "en"):
                    model = root / "piper" / f"{settings['voices'][lang]}.onnx"
                    if model.exists():
                        _piper_voices[(str(model), model.stat().st_mtime)] = PiperVoice.load(str(model))
        except Exception as error:
            print(f"warm-up failed: {error}", file=sys.stderr)

    def warm_locked():  # answers wait for the model instead of loading a second copy
        with speech_lock:
            warm()

    threading.Thread(target=warm_locked, daemon=True).start()
    emit("ready", text="speech server ready")
    while True:
        conn, _ = server.accept()
        try:
            _pid, uid, _gid = struct.unpack("3i", conn.getsockopt(socket.SOL_SOCKET, socket.SO_PEERCRED, struct.calcsize("3i")))
        except OSError:
            uid = -1
        if uid != os.getuid():
            conn.close()
            continue
        threading.Thread(target=handle, args=(conn,), daemon=True).start()


def speak_via_server(text, lang, job):
    """True when a running server spoke (or was stopped), None when there is none."""
    conn = socket.socket(socket.AF_UNIX, socket.SOCK_STREAM)
    try:
        conn.connect(str(socket_path()))
    except OSError:
        conn.close()
        return None
    job.on_cancel(lambda: conn.shutdown(socket.SHUT_RDWR))
    with conn:
        conn.sendall((json.dumps({"text": text, "lang": lang}, ensure_ascii=False) + "\n").encode())
        unavailable = False
        for line in conn.makefile("rb"):
            try:
                event = json.loads(line)
            except ValueError:
                continue
            kind = event.get("event")
            if kind == "done":
                return False if unavailable else True
            if kind == "unavailable":
                unavailable = True
            elif kind in ("engine", "error", "level"):
                send(event)
    return True if job.cancelled.is_set() else None


def status(root):
    settings = load_settings(root)
    order = plan(settings, root)
    return {"engine": settings["engine"], "speaks": order[0] if order else "",
            "ready": {e: ready(e, settings, root) for e in ENGINES}}


def main():
    root = voice_root()
    if "--serve" in sys.argv:
        return serve(root)
    if "--status" in sys.argv:
        print(json.dumps(status(root)))
        return 0
    args = [a for a in sys.argv[1:] if not a.startswith("--")]
    forced = args[0] if args and args[0] in DEFAULT_VOICES else None
    text = sys.stdin.read(MAX_TEXT)
    if not text.strip():
        return 0
    lang = forced or language(text)
    spoken = speakable(text, lang)
    if not spoken:
        return 0
    job = Job(report=send)

    def stop(*_):
        job.cancel()

    signal.signal(signal.SIGTERM, stop)
    signal.signal(signal.SIGINT, stop)
    if "--engine" in sys.argv:  # a child in a voice environment: just this engine
        engine = sys.argv[sys.argv.index("--engine") + 1] if sys.argv.index("--engine") + 1 < len(sys.argv) else ""
        if engine not in SPEAKERS:
            return 2
        try:
            emit("engine", name=engine)
            SPEAKERS[engine](spoken, lang, load_settings(root), root, job)
            return 0
        except Exception as error:
            if job.cancelled.is_set():
                return 0
            emit("error", text=str(error)[:300] or type(error).__name__)
            return 3
    if "--no-server" not in sys.argv:
        result = speak_via_server(spoken, lang, job)
        if result:
            return 0
    return 0 if speak_local(spoken, lang, root, job, send) else 1


if __name__ == '__main__':
    try:
        sys.exit(main())
    except SystemExit:
        raise
    except KeyboardInterrupt:
        sys.exit(0)
    except Exception as error:
        emit("error", text=str(error)[:300])
        sys.exit(1)
