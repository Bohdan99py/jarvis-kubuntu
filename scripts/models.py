#!/usr/bin/env python3
"""Local models for Jarvis's offline agent (0.22).

  models.py list                 the catalogue with sizes and what is installed (JSON)
  models.py status [ID]          the state of one model or all: missing, partial, unverified, ready, corrupt
  models.py download ID --yes    download once (resumes a partial file), then verify the sha256
  models.py verify ID            check the sha256 of an installed file and record it
  models.py remove ID            delete the file (and a partial download)

Nothing is downloaded without `download ID --yes`: the size and the purpose are
printed first. The weights go to ~/.local/share/jarvis/models and are never
part of the Jarvis package. After `verify` succeeds the model works offline.
Progress is printed as JSON lines: {"event": "progress", "done": …, "total": …}.
"""
import hashlib
import json
import os
import sys
import time
import urllib.request

UA = "Jarvis/0.22.0 (+https://github.com/Bohdan99py/jarvis-kubuntu)"
CHUNK = 1 << 20


def catalogue_path():
    here = os.path.dirname(os.path.abspath(__file__))
    for candidate in (os.path.join(here, "..", "models.json"), "/usr/share/jarvis/models.json"):
        if os.path.exists(candidate):
            return candidate
    raise SystemExit("models.json not found")


def catalogue():
    with open(catalogue_path(), encoding="utf-8") as f:
        return {m["id"]: m for m in json.load(f)["models"]}


def models_dir():
    base = os.environ.get("XDG_DATA_HOME") or os.path.join(os.path.expanduser("~"), ".local", "share")
    return os.path.join(base, "jarvis", "models")


def emit(**event):
    print(json.dumps(event, ensure_ascii=False), flush=True)


def verified_path():
    return os.path.join(models_dir(), "verified.json")


def read_verified():
    try:
        with open(verified_path(), encoding="utf-8") as f:
            return json.load(f)
    except (OSError, ValueError):
        return {}


def write_verified(data):
    os.makedirs(models_dir(), mode=0o700, exist_ok=True)
    tmp = verified_path() + ".tmp"
    with open(tmp, "w", encoding="utf-8") as f:
        json.dump(data, f, indent=1)
    os.chmod(tmp, 0o600)
    os.replace(tmp, verified_path())


def state(m):
    path = os.path.join(models_dir(), os.path.basename(m["file"]))
    if not os.path.exists(path):
        return "partial" if os.path.exists(path + ".part") else "missing"
    st = os.stat(path)
    if st.st_size != m["size"]:
        return "corrupt"
    v = read_verified().get(os.path.basename(m["file"]), {})
    if v.get("sha256") == m["sha256"] and v.get("size") == st.st_size and v.get("mtime") == int(st.st_mtime):
        return "ready"
    return "unverified"


def sha256_of(path, total):
    h = hashlib.sha256()
    done = 0
    last = 0.0
    with open(path, "rb") as f:
        while True:
            block = f.read(CHUNK * 4)
            if not block:
                break
            h.update(block)
            done += len(block)
            if time.monotonic() - last > 0.5:
                emit(event="verifying", done=done, total=total)
                last = time.monotonic()
    return h.hexdigest()


def verify(m):
    path = os.path.join(models_dir(), os.path.basename(m["file"]))
    if not os.path.exists(path):
        emit(event="error", text="not downloaded")
        return 1
    st = os.stat(path)
    if st.st_size != m["size"]:
        emit(event="error", text=f"wrong size: {st.st_size} bytes instead of {m['size']} — download it again")
        return 1
    digest = sha256_of(path, st.st_size)
    if digest != m["sha256"]:
        emit(event="error", text="the checksum does not match — the file is damaged; remove it and download again")
        return 1
    data = read_verified()
    data[os.path.basename(m["file"])] = {"sha256": digest, "size": st.st_size, "mtime": int(st.st_mtime), "verified": int(time.time())}
    write_verified(data)
    emit(event="ready", id=m["id"])
    return 0


def download(m):
    os.makedirs(models_dir(), mode=0o700, exist_ok=True)
    path = os.path.join(models_dir(), os.path.basename(m["file"]))
    part = path + ".part"
    if state(m) == "ready":
        emit(event="ready", id=m["id"])
        return 0
    if os.path.exists(path) and os.path.getsize(path) == m["size"]:
        return verify(m)
    free = os.statvfs(models_dir())
    have = os.path.getsize(part) if os.path.exists(part) else 0
    if free.f_bavail * free.f_frsize < (m["size"] - have) + 512 * 1024 * 1024:
        emit(event="error", text="not enough free disk space")
        return 1
    if not m["url"].startswith("https://"):
        emit(event="error", text="refusing a non-HTTPS download")
        return 1
    request = urllib.request.Request(m["url"], headers={"User-Agent": UA})
    if have:
        request.add_header("Range", f"bytes={have}-")
    emit(event="start", id=m["id"], total=m["size"], resumed=have)
    try:
        with urllib.request.urlopen(request, timeout=30) as response:
            if not response.url.startswith("https://"):
                emit(event="error", text="redirected to a non-HTTPS address")
                return 1
            if have and response.status != 206:
                have = 0  # the server ignored the range: start over
            mode = "ab" if have else "wb"
            done = have
            last = 0.0
            with open(part, mode) as f:
                while True:
                    block = response.read(CHUNK)
                    if not block:
                        break
                    f.write(block)
                    done += len(block)
                    if done > m["size"]:
                        emit(event="error", text="the download is larger than expected")
                        return 1
                    if time.monotonic() - last > 0.5:
                        emit(event="progress", done=done, total=m["size"])
                        last = time.monotonic()
    except OSError as e:
        emit(event="error", text=f"download interrupted ({e}); run it again to resume")
        return 1
    if os.path.getsize(part) != m["size"]:
        emit(event="error", text="the download is incomplete; run it again to resume")
        return 1
    os.replace(part, path)
    return verify(m)


def main(argv):
    if len(argv) < 2:
        print(__doc__)
        return 2
    command = argv[1]
    models = catalogue()
    if command == "list":
        for m in models.values():
            emit(id=m["id"], name=m["name"], size=m["size"], license=m["license"], state=state(m), recommended=m.get("recommended", False))
        return 0
    if command == "status":
        ids = argv[2:] or list(models)
        for i in ids:
            if i in models:
                emit(id=i, state=state(models[i]))
        return 0
    if len(argv) < 3 or argv[2] not in models:
        emit(event="error", text="unknown model; see `models.py list`")
        return 2
    m = models[argv[2]]
    if command == "download":
        if "--yes" not in argv:
            print(f"{m['name']}: {m['size'] / 1e9:.2f} GB from {m['url']}\n"
                  f"License: {m['license']}. Purpose: the offline agent understands free wording and plans actions.\n"
                  f"Saved to {models_dir()}. Run again with --yes to download.")
            return 3
        return download(m)
    if command == "verify":
        return verify(m)
    if command == "remove":
        path = os.path.join(models_dir(), os.path.basename(m["file"]))
        for p in (path, path + ".part"):
            if os.path.exists(p):
                os.remove(p)
        data = read_verified()
        data.pop(os.path.basename(m["file"]), None)
        write_verified(data)
        emit(event="removed", id=m["id"])
        return 0
    emit(event="error", text="unknown command")
    return 2


if __name__ == "__main__":
    sys.exit(main(sys.argv))
