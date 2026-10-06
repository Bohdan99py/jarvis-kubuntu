#!/usr/bin/env python3
"""Download the correct public GitHub release asset and verify its SHA-256 digest."""
import argparse
import hashlib
import json
import os
from pathlib import Path
import re
import subprocess
import sys
import tempfile
import urllib.request

REPOSITORY = "Bohdan99py/jarvis-kubuntu"
MAX_PACKAGE = 128 * 1024 * 1024

def emit(kind, **kwargs):
    print(json.dumps(dict(event=kind, **kwargs), ensure_ascii=False), flush=True)

def request(url):
    return urllib.request.Request(url, headers={"User-Agent": "Jarvis-Updater/0.6", "Accept": "application/vnd.github+json"})

def version(value):
    match = re.fullmatch(r"v?(\d+)\.(\d+)\.(\d+)", value)
    if not match:
        raise ValueError("Релиз должен иметь версию vMAJOR.MINOR.PATCH")
    return tuple(map(int, match.groups()))

def platform_tag(path=Path("/etc/os-release")):
    fields = {}
    for line in path.read_text().splitlines():
        if "=" in line:
            key, value = line.split("=", 1)
            fields[key] = value.strip('"')
    if fields.get("ID") != "ubuntu" or fields.get("VERSION_ID") not in ("24.04", "26.04"):
        raise ValueError("Автообновление подготовлено для Kubuntu/Ubuntu 24.04 и 26.04")
    return "ubuntu-" + fields["VERSION_ID"]

def select_asset(release, current, platform, arch):
    if release.get("draft") or release.get("prerelease"):
        raise ValueError("Ожидался стабильный опубликованный релиз")
    tag = release.get("tag_name", "")
    if version(tag) <= version(current):
        return None
    wanted = f"jarvis_{tag.removeprefix('v')}_{platform}_{arch}.deb"
    for asset in release.get("assets", []):
        if asset.get("name") != wanted:
            continue
        url = asset.get("browser_download_url", "")
        prefix = f"https://github.com/{REPOSITORY}/releases/download/{tag}/"
        if url != prefix + wanted:
            raise ValueError("Адрес пакета не соответствует репозиторию")
        digest = asset.get("digest", "")
        if not re.fullmatch(r"sha256:[0-9a-f]{64}", digest):
            raise ValueError("GitHub не предоставил SHA-256 пакета. Установка отменена.")
        if not 0 < asset.get("size", 0) <= MAX_PACKAGE:
            raise ValueError("Некорректный размер пакета")
        return asset
    raise ValueError(f"В релизе {tag} нет пакета для {platform}/{arch}")

def download(asset, cache):
    cache.mkdir(parents=True, exist_ok=True, mode=0o700)
    target = cache / asset["name"]
    descriptor, temporary = tempfile.mkstemp(prefix="download-", dir=cache)
    try:
        digest = hashlib.sha256()
        total = 0
        with os.fdopen(descriptor, "wb") as out, urllib.request.urlopen(request(asset["browser_download_url"]), timeout=60) as response:
            if not response.url.startswith("https://"):
                raise ValueError("Загрузка требует HTTPS")
            while chunk := response.read(1024 * 1024):
                total += len(chunk)
                if total > MAX_PACKAGE or total > asset["size"]:
                    raise ValueError("Размер загрузки превышает ожидаемый")
                digest.update(chunk)
                out.write(chunk)
        if total != asset["size"] or "sha256:" + digest.hexdigest() != asset["digest"]:
            raise ValueError("Контрольная сумма пакета не совпала")
        os.replace(temporary, target)
        return target
    finally:
        if os.path.exists(temporary):
            os.unlink(temporary)

def main():
    parser = argparse.ArgumentParser()
    parser.add_argument("--current", required=True)
    parser.add_argument("--cache", type=Path, required=True)
    args = parser.parse_args()
    try:
        emit("status", text="Проверка последнего релиза GitHub…")
        with urllib.request.urlopen(request(f"https://api.github.com/repos/{REPOSITORY}/releases/latest"), timeout=20) as response:
            raw = response.read(1024 * 1024 + 1)
            if len(raw) > 1024 * 1024:
                raise ValueError("Ответ GitHub слишком большой")
            release = json.loads(raw)
        arch = subprocess.check_output(["dpkg", "--print-architecture"], text=True, timeout=5).strip()
        asset = select_asset(release, args.current, platform_tag(), arch)
        if asset is None:
            emit("current", text="Установлена актуальная версия.")
            return 0
        emit("status", text="Загрузка и проверка пакета…")
        target = download(asset, args.cache)
        # Check archive metadata before handing it to the system package installer.
        for field, expected in (("Package", "jarvis"), ("Architecture", arch), ("Version", release["tag_name"].removeprefix("v"))):
            actual = subprocess.check_output(["dpkg-deb", "--field", str(target), field], text=True, timeout=10).strip()
            if actual != expected:
                target.unlink()
                raise ValueError("Метаданные пакета не соответствуют релизу")
        emit("package", path=str(target), text="Пакет проверен. Подтвердите установку в Discover.")
        return 0
    except Exception as error:
        emit("error", text=str(error))
        return 1

if __name__ == "__main__":
    sys.exit(main())
