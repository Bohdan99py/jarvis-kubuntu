#!/usr/bin/env python3
"""Language study helper: lesson recording, French transcription, PDF text.

Speech is recognized on this computer with faster-whisper (models from
Hugging Face, installed into ${XDG_DATA_HOME}/jarvis/study); audio is never
uploaded. Events go to stdout as JSON lines; a line on stdin stops recording.

Modes:
  setup       create the venv, install faster-whisper, download a model
  record      record the microphone to a 16 kHz mono WAV; --live also
              transcribes phrase by phrase while recording
  listen      record one phrase until stopped, then transcribe it
  transcribe  transcribe an audio file into <name>.fr.json / <name>.fr.txt
  import      convert any audio/video file into a 16 kHz mono WAV
  play        play a WAV from --start to --end through aplay
  pdf         extract paragraphs from a PDF (pdftotext, OCR with tesseract for scans)
"""
import argparse
import array
import json
import math
import os
from pathlib import Path
import queue
import re
import selectors
import shutil
import signal
import subprocess
import sys
import tempfile
import threading
import time
import wave

RATE = 16000
FRAME = 480  # 30 ms
MODELS = {"base": 145, "small": 484, "medium": 1530}  # approximate download size, MB
PACKAGES = ["faster-whisper==1.2.1", "av>=12,<17"]
MAX_RECORDING = 4 * 3600
MAX_PDF_PAGES = 500
MAX_PDF_CHARS = 2_000_000
# Whisper's typical inventions on silence or music in French videos.
HALLUCINATIONS = re.compile(
    r"amara\.org|sous-titr|merci d'avoir regard|abonnez-vous|n'oubliez pas de vous abonner|"
    r"transcription par|traduction par|www\.|\.com\b", re.IGNORECASE)

MESSAGES = {
    "ru": {"setup": "Установка распознавания французской речи…",
           "pip": "Загрузка faster-whisper из PyPI (около 450 МБ)…",
           "model": "Загрузка модели Whisper «{}» (около {} МБ)…",
           "check": "Проверка модели…",
           "ready": "Распознавание французского готово.",
           "mic": "Не удалось записать микрофон: ",
           "recording": "Идёт запись…",
           "finishing": "Дораспознаю последние фразы…",
           "loading": "Загрузка модели распознавания…",
           "transcribing": "Распознаю французскую речь…",
           "listening": "Говорите по-французски… Нажмите стоп, когда закончите.",
           "nopdf": "Не найден pdftotext (пакет poppler-utils).",
           "pdfpages": "В PDF больше {} страниц.",
           "ocr": "Распознаю скан, страница {} из {}…",
           "noocr": "В PDF нет текстового слоя. Для сканов установите tesseract-ocr-fra.",
           "empty": "В PDF не найден текст.",
           "noaudio": "В файле нет звуковой дорожки.",
           "notready": "Сначала установите распознавание французского."},
    "en": {"setup": "Installing French speech recognition…",
           "pip": "Downloading faster-whisper from PyPI (about 450 MB)…",
           "model": "Downloading the Whisper \"{}\" model (about {} MB)…",
           "check": "Checking the model…",
           "ready": "French recognition is ready.",
           "mic": "Could not record the microphone: ",
           "recording": "Recording…",
           "finishing": "Recognizing the last phrases…",
           "loading": "Loading the recognition model…",
           "transcribing": "Recognizing French speech…",
           "listening": "Speak French… Press stop when you are done.",
           "nopdf": "pdftotext was not found (package poppler-utils).",
           "pdfpages": "The PDF has more than {} pages.",
           "ocr": "Recognizing the scan, page {} of {}…",
           "noocr": "The PDF has no text layer. For scans install tesseract-ocr-fra.",
           "empty": "No text was found in the PDF.",
           "noaudio": "The file has no audio track.",
           "notready": "Install French recognition first."},
}
LANG = "ru"
STOP = threading.Event()


def message(key, *args):
    return MESSAGES.get(LANG, MESSAGES["en"])[key].format(*args)


def event(kind, **data):
    print(json.dumps(dict(event=kind, **data), ensure_ascii=False), flush=True)


# ---------------------------------------------------------------------------
# Pure helpers (unit-tested)
# ---------------------------------------------------------------------------

def rms(samples):
    """Root mean square of int16 samples, 0..1."""
    if not samples:
        return 0.0
    return math.sqrt(sum(s * s for s in samples) / len(samples)) / 32768.0


def timestamp(seconds):
    seconds = int(max(0, seconds))
    h, rest = divmod(seconds, 3600)
    m, s = divmod(rest, 60)
    return f"{h}:{m:02d}:{s:02d}" if h else f"{m:02d}:{s:02d}"


def clean_segment(text):
    """Whitespace-normalized text, or "" for Whisper's silence inventions."""
    text = " ".join(text.split())
    if not text or HALLUCINATIONS.search(text) or not any(c.isalpha() for c in text):
        return ""
    return text


class Chunker:
    """Cuts a live audio stream into phrases at pauses.

    feed() takes 30 ms frames and returns a finished phrase (bytes, start
    second) when speech is followed by a pause, or when a phrase gets long.
    The noise floor adapts, so a quiet room and a lecture hall both work.
    """

    # Whisper needs context: phrases of at least a few seconds recognize far
    # better than single words, at the cost of a few seconds of delay.
    def __init__(self, pause=0.6, shortest=4.0, longest=15.0):
        self.pause_frames = int(pause * RATE / FRAME)
        self.shortest = int(shortest * RATE / FRAME)
        self.longest = int(longest * RATE / FRAME)
        self.noise = 0.004
        self.frames = []
        self.silent = 0
        self.speech = 0
        self.position = 0  # frames seen
        self.start = 0

    def feed(self, frame):
        level = rms(array.array("h", frame))
        self.position += 1
        # The floor follows quiet stretches quickly and loud ones slowly (~15 s).
        self.noise = self.noise * 0.95 + level * 0.05 if level < self.noise * 1.5 else self.noise * 0.998 + level * 0.002
        speaking = level > max(self.noise * 3.0, 0.006)
        if not self.frames:
            if not speaking:
                return None
            self.start = self.position - 1
        self.frames.append(frame)
        self.speech += 1 if speaking else 0
        self.silent = 0 if speaking else self.silent + 1
        if (self.silent >= self.pause_frames and len(self.frames) >= self.shortest) or len(self.frames) >= self.longest:
            return self.flush()
        return None

    def flush(self):
        if not self.frames or self.speech < 5:  # a click, not speech
            self.frames, self.speech, self.silent = [], 0, 0
            return None
        phrase = (b"".join(self.frames), self.start * FRAME / RATE)
        self.frames, self.speech, self.silent = [], 0, 0
        return phrase


def reflow(page):
    """Paragraphs of one pdftotext page. pdftotext drops blank lines, so a
    paragraph ends at a line that finishes a sentence well short of the text
    width; short lines without final punctuation are headings; wrapped lines
    are joined and hyphenation is undone."""
    lines = [line.strip() for line in page.splitlines()]
    lines = [line for line in lines if not re.fullmatch(r"[-–—]?\s*\d{1,4}\s*[-–—]?", line)]  # page numbers
    lengths = sorted(len(line) for line in lines if line)
    if not lengths:
        return []
    width = lengths[min(len(lengths) - 1, int(len(lengths) * 0.9))]  # typical full line, ignoring outliers
    paragraphs, current = [], []

    def close():
        if current:
            text = re.sub(r"\s+", " ", " ".join(current)).strip()
            if text:
                paragraphs.append(text)
            current.clear()

    for i, line in enumerate(lines):
        if not line:
            # OCR puts blank lines between wrapped lines too: a blank line only
            # ends a paragraph whose last line looks finished.
            if current and (len(current[-1]) < width * 0.8 or re.search(r"[.:!?»…]$", current[-1])):
                close()
            continue
        following = next((l for l in lines[i + 1:] if l), "")
        if re.match(r"^([-•▪●*–]|\d+[.)])\s", line):  # list item
            close()
        if current and current[-1].endswith("-") and line[:1].islower():
            current[-1] = current[-1][:-1] + line
        else:
            current.append(line)
        short = len(line) < width * 0.8
        sentence_end = re.search(r"[.:!?»…]$", line) is not None
        heading = len(line) < width * 0.6 and not sentence_end and not line.endswith(("-", ",")) \
            and len(current) == 1 and following[:1].isupper()
        if (short and sentence_end) or heading:
            close()
    close()
    return split_long(paragraphs)


def split_long(paragraphs, limit=1800):
    """Paragraphs longer than `limit` split at sentence ends (translation chunks)."""
    out = []
    for p in paragraphs:
        while len(p) > limit:
            cut = max(p.rfind(". ", 0, limit), p.rfind("! ", 0, limit), p.rfind("? ", 0, limit))
            cut = cut + 1 if cut > limit // 3 else limit
            out.append(p[:cut].strip())
            p = p[cut:].strip()
        if p:
            out.append(p)
    return out


def write_transcript(audio, segments, duration, model):
    """<name>.fr.json (segments for the app) and <name>.fr.txt (for people)."""
    base = audio.with_suffix("")
    data = {"version": 1, "language": "fr", "model": model, "duration": duration, "segments": segments}
    temp = base.with_name(base.name + ".fr.json.part")
    temp.write_text(json.dumps(data, ensure_ascii=False, indent=1), encoding="utf-8")
    temp.replace(base.with_name(base.name + ".fr.json"))
    lines = [f"[{timestamp(s['start'])}] {s['text']}" for s in segments]
    base.with_name(base.name + ".fr.txt").write_text("\n".join(lines) + "\n", encoding="utf-8")


# ---------------------------------------------------------------------------
# Whisper
# ---------------------------------------------------------------------------

def model_dir(root, model):
    return root / "models" / model


def load_model(root, model):
    from faster_whisper import WhisperModel
    path = model_dir(root, model)
    if not (path / "model.bin").is_file():
        raise RuntimeError(message("notready"))
    threads = max(1, (os.cpu_count() or 2) - 1)
    return WhisperModel(str(path), device="cpu", compute_type="int8", cpu_threads=threads)


def directory_size(path):
    return sum(f.stat().st_size for f in path.rglob("*") if f.is_file())


def setup(root, model):
    import fcntl
    import venv
    root.mkdir(parents=True, exist_ok=True, mode=0o700)
    with (root / ".setup.lock").open("w") as lock:
        fcntl.flock(lock, fcntl.LOCK_EX | fcntl.LOCK_NB)
        event("status", text=message("setup"))
        environment = root / "venv"
        python = environment / "bin/python"
        if not python.exists():
            venv.EnvBuilder(with_pip=True).create(environment)
        event("status", text=message("pip"))
        subprocess.run([str(python), "-m", "pip", "install", "--disable-pip-version-check", "--timeout", "60", *PACKAGES],
                       check=True, stdout=sys.stderr, timeout=1800)
        target = model_dir(root, model)
        if not (target / "model.bin").is_file():
            event("status", text=message("model", model, MODELS[model]))
            target.mkdir(parents=True, exist_ok=True)
            expected = MODELS[model] * 1024 * 1024
            done = threading.Event()

            def report():
                while not done.wait(1.0):
                    event("progress", value=min(0.99, directory_size(target) / expected))
            reporter = threading.Thread(target=report, daemon=True)
            reporter.start()
            try:
                # In the venv: download and verify in one go.
                subprocess.run([str(python), "-c",
                                "import sys; from faster_whisper import download_model; "
                                "download_model(sys.argv[1], output_dir=sys.argv[2])", model, str(target)],
                               check=True, stdout=sys.stderr, timeout=3600)
            finally:
                done.set()
        event("status", text=message("check"))
        subprocess.run([str(python), "-c",
                        "import sys; from faster_whisper import WhisperModel; "
                        "WhisperModel(sys.argv[1], device='cpu', compute_type='int8')", str(target)],
                       check=True, stdout=sys.stderr, timeout=300)
        ready = root / "ready.json"
        try:
            state = json.loads(ready.read_text(encoding="utf-8"))
        except (OSError, ValueError):
            state = {}
        state["models"] = sorted(set(state.get("models", [])) | {model})
        state["packages"] = PACKAGES
        ready.write_text(json.dumps(state), encoding="utf-8")
        event("ready", text=message("ready"))


def transcribe_audio(model, audio, offset=0.0, prompt=None, live=False):
    """[{start, end, text}] for a path or a float32 array. Live phrases get the
    previous phrase as a prompt and no temperature fallback, so one bad phrase
    cannot stall the stream."""
    # The VAD filter stays on: it is the best guard against text invented from noise.
    segments, _ = model.transcribe(audio, language="fr", beam_size=5, vad_filter=True, initial_prompt=prompt,
                                   condition_on_previous_text=not live, temperature=0.0 if live else
                                   (0.0, 0.2, 0.4, 0.6, 0.8, 1.0))
    out = []
    for s in segments:
        text = clean_segment(s.text)
        if text:
            out.append({"start": round(offset + s.start, 2), "end": round(offset + s.end, 2), "text": text})
    return out


def pcm_to_float(data):
    import numpy
    return numpy.frombuffer(data, dtype=numpy.int16).astype(numpy.float32) / 32768.0


def transcribe(root, model_name, audio):
    event("status", text=message("loading"))
    model = load_model(root, model_name)
    event("status", text=message("transcribing"))
    segments_iter, info = model.transcribe(str(audio), language="fr", beam_size=5, vad_filter=True)
    duration = info.duration or 0
    segments = []
    for s in segments_iter:
        text = clean_segment(s.text)
        if not text:
            continue
        segment = {"start": round(s.start, 2), "end": round(s.end, 2), "text": text}
        segments.append(segment)
        event("segment", index=len(segments) - 1, **segment)
        if duration:
            event("progress", value=min(1.0, s.end / duration))
    write_transcript(audio, segments, duration, model_name)
    event("done", segments=len(segments), duration=duration)


# ---------------------------------------------------------------------------
# Microphone
# ---------------------------------------------------------------------------

def stop_on_stdin():
    """A line (or EOF) on stdin stops recording."""
    def wait():
        try:
            sys.stdin.readline()
        finally:
            STOP.set()
    threading.Thread(target=wait, daemon=True).start()


def microphone(device):
    return subprocess.Popen(["arecord", "-q", "-D", device, "-t", "raw", "-f", "S16_LE", "-r", str(RATE), "-c", "1"],
                            stdout=subprocess.PIPE, stderr=subprocess.PIPE)


def frames(recorder, limit):
    """30 ms frames from arecord until STOP or `limit` seconds."""
    selector = selectors.DefaultSelector()
    selector.register(recorder.stdout, selectors.EVENT_READ)
    pending = b""
    deadline = time.monotonic() + limit
    while not STOP.is_set() and time.monotonic() < deadline:
        if not selector.select(0.2):
            continue
        data = os.read(recorder.stdout.fileno(), FRAME * 2 * 8)
        if not data:
            if recorder.wait(timeout=2) != 0:
                raise RuntimeError(message("mic") + recorder.stderr.read(2048).decode(errors="replace").strip())
            return
        pending += data
        while len(pending) >= FRAME * 2:
            yield pending[:FRAME * 2]
            pending = pending[FRAME * 2:]


def close_recorder(recorder):
    recorder.terminate()
    try:
        recorder.wait(timeout=2)
    except subprocess.TimeoutExpired:
        recorder.kill()
        recorder.wait()


def record(out, device, live, root, model_name):
    """Records until stopped. The WAV is written as <out>.part and renamed when
    complete; with --live, phrases are transcribed on a second thread."""
    part = out.with_name(out.name + ".part")
    phrases = queue.Queue()
    segments = []
    worker = None
    if live:
        def transcriber():
            # The model loads while recording already runs: phrases wait in the queue.
            try:
                model = load_model(root, model_name)
            except Exception as error:
                event("warning", text=str(error))
                while phrases.get() is not None:
                    pass
                return
            previous = None
            while True:
                item = phrases.get()
                if item is None:
                    return
                data, start = item
                try:
                    found = transcribe_audio(model, pcm_to_float(data), start, previous, live=True)
                except Exception as error:  # keep recording even if one phrase fails
                    print(error, file=sys.stderr)
                    continue
                for s in found:
                    segments.append(s)
                    event("segment", index=len(segments) - 1, **s)
                if found:
                    previous = found[-1]["text"]
        worker = threading.Thread(target=transcriber, daemon=True)
        worker.start()
    chunker = Chunker()
    recorder = microphone(device)
    total = 0
    try:
        with wave.open(str(part), "wb") as wav:
            wav.setnchannels(1)
            wav.setsampwidth(2)
            wav.setframerate(RATE)
            event("recording", text=message("recording"))
            peak, last = 0.0, time.monotonic()
            for frame in frames(recorder, MAX_RECORDING):
                wav.writeframes(frame)
                total += len(frame) // 2
                level = rms(array.array("h", frame))
                peak = max(peak, level)
                if live:
                    phrase = chunker.feed(frame)
                    if phrase:
                        phrases.put(phrase)
                if time.monotonic() - last >= 0.2:
                    # Speech sits around 0.02–0.2: a square-root scale makes the meter readable.
                    event("level", value=round(min(1.0, math.sqrt(peak * 4)), 3), seconds=round(total / RATE, 1))
                    peak, last = 0.0, time.monotonic()
    finally:
        close_recorder(recorder)
        if part.exists():
            part.replace(out)
    seconds = total / RATE
    if live:
        phrase = chunker.flush()
        if phrase:
            phrases.put(phrase)
        event("status", text=message("finishing"))
        phrases.put(None)
        worker.join()
        segments.sort(key=lambda s: s["start"])
        write_transcript(out, segments, seconds, model_name)
    event("saved", path=str(out), seconds=round(seconds, 1), segments=len(segments))


def listen(root, model_name, device, limit=60):
    loaded = {}

    def load():
        try:
            loaded["model"] = load_model(root, model_name)
        except Exception as error:
            loaded["error"] = error
    loader = threading.Thread(target=load, daemon=True)
    loader.start()
    recorder = microphone(device)
    data = bytearray()
    try:
        event("recording", text=message("listening"))
        last = time.monotonic()
        peak = 0.0
        for frame in frames(recorder, limit):
            data += frame
            peak = max(peak, rms(array.array("h", frame)))
            if time.monotonic() - last >= 0.2:
                event("level", value=round(min(1.0, math.sqrt(peak * 4)), 3), seconds=round(len(data) / 2 / RATE, 1))
                peak, last = 0.0, time.monotonic()
    finally:
        close_recorder(recorder)
    event("status", text=message("transcribing"))
    loader.join()
    if "error" in loaded:
        raise loaded["error"]
    found = transcribe_audio(loaded["model"], pcm_to_float(bytes(data))) if len(data) > RATE else []
    event("text", text=" ".join(s["text"] for s in found))


# ---------------------------------------------------------------------------
# Files
# ---------------------------------------------------------------------------

def import_audio(source, out):
    """Any audio or video file -> 16 kHz mono WAV (PyAV from the venv)."""
    import av
    part = out.with_name(out.name + ".part")
    try:
        with av.open(str(source)) as container, wave.open(str(part), "wb") as wav:
            if not container.streams.audio:
                raise RuntimeError(message("noaudio"))
            stream = container.streams.audio[0]
            duration = float(container.duration or 0) / 1_000_000
            resampler = av.AudioResampler(format="s16", layout="mono", rate=RATE)
            wav.setnchannels(1)
            wav.setsampwidth(2)
            wav.setframerate(RATE)
            last = 0.0
            for frame in container.decode(stream):
                for converted in resampler.resample(frame):
                    wav.writeframes(converted.to_ndarray().tobytes())
                if duration and frame.time is not None and frame.time - last > 5:
                    last = frame.time
                    event("progress", value=min(1.0, frame.time / duration))
            for converted in resampler.resample(None):
                wav.writeframes(converted.to_ndarray().tobytes())
        part.replace(out)
    finally:
        part.unlink(missing_ok=True)
    event("saved", path=str(out))


def play(audio, start, end):
    """Plays [start, end) of a 16-bit WAV through aplay; stops on SIGTERM."""
    with wave.open(str(audio), "rb") as wav:
        rate, channels = wav.getframerate(), wav.getnchannels()
        wav.setpos(min(wav.getnframes(), int(max(0.0, start) * rate)))
        remaining = int((end - start) * rate) if end and end > start else wav.getnframes()
        player = subprocess.Popen(["aplay", "-q", "-t", "raw", "-f", "S16_LE", "-r", str(rate), "-c", str(channels)],
                                  stdin=subprocess.PIPE)
        try:
            while remaining > 0 and not STOP.is_set():
                data = wav.readframes(min(rate // 5, remaining))
                if not data:
                    break
                remaining -= len(data) // (2 * channels)
                player.stdin.write(data)
            player.stdin.close()
            player.wait()
        finally:
            if player.poll() is None:
                player.terminate()
                player.wait()


def pdf_text(source):
    """[[paragraph, …] per page]; scanned pages go through tesseract when it has French."""
    if not shutil.which("pdftotext"):
        raise RuntimeError(message("nopdf"))
    raw = subprocess.run(["pdftotext", "-enc", "UTF-8", "-l", str(MAX_PDF_PAGES + 1), str(source), "-"],
                         check=True, capture_output=True, timeout=300).stdout.decode("utf-8", errors="replace")
    pages = raw.split("\f")
    if pages and not pages[-1].strip():
        pages.pop()
    if len(pages) > MAX_PDF_PAGES:
        raise RuntimeError(message("pdfpages", MAX_PDF_PAGES))
    scanned = [i for i, page in enumerate(pages) if len(page.strip()) < 20]
    ocr = 0
    if scanned and ocr_available():
        with tempfile.TemporaryDirectory(prefix="jarvis-ocr-") as temp:
            for n, index in enumerate(scanned):
                event("status", text=message("ocr", n + 1, len(scanned)))
                event("progress", value=n / len(scanned))
                image = Path(temp) / "page"
                subprocess.run(["pdftoppm", "-r", "300", "-gray", "-png", "-singlefile", "-f", str(index + 1),
                                "-l", str(index + 1), str(source), str(image)], check=True, timeout=120)
                pages[index] = subprocess.run(["tesseract", str(image) + ".png", "-", "-l", "fra"], check=True,
                                              capture_output=True, timeout=300).stdout.decode("utf-8", errors="replace")
                ocr += 1
    result = [reflow(page) for page in pages]
    total = sum(len(p) for page in result for p in page)
    if total == 0:
        raise RuntimeError(message("noocr") if scanned and not ocr_available() else message("empty"))
    if total > MAX_PDF_CHARS:
        raise RuntimeError(message("pdfpages", MAX_PDF_PAGES))
    event("pages", pages=result, ocr=ocr, chars=total)


def ocr_available():
    if not shutil.which("tesseract") or not shutil.which("pdftoppm"):
        return False
    try:
        langs = subprocess.run(["tesseract", "--list-langs"], capture_output=True, text=True, timeout=20).stdout
    except (OSError, subprocess.SubprocessError):
        return False
    return "fra" in langs.split()


def main():
    def terminate(signum, frame):
        STOP.set()
        raise SystemExit(128 + signum)
    signal.signal(signal.SIGTERM, terminate)
    parser = argparse.ArgumentParser()
    parser.add_argument("mode", choices=["setup", "record", "listen", "transcribe", "import", "play", "pdf"])
    parser.add_argument("--root", type=Path)
    parser.add_argument("--model", choices=sorted(MODELS), default="small")
    parser.add_argument("--in", dest="source", type=Path)
    parser.add_argument("--out", type=Path)
    parser.add_argument("--device", default="default")
    parser.add_argument("--live", action="store_true")
    parser.add_argument("--start", type=float, default=0.0)
    parser.add_argument("--end", type=float, default=0.0)
    parser.add_argument("--lang", choices=sorted(MESSAGES), default="ru")
    args = parser.parse_args()
    global LANG
    LANG = args.lang
    try:
        if args.mode == "setup":
            setup(args.root, args.model)
        elif args.mode == "record":
            stop_on_stdin()
            record(args.out, args.device, args.live, args.root, args.model)
        elif args.mode == "listen":
            stop_on_stdin()
            listen(args.root, args.model, args.device)
        elif args.mode == "transcribe":
            transcribe(args.root, args.model, args.source)
        elif args.mode == "import":
            import_audio(args.source, args.out)
        elif args.mode == "play":
            play(args.source, args.start, args.end)
        else:
            pdf_text(args.source)
    except SystemExit:
        raise
    except Exception as error:
        detail = str(error) or error.__class__.__name__
        if isinstance(error, subprocess.CalledProcessError) and error.stderr:
            detail = error.stderr.decode(errors="replace").strip()[-300:] if isinstance(error.stderr, bytes) else str(error.stderr)[-300:]
        event("error", text=detail)
        return 1
    return 0


if __name__ == "__main__":
    sys.exit(main())
