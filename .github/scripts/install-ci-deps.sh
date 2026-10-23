#!/usr/bin/env bash
# Installs what CI needs to build, test and package Jarvis on Ubuntu 24.04 and 26.04.
#
# Hosted runners fetch from azure.archive.ubuntu.com, which sometimes stalls: apt
# prints "Ign:" and, with its default 120 s timeouts, spends minutes per package.
# So: short network timeouts, bounded retries, downloads separated from the
# install (a download can be killed safely, dpkg cannot), the main Ubuntu
# archive when the Azure mirror keeps failing, and .debs reused from the cache.
#
# Usage: install-ci-deps.sh <directory for downloaded .debs>
set -euo pipefail

archives=${1:?directory for downloaded packages}
export DEBIAN_FRONTEND=noninteractive

sudo tee /etc/apt/apt.conf.d/90jarvis-ci >/dev/null <<'EOF'
Acquire::Retries "3";
Acquire::http::Timeout "20";
Acquire::https::Timeout "20";
APT::Install-Recommends "false";
EOF
# man-db re-indexes manual pages after every install; nobody reads them in CI.
sudo rm -f /var/lib/man-db/auto-update

# Leave the Azure mirror for the main archive (both apt source formats and the
# runner's mirror list, whichever this image uses).
use_main_archive() {
    echo "::warning::APT kept failing on azure.archive.ubuntu.com; switching to archive.ubuntu.com"
    sudo sed -i 's|azure\.archive\.ubuntu\.com|archive.ubuntu.com|g' /etc/apt/apt-mirrors.txt \
        /etc/apt/sources.list /etc/apt/sources.list.d/*.list /etc/apt/sources.list.d/*.sources 2>/dev/null || true
}

# Runs a network step at most three times, each bounded in time.
fetch() {
    local attempt
    for attempt in 1 2 3; do
        if timeout 300 "$@"; then
            return 0
        fi
        echo "::warning::'$*' failed (attempt $attempt of 3)"
        [ "$attempt" -eq 3 ] && break
        [ "$attempt" -eq 1 ] && use_main_archive
        sleep $((attempt * 10))
    done
    return 1
}

fetch sudo apt-get update

# The SVG image plugin: its own package on 26.04, part of libqt6svg6 on 24.04.
svg=libqt6svg6
apt-cache show qt6-svg-plugins >/dev/null 2>&1 && svg=qt6-svg-plugins

packages=(
    # Build: compiler, CMake + Ninja, Qt with qmlcachegen, qsb (HUD shaders), lrelease/lupdate.
    build-essential cmake ninja-build
    qt6-base-dev qt6-declarative-dev qt6-declarative-dev-tools qt6-shadertools-dev qt6-l10n-tools
    # Package: dpkg-shlibdeps for the .deb dependencies.
    dpkg-dev
    # QML smoke test: every QML module the windows import, and the SVG icons.
    qml6-module-qtquick qml6-module-qtquick-controls qml6-module-qtquick-layouts
    qml6-module-qtquick-templates qml6-module-qtquick-window qml6-module-qtquick-dialogs
    qml6-module-qtqml-workerscript qml6-module-qt-labs-platform
    "$svg"
    # Integration tests: a private session bus, gdbus calls, fake KWin and notification
    # services in Python (GObject), and desktop-file-validate.
    dbus libglib2.0-bin python3 python3-gi gir1.2-glib-2.0 desktop-file-utils
)
# Speech Dispatcher, espeak-ng, ALSA and python3-venv stay runtime dependencies of
# the package (packaging/CPackConfig.cmake) but no CI step uses them: the tests
# speak through a fake spd-say.

mkdir -p "$archives/partial"
# The cache lives in the runner's home, which apt's _apt sandbox user cannot enter.
apt_cached=(-o "Dir::Cache::Archives=$archives" -o APT::Sandbox::User=root)
fetch sudo apt-get "${apt_cached[@]}" install --download-only -y "${packages[@]}"
sudo apt-get "${apt_cached[@]}" install -y "${packages[@]}"

# Keep only what the next run can reuse, readable by the cache action.
sudo apt-get "${apt_cached[@]}" autoclean -y >/dev/null
sudo rm -rf "$archives/partial" "$archives/lock"
sudo chown -R "$(id -u):$(id -g)" "$archives"
