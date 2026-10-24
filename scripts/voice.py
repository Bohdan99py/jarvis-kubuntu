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
                request = urllib.request.Request(url, headers={"User-Agent": "Jarvis/0.19.2"})
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

def input_level(pcm):
    """Loudness of 16-bit mono PCM, 0…1 (speech at a normal distance ≈ 0.3–0.8)."""
    import array
    import math
    samples = array.array("h")
    samples.frombytes(pcm[:len(pcm) - len(pcm) % 2])
    if not samples:
        return 0.0
    rms = math.sqrt(sum(x * x for x in samples) / len(samples)) / 32768.0
    return round(min(1.0, rms * 6.0), 3)


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
            last_level = 0.0
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
                    now = time.monotonic()
                    if now - last_level >= 0.05:  # the microphone level for the interface, 20 times a second
                        last_level = now
                        event("level", v=input_level(data))
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

# Natural voices: Piper neural text-to-speech, run on this computer.
PIPER = "piper-tts==1.8.0"
VOICE_BASE = "https://huggingface.co/rhasspy/piper-voices/resolve/main/"
MAX_VOICE = 150 * 1024 * 1024
VOICE_MESSAGES = {
    "ru": {"tts": "Установка естественного голоса (Piper)…", "voice": "Загрузка голоса {name} (~63 МБ)…",
           "catalog": "Голос {name} не найден в каталоге Piper", "checksum": "Контрольная сумма голоса {name} не совпала",
           "tts_ready": "Естественный голос готов."},
    "en": {"tts": "Installing the natural voice (Piper)…", "voice": "Downloading the voice {name} (~63 MB)…",
           "catalog": "The voice {name} is not in the Piper catalog", "checksum": "The checksum of the voice {name} does not match",
           "tts_ready": "The natural voice is ready."},
}

def voice_message(key, **values):
    return VOICE_MESSAGES.get(LANG, VOICE_MESSAGES["en"])[key].format(**values)

def download(url, target, limit):
    request = urllib.request.Request(url, headers={"User-Agent": "Jarvis/0.19.2"})
    with urllib.request.urlopen(request, timeout=60) as response, target.open("wb") as out:
        if not response.url.startswith("https://"):
            raise ValueError(message("https"))
        total = 0
        while chunk := response.read(1024 * 1024):
            total += len(chunk)
            if total > limit:
                raise ValueError(message("big"))
            out.write(chunk)

def setup_tts(root, voices):
    """Install Piper into the voice environment and download the chosen voices,
    each checked against the MD5 published in the Piper catalog."""
    import fcntl
    import hashlib
    import venv
    root.mkdir(parents=True, exist_ok=True, mode=0o700)
    with (root / ".setup.lock").open("w") as lock:
        fcntl.flock(lock, fcntl.LOCK_EX | fcntl.LOCK_NB)
        event("status", text=voice_message("tts"))
        environment = root / "venv"
        if not (environment / "bin/python").exists():
            venv.EnvBuilder(with_pip=True).create(environment)
        python = str(environment / "bin/python")
        subprocess.run([python, "-m", "pip", "install", "--disable-pip-version-check", "--timeout", "30", PIPER],
                       check=True, stdout=sys.stderr, timeout=900)
        subprocess.run([python, "-c", "import piper"], check=True, timeout=60)
        target = root / "piper"
        target.mkdir(exist_ok=True)
        with tempfile.TemporaryDirectory(prefix="download-", dir=root) as temp:
            catalog = Path(temp) / "voices.json"
            download(VOICE_BASE + "voices.json", catalog, 8 * 1024 * 1024)
            entries = json.loads(catalog.read_text(encoding="utf-8"))
            for name in voices:
                entry = entries.get(name)
                if not entry or not all(c.isalnum() or c in "_-" for c in name):
                    raise ValueError(voice_message("catalog", name=name))
                for path, info in entry["files"].items():
                    if not path.endswith((".onnx", ".onnx.json")):
                        continue
                    final = target / Path(path).name
                    if final.exists() and hashlib.md5(final.read_bytes()).hexdigest() == info["md5_digest"]:
                        continue
                    if path.endswith(".onnx"):
                        event("status", text=voice_message("voice", name=name))
                    part = Path(temp) / Path(path).name
                    download(VOICE_BASE + path, part, MAX_VOICE)
                    if hashlib.md5(part.read_bytes()).hexdigest() != info["md5_digest"]:
                        raise ValueError(voice_message("checksum", name=name))
                    part.replace(final)
        # A voice that loads and speaks is a ready voice.
        first = target / (voices[0] + ".onnx")
        subprocess.run([python, "-c", "import sys; from piper import PiperVoice; list(PiperVoice.load(sys.argv[1]).synthesize('ok'))",
                        str(first)], check=True, timeout=120, stdout=sys.stderr)
        (target / "ready").write_text(PIPER + "\n", encoding="utf-8")
        event("ready", text=voice_message("tts_ready"))

# Chatterbox (Resemble AI): local neural speech in 23 languages that can take on a
# voice from a short sample. PyTorch is large: without an NVIDIA GPU the CPU build
# (~200 MB instead of ~2.5 GB) is installed. Its own environment keeps its pinned
# libraries away from Vosk and Piper.
CHATTERBOX = "chatterbox-tts==0.1.7"
CHATTERBOX_MESSAGES = {
    "ru": {"start": "Установка Chatterbox: PyTorch и программа (1–3 ГБ)…", "model": "Загрузка модели Chatterbox (~3 ГБ)…",
           "check": "Проверка голоса Chatterbox…", "ready": "Голос Chatterbox готов."},
    "en": {"start": "Installing Chatterbox: PyTorch and the program (1–3 GB)…", "model": "Downloading the Chatterbox model (~3 GB)…",
           "check": "Checking the Chatterbox voice…", "ready": "The Chatterbox voice is ready."},
}


def chatterbox_message(key):
    return CHATTERBOX_MESSAGES.get(LANG, CHATTERBOX_MESSAGES["en"])[key]


def has_nvidia():
    import shutil
    return bool(shutil.which("nvidia-smi")) or Path("/proc/driver/nvidia/version").exists()


def torch_packages(version=sys.version_info):
    """The PyTorch that chatterbox-tts 0.1.7 asks for on this Python."""
    if version >= (3, 14):
        return ["torch>=2.9.0", "torchaudio>=2.9.0"]
    return ["torch==2.6.0", "torchaudio==2.6.0"]


def setup_chatterbox(root, device="auto"):
    import fcntl
    import venv
    root.mkdir(parents=True, exist_ok=True, mode=0o700)
    with (root / ".setup.lock").open("w") as lock:
        fcntl.flock(lock, fcntl.LOCK_EX | fcntl.LOCK_NB)
        event("status", text=chatterbox_message("start"))
        home = root / "chatterbox"
        home.mkdir(exist_ok=True, mode=0o700)
        (home / "ready").unlink(missing_ok=True)
        environment = home / "venv"
        if not (environment / "bin/python").exists():
            venv.EnvBuilder(with_pip=True).create(environment)
        python = str(environment / "bin/python")
        pip = [python, "-m", "pip", "install", "--disable-pip-version-check", "--timeout", "60"]
        gpu = device == "cuda" or (device == "auto" and has_nvidia())
        if not gpu:
            subprocess.run(pip + ["--index-url", "https://download.pytorch.org/whl/cpu"] + torch_packages(),
                           check=True, stdout=sys.stderr, timeout=3600)
        subprocess.run(pip + [CHATTERBOX], check=True, stdout=sys.stderr, timeout=3600)
        # The weights come from Hugging Face into Jarvis's own folder.
        env = dict(os.environ, HF_HOME=str(home / "hf"), HF_HUB_DISABLE_TELEMETRY="1")
        event("status", text=chatterbox_message("model"))
        subprocess.run([python, "-c", "from chatterbox.mtl_tts import ChatterboxMultilingualTTS as M; M.from_pretrained(device='cpu')"],
                       check=True, timeout=3600, stdout=sys.stderr, env=env)
        event("status", text=chatterbox_message("check"))
        subprocess.run([python, "-c", "from chatterbox.mtl_tts import ChatterboxMultilingualTTS as M; "
                                      "M.from_pretrained(device='cpu').generate('Ok.', language_id='en')"],
                       check=True, timeout=1800, stdout=sys.stderr, env=env)
        (home / "ready").write_text(CHATTERBOX + ("\ncuda\n" if gpu else "\ncpu\n"), encoding="utf-8")
        event("ready", text=chatterbox_message("ready"))


def main():
    def terminate(signum, frame):
        raise SystemExit(128 + signum)
    signal.signal(signal.SIGTERM, terminate)
    parser = argparse.ArgumentParser()
    parser.add_argument("mode", choices=["setup", "listen", "transcribe", "tts", "chatterbox"])
    parser.add_argument("--chatterbox-device", choices=["auto", "cpu", "cuda"], default="auto")
    parser.add_argument("--voices", default="", help="tts: Piper voices to install, comma separated")
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
        elif args.mode == "tts":
            setup_tts(args.root, [v for v in args.voices.split(",") if v])
        elif args.mode == "chatterbox":
            setup_chatterbox(args.root, args.chatterbox_device)
        else:
            recognize(args.root / model, args.wav if args.mode == "transcribe" else None, args.device)
    except Exception as error:
        event("error", text=str(error))
        return 1
    return 0

if __name__ == "__main__":
    sys.exit(main())
