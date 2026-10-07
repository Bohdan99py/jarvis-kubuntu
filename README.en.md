# Jarvis for Kubuntu · 0.8

A desktop assistant for KDE Plasma with chat, voice, quick commands, plug-in skills and a memory that learns from your conversations and what you do. Qt Quick interface in English and Russian, a systemd user daemon on D-Bus.

[Русский README](README.md) · [Latest release](https://github.com/Bohdan99py/jarvis-kubuntu/releases/latest)

## Install

Download the `.deb` for your Kubuntu/Ubuntu release (24.04 or 26.04, amd64) and open it in Discover, or:

```sh
sudo apt install ./jarvis_0.8.0_ubuntu-26.04_amd64.deb
```

**Meta+J** opens the quick command bar; the tray icon and the KDE menu entry open the main window.

## Language

Control center → Application:

- **Interface language**: System, Русский or English. Applies immediately. "System" means Russian for ru/uk/be locales, English otherwise.
- **Answer language**: same as your message (default), or always Russian/English. Local answers, memory commands and Claude follow it.

Voice recognition uses the Vosk model for the interface language (`vosk-model-small-en-us-0.15` or `vosk-model-small-ru-0.22`, installed with the "Install voice" button). Spoken answers pick the voice by the answer's own language.

## Memory that learns

Stored in `${XDG_DATA_HOME:-~/.local/share}/jarvis/` (folder 0700, files 0600) and shown in the right-hand panel.

- **Taught examples** — question → answer. Exact matches answer offline; similar ones are given to Claude as hints. Teach in the panel or in chat: "no, the correct answer is …" corrects the previous answer, "remember this answer" saves it.
- **Facts about you** ("About me") — learned from phrases like "my name is …", "I use Kate", "I'm working on …", from "remember that …", and from notes Claude marks with `<memory>` tags (hidden from the reply). Claude sees these facts; delete wrong ones with ×, "forget …" or "Forget all". Text that looks like a password, API key or card number is never stored.
- **Automatic memory** — with a Claude key, after 90 seconds of silence Jarvis sends the last few turns (up to 12) in a separate request and keeps the durable facts it finds (at most every 10 minutes). Activity adds apps you used for more than 3 hours and your active hours.
- **Curiosity** — now and then (after a few messages, at most every 10 minutes, each question at most monthly) Jarvis asks about what it doesn't know yet: your name, your project, what you do in an app you spend hours in, a topic you keep mentioning, your goal. The next message, unless it is a question or a request, is taken as the answer. "Ask me in chat" in the About me tab asks right away; turn it off in Control center → Memory.
- **Conversation topics** — counts of meaningful words in your messages (messages themselves are not stored).

The graph shows up to 48 words: teal from examples, orange from facts, violet from topics.

## Activity: Jarvis sees what you do

Off by default. Turn it on in Control center → Memory or in the Activity tab.

- The daemon loads a small KWin script (Plasma 5/6, X11/Wayland) that reports the focused window: application class, desktop file and title. It is unloaded when tracking is turned off or the daemon stops.
- Jarvis counts time per app and category, hours of the day, app switches and recent windows. Nothing accrues while the screen is locked or after 15 minutes without window or title changes. Daily data is kept for 14 days.
- Window titles are kept only with a separate permission; private browsing and password manager windows are skipped.
- Your Jarvis actions (quick buttons, launches, web searches without the query) feed suggestions in the quick bar: ★ actions and ▶ apps you usually use at this hour.
- Ask "what am I doing" or "what did I do today" — answered locally.
- "Tell Claude what I'm doing" (separate switch) adds the focused app and today's summary to Claude requests.
- If the service starts before Plasma (typical at login), the daemon waits for KWin and connects by itself, and reloads the script when KWin restarts. Window reports are accepted only from the owner of `org.kde.KWin`.

## Security

- `jarvis.service` runs in a systemd sandbox: system and home are read-only except `~/.local/share/jarvis` and the runtime directory; no capabilities, `NoNewPrivileges`, `MemoryDenyWriteExecute`, restricted namespaces and socket families.
- Memory refuses passwords, keys, card numbers, markup and instruction-like phrases, because facts go back into Claude's system prompt; window titles are stripped of markup.
- Updates: the package URL is pinned to this repository; size, the GitHub SHA-256 and `.deb` metadata are checked; installation goes through Discover with confirmation.

## Development

```sh
sudo apt install build-essential cmake ninja-build dpkg-dev qt6-base-dev qt6-declarative-dev qt6-declarative-dev-tools qt6-l10n-tools \
  qml6-module-qtquick qml6-module-qtquick-controls qml6-module-qtquick-layouts qml6-module-qtquick-templates \
  qml6-module-qtquick-window qml6-module-qtquick-dialogs qml6-module-qtqml-workerscript qml6-module-qt-labs-platform \
  python3 python3-venv python3-speechd alsa-utils speech-dispatcher speech-dispatcher-espeak-ng espeak-ng
cmake -S . -B build -G Ninja -DCMAKE_BUILD_TYPE=Release -DCMAKE_INSTALL_PREFIX=/usr
cmake --build build -j 4
ctest --test-dir build --output-on-failure
JARVIS_TEST_BUILD="$PWD/build" dbus-run-session --config-file=tests/test-bus.conf -- python3 tests/integration.py
```

Source strings are English; the Russian translation lives in `i18n/jarvis_ru.ts`. After changing strings run `/usr/lib/qt6/bin/lupdate -locations none -no-obsolete *.cpp *.h *.qml -ts i18n/jarvis_ru.ts` and translate the new messages (the `translations` test checks completeness). To run a development daemon next to an installed one: `JARVIS_DBUS_SERVICE=org.jarvis.DaemonDev ./build/jarvisd`.
