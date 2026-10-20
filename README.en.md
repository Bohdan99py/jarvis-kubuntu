# Jarvis for Kubuntu · 0.15

A desktop assistant for KDE Plasma with chat, voice, quick commands, plug-in skills and a memory that learns from your conversations and what you do. Qt Quick interface in English and Russian, a systemd user daemon on D-Bus.

[Русский README](README.md) · [Latest release](https://github.com/Bohdan99py/jarvis-kubuntu/releases/latest)

## Install

Download the `.deb` for your Kubuntu/Ubuntu release (24.04 or 26.04, amd64) and open it in Discover, or:

```sh
sudo apt install ./jarvis_0.15.0_ubuntu-26.04_amd64.deb
```

**Meta+J** opens the quick command bar; the tray icon and the KDE menu entry open the main window.

## Autostart and the tray

Start Jarvis once from the menu after installing: from then on it starts in the tray at every login and keeps running. The user unit `jarvis-tray.service` (systemd, with the Plasma graphical session) restarts it after a crash; sessions without systemd use `~/.config/autostart/jarvis-tray.desktop`. Closing the window only hides it (a lesson keeps recording); "Quit" in the tray menu ends it. At logout Jarvis exits cleanly and saves a recording in progress. Turn it off in Control center → Application.

## Language

Control center → Application:

- **Interface language**: System, Русский or English. Applies immediately. "System" means Russian for ru/uk/be locales, English otherwise.
- **Answer language**: same as your message (default), or always Russian/English. Local answers, memory commands and Claude follow it.

Voice recognition uses the Vosk model for the interface language (`vosk-model-small-en-us-0.15` or `vosk-model-small-ru-0.22`, installed with the "Install voice" button). Spoken answers pick the voice by the answer's own language.

## Voice

Control center → Voice: "Install the natural voice" sets up [Piper](https://github.com/rhasspy/piper), a neural voice that sounds like a person and runs on this computer without the internet (about 200 MB plus ~63 MB per voice, checked against the Piper catalog). Pick a Russian, English and French voice (the French one also reads the French studies), listen and set the pace. Code, links and tables are not read out. Without Piper, Speech Dispatcher (espeak-ng) speaks. Dictation uses Vosk, also locally.

## Memory that learns

Stored in `${XDG_DATA_HOME:-~/.local/share}/jarvis/` (folder 0700, files 0600) and shown in the right-hand panel.

- **Taught examples** — question → answer. Exact matches answer offline; similar ones (shared words weighted by rarity) are given to Claude as hints. Up to 2000; each counts how often it helped, and a full memory drops the least used one. Teach in the panel or in chat: "no, the correct answer is …" corrects the previous answer, "remember this answer" saves it.
- **Facts about you** ("About me") — learned from phrases like "my name is …", "I use Kate", "I'm working on …", from "remember that …", and from notes Claude marks with `<memory>` tags (hidden from the reply). Claude sees these facts; delete wrong ones with ×, "forget …" or "Forget all". Text that looks like a password, API key or card number is never stored.
- **Automatic memory** — with a Claude key, after 90 seconds of silence Jarvis sends the last few turns (up to 12) in a separate request and keeps the durable facts it finds and a short summary of the conversation (at most every 10 minutes).
- **Conversation summaries** (up to 150) — later questions bring related earlier conversations, and the latest one if recent, into Claude's context. "What did we talk about" answers locally; the last three are in the About me tab.
- **What matters now** — Claude gets core facts first (name, work, project, goal, studies), then the ones related to the question, then the strongest and freshest, so a long memory still fits.
- **Contradictions** — the newer statement wins: "I don't like jazz" replaces "likes jazz", "I no longer use Kate" removes it. Activity adds apps you used for more than 3 hours and your active hours.
- **Curiosity** — now and then (after a few messages, at most every 10 minutes, each question at most monthly) Jarvis asks about what it doesn't know yet: your name, your project, what you do in an app you spend hours in, a topic you keep mentioning, your goal. The next message, unless it is a question or a request, is taken as the answer. "Ask me in chat" in the About me tab asks right away; turn it off in Control center → Memory.
- **Conversation topics** — counts of meaningful words in your messages (messages themselves are not stored).

The graph shows up to 48 words: teal from examples, orange from facts, violet from topics. Words that come up together form clusters. Each word shows its two strongest links by default; "All links" shows the rest. Click a word to highlight its links and list its neighbours in a card below, each one a step further. The wheel zooms, dragging pans (nodes can be dragged too), a double-click zooms in on a word or shows everything; search and a filter by kind (click the legend) are at the top.

## Reminders, tasks and notifications

Write the way you speak — in chat, in the quick bar or in the Tasks tab of the memory panel (repetitions: every day, on weekdays, on weekends, on Mondays and Wednesdays, every 2 hours, every 3 days, every 5th of the month, every year; "15 minutes before"; "move the reminder about the call to 5 pm"): "remind me tomorrow at 9 to call mum", "in 20 minutes check the build", "on Friday at 10am standup", "every weekday at 8:30 exercise", "every Monday at 10 plan the week", "add a task buy solder", "my reminders", "cancel the reminder about the standup". Times are read on this computer; what the rules miss ("after lunch") goes to Claude when a key is set, otherwise Jarvis asks "when?". Claude sees your open tasks and sets reminders you ask for in free form; each one is confirmed with a ⏰ line under the answer.

Reminders arrive as KDE notifications from the background daemon — even with the window closed — with "Snooze 10 min" and "Done" buttons; reminders missed while the computer was off come at the next start. Plasma's "Do not disturb" applies.

**Jarvis speaks up by itself** (Control center → Autonomy), at most every half hour, never 23:00–7:00 or during games and videos: a break after 1–2 hours without a pause (needs activity tracking), French flashcards due for review, a curious question when you have not talked for a while, and a morning plan. **Daily memory review** (with a Claude key): once a day Claude merges duplicate facts, drops outdated ones and notes goals and habits; facts you added yourself are never deleted. While hidden, the app also tells you when an answer, a transcription, a PDF translation or the voice setup is ready, and checks for new releases twice a day.

## Skills, calculators and formulas

Skills are picture tiles in Control center → Skills: Electronics and circuits, Physics, Embedded systems, Programming, plus conversation, philosophy, games and writing. Open a tile for **calculators** that work offline and update as you type (Ohm's law, LED resistor, voltage divider, series/parallel, Kirchhoff with two sources, RC, reactance and resonance, capacitor energy, resistor colour code, NE555, op-amp, wire, battery, decibels, UART baud, timer PSC/ARR, ADC, I2C pull-ups, number bases, motion, energy, heat, Coulomb, pendulum), about a hundred **formulas**, a base of ready **answers** given instantly without the internet, and the **instructions** Claude receives. Calculations also work in chat: "divider 12V 10k 4.7k", "LED resistor 5V 20mA", "U=5 R=220", "parallel 10k 10k", "colour code 4.7k", "uart 72MHz 115200", "0x3F to binary", "calc 2*pi*50*100n". Skill JSON may add `image`, `color`, `tools` (built-in calculator ids), `formulas` and per-example `aliases`.

## Files, code and schematics

The paperclip (or drag and drop) attaches up to 8 files to a question: images, PDF datasheets, source code and text, KiCad schematics (`.kicad_sch` — with `kicad-cli` Jarvis sends the full netlist of all sheets and a PDF of the drawing), boards (`.kicad_pcb`), netlists (`.net`) and SimulIDE circuits (`.sim1`). Schematics reach Claude as parts and nets, so it checks connections net by net; without a key Jarvis still lists them. Code in answers gets its own blocks with syntax colours, Copy and Save as file (into `~/Documents/Jarvis/Files`).

## Embedded systems specialist

The "Embedded systems" skill (on by default) makes Jarvis an engineer and mentor: microcontrollers (STM32, AVR/Arduino, ESP32, RP2040…), C/C++ firmware (registers, interrupts, DMA, timers, linker scripts), RTOS, buses (UART, SPI, I2C, CAN, USB), schematics and electronics (pull-ups, decoupling, power, MOSFET switches, op-amps, protection, datasheets, PCB basics) and debugging (SWD/JTAG, OpenOCD, GDB, scope, logic analyzer). Common questions are answered locally, offline. The VS Code assistant knows firmware too.

## French studies: lessons, translation, flashcards

The **Français** button in the window header (or menu → French studies) opens the study window.

- **Lessons** — record the microphone to a 16 kHz mono WAV in `Documents/Jarvis/French/recordings`. With "Transcribe while recording", French phrases appear a few seconds after they are spoken and, with a Claude key, their Russian translation right next to them (optionally read aloud). The live text is a draft; Transcribe recognizes the whole file more accurately afterwards. Import mp3, m4a, ogg, video and more. Replay any phrase, select words and add them to flashcards.
- **Notes and words** — Claude writes Russian lesson notes (topic, grammar with examples, useful phrases, homework) and adds 8–20 key words to the flashcards.
- **Translator** — French → Russian (Russian → French), typed or spoken ("Speak French"); 🔊 reads it in a French or Russian voice.
- **PDF** — text from `pdftotext` (scans through Tesseract when `tesseract-ocr-fra` is installed), translated by Claude paragraph by paragraph into a new PDF (bilingual or Russian only) and a `.txt` in `Documents/Jarvis/French/pdf`.
- **Flashcards** with spaced repetition (simplified SM-2): Again / Hard / Good / Easy (keys 1–4, Space shows the answer). Claude fills in translations. The deck lives in `~/.local/share/jarvis/vocabulary.json`; Jarvis knows you study French and which words are hard for you, and can quiz you in chat.

Recognition is Whisper (faster-whisper) on this computer; audio is never uploaded. Setup → Install recognition downloads the program from PyPI (about 450 MB) and a model from Hugging Face into `~/.local/share/jarvis/study`: by default **large-v3-turbo** (~1.6 GB), best for technical French and several times faster than real time, with small (~480 MB) next to it for live phrases.

**Technical French.** "Subject of the lessons" (Setup tab) lists French terms Whisper expects when transcribing a recording — embedded systems, electronics and programming by default — so jargon is spelled right; add your courses' words. A filter drops loops and echoed terms. Claude translates with standard Russian engineering terminology, keeps code, register names and formulas, and fixes misrecognized terms from context. Recording works without it. Only text (transcripts, phrases, PDF text) goes to Anthropic, for translation and notes.

## Activity: Jarvis sees what you do

Off by default. Turn it on in Control center → Memory or in the Activity tab.

- The daemon loads a small KWin script (Plasma 5/6, X11/Wayland) that reports the focused window: application class, desktop file and title. It is unloaded when tracking is turned off or the daemon stops.
- Jarvis counts time per app and category, hours of the day, app switches and recent windows. Nothing accrues while the screen is locked or after 15 minutes without window or title changes. Daily data is kept for 14 days.
- Window titles are kept only with a separate permission; private browsing and password manager windows are skipped.
- Your Jarvis actions (quick buttons, launches, web searches without the query) feed suggestions in the quick bar: ★ actions and ▶ apps you usually use at this hour.
- Ask "what am I doing" or "what did I do today" — answered locally.
- "Tell Claude what I'm doing" (separate switch) adds the focused app and today's summary to Claude requests.
- If the service starts before Plasma (typical at login), the daemon waits for KWin and connects by itself, and reloads the script when KWin restarts. Window reports are accepted only from the owner of `org.kde.KWin`.

## Programming and VS Code

The extension has its own side bar (all actions, connection and Claude status, recent answers), a menu on the status bar or Ctrl+Alt+J, a "Jarvis" submenu in the editor's context menu, a light-bulb action on errors, Review this file and Add comments, a Compare view before replacing code, and a Get Started walkthrough.

The Code tab → "Install the VS Code extension" installs the Jarvis extension (`/usr/share/jarvis/vscode/jarvis-vscode.vsix`). After "Developer: Reload Window" Jarvis appears in the VS Code status bar.

- Commands (palette → "Jarvis", editor context menu, `Ctrl+Alt+J`): ask, explain, fix, write tests, explain errors here, teach Jarvis a solution. Answers open beside the editor; code can be inserted or replace the selection in one click.
- **Lessons**: Claude's fixes and error explanations, answers rated 👍 and snippets you teach are kept. A similar error later (compared without names and numbers) gets the lesson as a proven solution — or, without a Claude key, as the answer. 👎 drops the lesson.
- **What Jarvis learns by itself** (`jarvis.learnFromCoding`): time per language and project, frameworks from project files, frequent compiler errors. Languages, frameworks and the project become facts in "About me".
- Code goes to Claude only when you run a command. The socket `$XDG_RUNTIME_DIR/jarvis-ide.sock` is owner-only and the daemon checks the peer UID.

## More self-learning

- 👍 under a chat answer saves it as an example (works offline); 👎 makes Jarvis ask for the right answer and remember your reply.
- "What did you learn today" lists today's new facts and lessons.
- Weak automatically learned facts that nothing confirmed for 90 days fade away.

## Security

- `jarvis.service` runs in a systemd sandbox: system and home are read-only except `~/.local/share/jarvis` and the runtime directory; no capabilities, `NoNewPrivileges`, `MemoryDenyWriteExecute`, restricted namespaces and socket families.
- Memory refuses passwords, keys, card numbers, markup and instruction-like phrases, because facts go back into Claude's system prompt; window titles are stripped of markup.
- Updates: the package URL is pinned to this repository; size, the GitHub SHA-256 and `.deb` metadata are checked; installation goes through Discover with confirmation.

## Development

```sh
sudo apt install build-essential cmake ninja-build dpkg-dev qt6-base-dev qt6-declarative-dev qt6-declarative-dev-tools qt6-l10n-tools \
  qml6-module-qtquick qml6-module-qtquick-controls qml6-module-qtquick-layouts qml6-module-qtquick-templates \
  qml6-module-qtquick-window qml6-module-qtquick-dialogs qml6-module-qtqml-workerscript qml6-module-qt-labs-platform \
  python3 python3-venv python3-speechd alsa-utils poppler-utils speech-dispatcher speech-dispatcher-espeak-ng espeak-ng
cmake -S . -B build -G Ninja -DCMAKE_BUILD_TYPE=Release -DCMAKE_INSTALL_PREFIX=/usr
cmake --build build -j 4
ctest --test-dir build --output-on-failure
JARVIS_TEST_BUILD="$PWD/build" dbus-run-session --config-file=tests/test-bus.conf -- python3 tests/integration.py
```

Source strings are English; the Russian translation lives in `i18n/jarvis_ru.ts`. After changing strings run `/usr/lib/qt6/bin/lupdate -locations none -no-obsolete *.cpp *.h *.qml -ts i18n/jarvis_ru.ts` and translate the new messages (the `translations` test checks completeness). To run a development daemon next to an installed one: `JARVIS_DBUS_SERVICE=org.jarvis.DaemonDev ./build/jarvisd`.
