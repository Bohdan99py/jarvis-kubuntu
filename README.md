# J.A.R.V.I.S. for Kubuntu (v0.3: daemon)

Everything lives in one flat folder with a single CMakeLists.txt.

## Processes

    jarvisd  — background daemon (systemd user service), owns the brain, D-Bus org.jarvis.Daemon1
    jarvis   — Qt Quick chat window; talks to jarvisd, falls back to a local engine if it is absent

## Files

| File(s)                                        | Role                                              |
|------------------------------------------------|---------------------------------------------------|
| jv_sys.h / jv_sys.c                            | pure C11 layer: reads /proc and /sys              |
| jv_sysinfo_cli.c                               | CLI to test the C layer on its own                |
| system_info.h/.cpp                             | Qt adapter: C data -> RU/EN sentences             |
| chat_engine.h/.cpp                             | rule-based brain (request ids, async replies)     |
| dbus_names.h                                   | D-Bus names shared by daemon and GUI              |
| daemon_service.h/.cpp, jarvisd_main.cpp        | the daemon                                        |
| daemon_client.h/.cpp                           | GUI-side D-Bus proxy                              |
| chat_model.h/.cpp, main.cpp, Main.qml          | chat window                                       |
| jarvis.service.in, org.jarvis.Daemon1.service.in | systemd unit and D-Bus activation templates     |
| org.jarvis.Jarvis.desktop                      | KDE launcher entry                                |

## Dependencies

    sudo apt install build-essential cmake ninja-build \
        qt6-base-dev qt6-declarative-dev qt6-declarative-dev-tools \
        qml6-module-qtquick qml6-module-qtquick-controls \
        qml6-module-qtquick-layouts qml6-module-qtquick-templates \
        qml6-module-qtquick-window qml6-module-qtqml-workerscript

## Build

    cmake -S . -B build -G Ninja
    cmake --build build

## Try the daemon without installing

    # terminal 1
    ./build/jarvisd
    # terminal 2
    ./build/jarvis        # header says "jarvisd подключён"

    # talk to it straight over D-Bus
    busctl --user introspect org.jarvis.Daemon1 /org/jarvis/Daemon1
    busctl --user call org.jarvis.Daemon1 /org/jarvis/Daemon1 org.jarvis.Daemon1 Version

## Install as a user service (no sudo)

    cmake --install build --prefix ~/.local
    systemctl --user daemon-reload
    systemctl --user enable --now jarvis.service

    systemctl --user status jarvis.service
    journalctl --user -u jarvis.service -f      # daemon log
    systemctl --user stop jarvis.service        # stop it

Once installed, opening the GUI wakes the daemon by itself (D-Bus activation).
