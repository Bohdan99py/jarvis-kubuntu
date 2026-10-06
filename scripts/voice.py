#!/usr/bin/env python3
"""Local Vosk setup and push-to-talk recognition. Audio is never uploaded."""
import argparse
import json
import os
from pathlib import Path
import selectors
import signal
import subprocess
import sys
import tempfile
import time
import urllib.request
import zipfile

MODELS = {"ru": "vosk-model-small-ru-0.22", "en": "vosk-model-small-en-us-0.15"}
MODEL = MODELS["ru"]
MAX_ARCHIVE = 100 * 1024 * 1024
MESSAGES = {
    "ru": {"setup": "Установка локального распознавания речи…", "big": "Архив модели слишком большой",
           "unsafe": "Небезопасный путь в архиве модели", "download": "Загрузка модели Vosk, около 45 МБ…",
           "https": "Загрузка требует HTTPS", "missing": "В архиве нет модели Vosk",
           "ready": "Голос готов. Нажмите микрофон и говорите.", "wav": "Нужен WAV PCM16, моно, 16000 Гц",
           "mic": "Не удалось записать микрофон: ", "listening": "Слушаю… Нажмите стоп, когда закончите (до 20 секунд)."},
    "en": {"setup": "Installing local speech recognition…", "big": "The model archive is too large",
           "unsafe": "Unsafe path in the model archive", "download": "Downloading the Vosk model, about 40 MB…",
           "https": "The download requires HTTPS", "missing": "The archive has no Vosk model",
           "ready": "Voice is ready. Press the microphone and speak.", "wav": "A 16 kHz mono PCM16 WAV is required",
           "mic": "Could not record the microphone: ", "listening": "Listening… Press stop when you are done (up to 20 seconds)."},
}
LANG = "ru"

def message(key):
    return MESSAGES.get(LANG, MESSAGES["en"])[key]

def event(kind, **data):
    print(json.dumps(dict(event=kind, **data), ensure_ascii=False), flush=True)

def safe_extract(archive, destination):
    with zipfile.ZipFile(archive) as z:
        if sum(i.file_size for i in z.infolist()) > 400 * 1024 * 1024:
            raise ValueError(message("big"))
        for info in z.infolist():
            target = (destination / info.filename).resolve()
            if not target.is_relative_to(destination.resolve()) or (info.external_attr >> 16) & 0o170000 == 0o120000:
                raise ValueError(message("unsafe"))
        z.extractall(destination)

def setup(root, model=MODEL):
    import fcntl
    import shutil
    import venv
    root.mkdir(parents=True, exist_ok=True, mode=0o700)
    with (root / ".setup.lock").open("w") as lock:
        fcntl.flock(lock, fcntl.LOCK_EX | fcntl.LOCK_NB)
        event("status", text=message("setup"))
        environment = root / "venv"
        if not (environment / "bin/python").exists():
            venv.EnvBuilder(with_pip=True).create(environment)
        python = str(environment / "bin/python")
        subprocess.run([python, "-m", "pip", "install", "--disable-pip-version-check", "--timeout", "30", "vosk==0.3.45"],
                       check=True, stdout=sys.stderr, timeout=300)
        subprocess.run([python, "-c", "import vosk"], check=True, timeout=30)
        target = root / model
        if not (target / "am/final.mdl").is_file():
            event("status", text=message("download"))
            with tempfile.TemporaryDirectory(prefix="download-", dir=root) as temp:
                temp = Path(temp)
                archive = temp / "model.zip"
                url = f"https://alphacephei.com/vosk/models/{model}.zip"
                request = urllib.request.Request(url, headers={"User-Agent": "Jarvis/0.7"})
                with urllib.request.urlopen(request, timeout=60) as response, archive.open("wb") as out:
                    if not response.url.startswith("https://"):
                        raise ValueError(message("https"))
                    total = 0
                    while chunk := response.read(1024 * 1024):
                        total += len(chunk)
                        if total > MAX_ARCHIVE:
                            raise ValueError(message("big"))
                        out.write(chunk)
                safe_extract(archive, temp)
                if not (temp / model / "am/final.mdl").is_file():
                    raise ValueError(message("missing"))
                if target.exists():
                    shutil.rmtree(target)
                (temp / model).rename(target)
        # Verify that the model can actually be loaded before marking it ready.
        subprocess.run([python, "-c", "import vosk,sys; vosk.SetLogLevel(-1); vosk.Model(sys.argv[1])", str(target)],
                       check=True, timeout=60, stdout=sys.stderr)
        (root / "ready").write_text("vosk==0.3.45\n", encoding="utf-8")
        event("ready", text=message("ready"))

def recognize(model, wav=None, device="default"):
    from vosk import Model, KaldiRecognizer, SetLogLevel
    SetLogLevel(-1)
    recognizer = KaldiRecognizer(Model(str(model)), 16000)
    parts = []
    def accept(data):
        if recognizer.AcceptWaveform(data):
            text = json.loads(recognizer.Result()).get("text", "")
            if text:
                parts.append(text)
    if wav:
        import wave
        with wave.open(str(wav), "rb") as audio:
            if audio.getnchannels() != 1 or audio.getsampwidth() != 2 or audio.getframerate() != 16000:
                raise ValueError(message("wav"))
            while data := audio.readframes(4000):
                accept(data)
    else:
        recorder = subprocess.Popen(["arecord", "-q", "-d", "20", "-D", device, "-t", "raw", "-f", "S16_LE", "-r", "16000", "-c", "1"],
                                    stdout=subprocess.PIPE, stderr=subprocess.PIPE)
        try:
            selector = selectors.DefaultSelector()
            selector.register(recorder.stdout, selectors.EVENT_READ)
            selector.register(sys.stdin, selectors.EVENT_READ)
            event("listening", text=message("listening"))
            deadline = time.monotonic() + 20
            stop = False
            while not stop and time.monotonic() < deadline:
                for key, _ in selector.select(.15):
                    if key.fileobj == sys.stdin:
                        sys.stdin.readline()
                        stop = True
                        break
                    data = os.read(recorder.stdout.fileno(), 8000)
                    if not data:
                        if recorder.wait(timeout=2) == 0:
                            stop = True
                            break
                        error = recorder.stderr.read(2048).decode(errors="replace")
                        raise RuntimeError(message("mic") + error)
                    accept(data)
        finally:
            recorder.terminate()
            try:
                recorder.wait(timeout=2)
            except subprocess.TimeoutExpired:
                recorder.kill()
                recorder.wait()
    final = json.loads(recognizer.FinalResult()).get("text", "")
    if final:
        parts.append(final)
    event("text", text=" ".join(parts))

def main():
    def terminate(signum, frame):
        raise SystemExit(128 + signum)
    signal.signal(signal.SIGTERM, terminate)
    parser = argparse.ArgumentParser()
    parser.add_argument("mode", choices=["setup", "listen", "transcribe"])
    parser.add_argument("--root", type=Path, required=True)
    parser.add_argument("--wav", type=Path)
    parser.add_argument("--device", default="default")
    parser.add_argument("--lang", choices=sorted(MODELS), default="ru")
    args = parser.parse_args()
    global LANG
    LANG = args.lang
    model = MODELS[args.lang]
    try:
        if args.mode == "setup":
            setup(args.root, model)
        else:
            recognize(args.root / model, args.wav if args.mode == "transcribe" else None, args.device)
    except Exception as error:
        event("error", text=str(error))
        return 1
    return 0

if __name__ == "__main__":
    sys.exit(main())
