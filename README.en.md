# Jarvis for Kubuntu · 0.18.0

A desktop assistant for KDE Plasma with chat, voice, quick commands, plug-in skills and a memory that learns from your conversations and what you do. Qt Quick interface in English and Russian, a systemd user daemon on D-Bus.

[Русский README](README.md) · [Latest release](https://github.com/Bohdan99py/jarvis-kubuntu/releases/latest)

## Install

Download the `.deb` for your Kubuntu/Ubuntu release (24.04 or 26.04, amd64) and open it in Discover, or:

```sh
sudo apt install ./jarvis_0.18.0_ubuntu-26.04_amd64.deb
```

**Meta+J** opens the quick command bar; the tray icon and the KDE menu entry open the main window.

## Chat

The chat matches the HUD: holographic cards (your messages amber on the right, Jarvis cyan on the left, errors red, notes from Jarvis a thin centred line). A new message unfolds — a line, the frame, then the text — with a short light pulse; while Jarvis writes, the card glows, a highlight runs along its top edge and its header shows the route (Claude › web search, calculator, memory…). Before the first words arrive, the bottom of the list says what Jarvis is doing. An error has "↻ Retry". The list glides to the newest message, or shows "↓ new messages" while you read above. Markdown, code blocks with Copy/Save, 👍/👎 and streaming work as before.

The input is multi-line (up to six lines): **Enter** sends, **Shift+Enter** starts a new line, **Esc** stops the answer (the "■ Stop" button replaces "Send"; what was written stays, marked "stopped"), **↑** in an empty field brings back the last message, **Ctrl+Shift+M** dictates, **Ctrl+O** attaches files. Its edges react: corner brackets light up with the focus, a microphone level on the left, a scanner along the bottom while Jarvis works, a pulse across the top when a message leaves. The Low quality and software rendering turn the chat effects off.

## Overlay and Ctrl+J

**Ctrl+J** in any application opens the Jarvis overlay above your windows without switching to the main window. It is the same conversation (the same Claude session) as the main window.

- **Compact**: a mini core, the state, a context line ("Context: CLion — programming in C++ (70 %) · Jarvis gets only the application and window title"), the question field and the answer being written right in the panel.
- **Expanded** (Ctrl+E or ⤢): the whole conversation with code blocks.
- **◉** (Ctrl+Shift+S) sends the content of the window you came from with the question (if screen awareness is allowed), ⌂ opens the main window, × or **Esc** closes (while an answer is written, Esc stops it first). The focus returns to the previous window. Tab moves between the buttons.
- The application you came from is kept: the daemon ignores Jarvis's own windows.
- On KDE Wayland the overlay is a layer-shell surface (`qml6-module-org-kde-layershell`): it opens on the active screen and takes the keyboard. Without the module (Ubuntu 24.04) or on X11 it is an always-on-top window.
- The 0.17 mini core (Ctrl+Shift+O, ◱) stays; a click on it opens the panel.

**Enabling Ctrl+J.** The global key is registered through the standard portal `org.freedesktop.portal.GlobalShortcuts` (xdg-desktop-portal-kde). On the first start of 0.18 Plasma asks you to confirm the shortcut — accept it. It then appears in System Settings → Keyboard → Shortcuts (Jarvis), where it can be changed: browsers use Ctrl+J for downloads. If you closed the dialog: Control center → Awareness → Bind Ctrl+J. Without the portal (other desktops) bind the "J.A.R.V.I.S. → Overlay" action or the command `jarvis --overlay` by hand. Jarvis never grabs the keyboard and sees no other key presses.

## Cognitive awareness

Jarvis can understand what you are doing, to answer with context and, rarely, offer help. Nothing is observed until you allow it (Control center → Awareness).

**Levels**: Off; Manual (default) — context only when you ask (the overlay, "what's on my screen"); Assistive — also rare offers on strong evidence; Proactive — also offers for electronics, documentation and studying.

**Separate permissions**:
- **Windows** — the focused application and its title (the same KWin script as activity tracking, but no history is kept). Titles stay in memory; private browsing, password managers, keyrings and authentication prompts are never read; your own list of excluded applications.
- **Screen** — a screenshot of the window you were in, read by Tesseract on this computer. Only when you ask, or, with an interval set (2–15 min), in the background at the Assistive/Proactive levels. While a picture is taken and read, the chat and the overlay show **"◉ READING SCREEN"**; with background analysis on, "◉ SCREEN" stays lit. Screenshots are never saved; in the background the picture never leaves memory; for a question to Claude it is written to `$XDG_RUNTIME_DIR/jarvis-screen` (RAM, 0600) and removed after the answer.
- **Send screenshots to Claude** — separately; without it only the recognised text goes, and only with your question.

**How the screen is captured.** The installed Jarvis uses KDE's `org.kde.KWin.ScreenShot2`, which KWin allows to programs that declare it in their desktop file (`X-KDE-DBUS-Restricted-Interfaces`, like Spectacle), and captures exactly the window you came from even while the overlay has the focus. If KWin refuses (a build from source), a request falls back to the `org.freedesktop.portal.Screenshot` portal, where Plasma asks you. There is no background analysis through the portal.

**Context and confidence.** The activity is inferred from several signals, strongest first: screen text, errors from the VS Code extension, the window title, the application. From the application alone the confidence stays low (≈30–50 %) and Jarvis says so. It recognises programming (language and project from the title), a build that keeps failing, electronics (KiCad, SimulIDE, LTspice…), the French driving theory test, technical documentation, studying programming, browsing and more. Claude always gets an honest note of what Jarvis saw: only the title ("do not describe the window's content") or the text/screenshot.

**Offers of help** (Assistive/Proactive) come only from observed events, never "because time passed": a build error that keeps coming back (the same error in three separate builds within 30 minutes — a diagnostic repeated while typing, or a log left on the screen, counts once); a driving theory test recognised from the title and the screen ("I can translate the question and go through the rule that applies"); electronics after real work on a circuit; documentation when you go back and forth between docs and code or errors pile up; studying after long work on a course.

**Not intrusive**: at most one offer every 45 minutes (configurable), 4 a day, a usefulness score (stricter at Assistive), refusals count ("Not now" — 6 h of silence, doubling each time; "Don't offer this" — a month), categories can be switched off; never at night (23–7), in Plasma's Do Not Disturb, on a locked screen, while you talk to Jarvis, in games and films or fullscreen applications (unless allowed). Pause observation for an hour or until morning from the overlay, the tray or the settings; switches apply at once.

**Memory**: subjects you keep studying become facts at most once a day, Code de la route progress, and the history of offers (accepted/declined) for 30 days in `awareness.json` (0600). Titles and screen text are never written to disk; "Forget observed context and offer history" clears it all.

## Driving, traffic rules & motorsport

The "Driving, traffic rules & motorsport" skill replaced the Games skill (the application-category module that keeps Jarvis quiet during games stays).

- **Driving** (France, category B): car control, manoeuvres and parking, junctions, distances, weather, how a car works (ABS, ESP, gearbox), the practical test.
- **French traffic rules (Code de la route)**: a test question is always answered in four parts — literal translation of the question and options, analysis under French rules, probable answer, confidence and ambiguities; no invented signs; sources Légifrance / Sécurité routière / Service-public. Progress over the 10 ETG themes (L, C, R, U, D, A, P, M, S, E) builds up when you say which answer you chose; Jarvis knows your weak themes. "My driving test progress" in chat or the Progress tab of the skill page; "Quiz me" to practise.
- **Motorsport**: F1, WEC, WRC, GT, history, engines and aerodynamics, telemetry (send a CSV), strategy, flags, marshals, regulations. Results and current regulations are never presented as current without checking.
- Calculators: stopping distance (physics and the exam rules (V/10)×3 and (V/10)²), following distance (2 s, R412-12), lap speed and gap, fuel and pit stops.
- **Fresh data**: Control center → Skills → "Let Claude search the web for current facts" turns on Anthropic's web search (up to 3 searches per answer, billed separately; your organisation must allow it in the Claude Console).

## Cinematic HUD

The main window is a holographic HUD: the chat on the left, the AI core in the middle and live panels around it.

- **The core** is a stack of independent GPU layers (GLSL shaders compiled to `.qsb`): an energy core with plasma, facets and pulsation; eight rings with their own motions (drift, mechanical steps, scanning, segment rebuilds); a sound analyser; data streams; particles (orbiting dust, an inflow while Jarvis works, bursts on events); a background field with waves. Idle motion never loops: ring speeds wander and impulses arrive at random.
- **States**: idle, listening, processing, speaking, notification, warning, error. Transitions are smooth — parameters ease, animations never restart. An error briefly destabilises the core (slip, chromatic split), then it recovers.
- **Real data only.** The core follows real events: a request sent, the route that answered (Claude, memory, calculator, schematics, code, reminders…), the answer stream (characters per second), memory recalls and writes (a network of real memory words lights up), notifications, errors and local fallbacks. While speaking, the analyser and the core follow the level and spectrum of the *audio being played* (the speech server reports levels in step with playback); while listening, the microphone level. When the sound is not measured (the system voice) the analyser draws no made-up spectrum.
- **Panels**: System (CPU per core, RAM, GPU and VRAM via `gpu_busy_percent` or `nvidia-smi`, temperature, HUD frames/s), Assistant, Tasks, Voice, Memory, Event stream, and transient Active request and Notification / Warning / Error panels that grow out of the core (point → line → frame → content) and fold away by themselves. Panels lay out without overlapping; hover highlights, a click on the title expands details, a right click pins, dragging moves, a double click puts it back.
- **Demo** (the DEMO button) plays made-up events to show every effect. They are marked DEMO and never counted.
- **Overlay**: Ctrl+J — a question panel above every window (see "Overlay and Ctrl+J"); the ◱ button, Ctrl+Shift+O or the tray menu — a small always-on-top core, a click on it opens the panel.
- **Quality** (the buttons on top or Control Center → Application → Interface): Low — three rings, no particles, 20 fps; Balanced — every layer except the background haze and the plasma's swirl, 30 fps while idle and 60 when active; Cinematic — every effect, 60 fps. The HUD's rate does not follow the display: on a 144 Hz monitor the window is still redrawn at most 60 times a second. The same menu has "Classic chat without the HUD". The animation stops while the window is minimised or hidden.
- **Shortcuts**: Ctrl+J — overlay, Ctrl+M — memory panel, Ctrl+Shift+O — the mini core above other windows. Clicking the core starts or stops listening (without voice it focuses the input).

For visual checks: `JARVIS_HUD_STATE=processing jarvis` pins a state (`idle`, `listening`, `processing`, `speaking`, `notification`, `warning`, `error`); `JARVIS_HUD_STATE=demo` starts the demo.

### Graphics card and checking the renderer

Jarvis reports what draws it: the System panel lists every GPU, the one that renders marked ◆, and its details show the API and renderer; software rendering (llvmpipe, Qt's software renderer) is shown in red. The same is printed at start (`jarvis: graphics: …` in the terminal or `journalctl --user -u jarvis-tray`) and by:

```sh
jarvis --gpu-info                      # API, renderer, the GPU that draws, every GPU found
jarvis --gpu-info --gpu dedicated      # the same with the dedicated card (not saved)
jarvis --gpu-info --graphics-api vulkan
```

Finding an NVIDIA card does not mean NVIDIA draws: the renderer string decides (`NVIDIA GeForce RTX …`, `AMD Radeon 680M …`, or `llvmpipe` — software).

Choose in Control Center → Application → Interface:
- **Graphics card**: System default (usually the integrated GPU, better for the battery), Integrated or Dedicated. Dedicated uses PRIME render offload for the Jarvis process only — NVIDIA's `__NV_PRIME_RENDER_OFFLOAD`, `__GLX_VENDOR_LIBRARY_NAME`, `__VK_LAYER_NV_optimus`, or `DRI_PRIME=pci-…` of that card for Mesa drivers (AMD, Intel, nouveau). Nothing system-wide changes; with OpenGL the variables are dropped after the first frame, so programs Jarvis opens stay on the default GPU.
- **Graphics API**: Automatic (OpenGL), OpenGL or Vulkan. Vulkan is used only when a driver and a device are found; otherwise Jarvis draws with OpenGL and says why.

The GPU cannot change under a running window, so a "Restart Jarvis" button appears. If a new choice never draws a frame (a driver failure), the next start returns to the system defaults and says so. "Copy graphics diagnostics" copies the report for a bug report.

Checking a hybrid laptop:
1. `jarvis --gpu-info` and `jarvis --gpu-info --gpu dedicated` — `Renderer:` must name the intended GPU, without `SOFTWARE`.
2. In Jarvis open the System panel's details (click its title): HUD frames/s at most 60 (about 30 in Balanced while idle), the Renderer line and the load of the ◆ GPU.
3. Load from outside: `nvidia-smi dmon -s u` for NVIDIA, `amdgpu_top` or `radeontop` for AMD; CPU — `top -H -p $(pidof jarvis)`.
4. Qt's frame times: `QSG_RENDER_TIMING=1 jarvis 2>&1 | grep "frame rendered"`; `blockedForSync` in the `[gui thread]` lines is how long the interface waited for rendering.

## Autostart and the tray

Start Jarvis once from the menu after installing: from then on it starts in the tray at every login and keeps running. The user unit `jarvis-tray.service` (systemd, with the Plasma graphical session) restarts it after a crash; sessions without systemd use `~/.config/autostart/jarvis-tray.desktop`. Closing the window only hides it (a lesson keeps recording); "Quit" in the tray menu ends it. At logout Jarvis exits cleanly and saves a recording in progress. Turn it off in Control center → Application.

## Language

Control center → Application:

- **Interface language**: System, Русский or English. Applies immediately. "System" means Russian for ru/uk/be locales, English otherwise.
- **Answer language**: same as your message (default), or always Russian/English. Local answers, memory commands and Claude follow it.

Voice recognition uses the Vosk model for the interface language (`vosk-model-small-en-us-0.15` or `vosk-model-small-ru-0.22`, installed with the "Install voice" button). Spoken answers pick the voice by the answer's own language.

## Voice

Control center → Voice → **Voice engine**: "Best available" (ElevenLabs → Chatterbox → Piper → system, in that order) or a specific one. If the chosen voice fails (no network, quota used up), the next ready one reads the answer, so Jarvis is never silent; the reason shows in the settings. A new answer interrupts the one still being read. While a local neural voice is in use, a speech server keeps the model loaded instead of loading it for every answer (socket `$XDG_RUNTIME_DIR/jarvis-speech.sock`, owner only).

**ElevenLabs** — studio-quality voices that sound like a real person, in Russian, English, French and dozens of other languages. Paste an API key (elevenlabs.io → Developers → API keys), press "Load my voices", pick a voice (George, a British voice, by default), a model (Multilingual v2 — the most natural, Flash v2.5 — faster and half the price, v3 — the most expressive) and the stability. Audio streams and starts at once. The text of every answer goes to ElevenLabs; the key lives in `~/.local/share/jarvis/voice/speech.json` (0600) or comes from `ELEVENLABS_API_KEY`, and the interface shows only its last characters.

**Chatterbox** (Resemble AI, open model) — a neural voice on this computer, nothing goes to the network. It can speak with the voice of a short recording (5–20 s, WAV/FLAC/MP3/OGG — "Voice sample") and has an expressiveness slider. "Install Chatterbox" puts `chatterbox-tts==0.1.7` with PyTorch into its own environment under `~/.local/share/jarvis/voice/chatterbox` — the compact CPU build without an NVIDIA card, the CUDA one with it — and downloads the model (~3 GB) from Hugging Face there. Fast on a GPU; on a CPU each sentence takes a few seconds.

**Piper**: "Install the natural voice" sets up [Piper](https://github.com/rhasspy/piper), a neural voice that sounds like a person and runs on this computer without the internet (about 200 MB plus ~63 MB per voice, checked against the Piper catalog). Pick a Russian, English and French voice (the French one also reads the French studies), listen and set the pace. Code, links and tables are not read out. Without Piper, Speech Dispatcher (espeak-ng) speaks. Dictation uses Vosk, also locally.

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

Skills are picture tiles in Control center → Skills: Electronics and circuits, Physics, Embedded systems, Programming, Driving, traffic rules & motorsport, plus conversation, philosophy and writing. Open a tile for **calculators** that work offline and update as you type (Ohm's law, LED resistor, voltage divider, series/parallel, Kirchhoff with two sources, RC, reactance and resonance, capacitor energy, resistor colour code, NE555, op-amp, wire, battery, decibels, UART baud, timer PSC/ARR, ADC, I2C pull-ups, number bases, motion, energy, heat, Coulomb, pendulum), about a hundred **formulas**, a base of ready **answers** given instantly without the internet, and the **instructions** Claude receives. Calculations also work in chat: "divider 12V 10k 4.7k", "LED resistor 5V 20mA", "U=5 R=220", "parallel 10k 10k", "colour code 4.7k", "uart 72MHz 115200", "0x3F to binary", "calc 2*pi*50*100n". The Programming skill also converts IEEE 754 floats and doubles to bits and back, Q15/Q16.16 fixed point, IPv4 subnets, colours (HEX/RGB/HSL and RGB565 for MCU displays), text to UTF-8 bytes (a C string and array, Base64), and estimates running time from complexity — in chat too: "192.168.1.10/24", "float 0.1", "0.25 to Q15", "#ff8800 to rgb565", "utf-8 \"Hello\"", "O(n log n) n=1e6"; its answer base grew to 70 (OOP, virtual functions, STL, concurrency, Python, Rust, Go, JavaScript, REST, SQL injection, secure coding, password hashing, algorithms, patterns, Docker, CI, profiling, endianness, floating point…). Skill JSON may add `image`, `color`, `tools` (built-in calculator ids), `formulas`, per-example `aliases` and `schematics` (ids of bundled example circuits: the skill page then shows them and a KiCad tab).

## Files, code and schematics

The paperclip (or drag and drop) attaches up to 8 files to a question: images, PDF datasheets, source code and text, KiCad schematics (`.kicad_sch` — with `kicad-cli` Jarvis sends the full netlist of all sheets and a PDF of the drawing), boards (`.kicad_pcb`), netlists (`.net`) and SimulIDE circuits (`.sim1`). Schematics reach Claude as parts and nets, so it checks connections net by net; without a key Jarvis still lists them. Code in answers gets its own blocks with syntax colours, Copy and Save as file (into `~/Documents/Jarvis/Files`).

## Example circuits and KiCad

The Electronics (and Embedded systems) skill page has two more tabs:

- **Schematics** — 11 classic circuits with working values: LED and resistor, voltage divider, RC filter, NE555 blinker, NPN and MOSFET switches, a non-inverting op-amp, a 3.3 V regulator, a button with pull-up and debounce, an I2C bus, a bridge rectifier with a 7805. Zoom in (Ctrl+wheel), read how it works and its parts and nets, ask Jarvis about it, or save it as SPICE (`.cir` for ngspice), a KiCad netlist (`.net` — PCB editor → File → Import → Netlist), a parts list (`.csv`) or the drawing (`.svg`), into `~/Documents/Jarvis/Schematics`. In chat: "show me an example circuit with a 555".
- **KiCad** — your projects: recent ones from Documents, Projects, KiCad and KiCad's demos, or "Open a project…". Jarvis only reads them: previews of every sheet and the board, ERC and DRC (KiCad 8+) with the findings listed, a parts list (CSV), netlist, schematic PDF, fabrication files (Gerbers of the needed layers, drill, placement), "Open in KiCad". "Ask Jarvis about it" sends the schematic (all nets and a PDF of the drawing) with the check report to the chat. Results go to `~/Documents/Jarvis/KiCad/<project>`. Needs `kicad-cli` (package `kicad`); without it Jarvis still makes a parts list from the schematic file.

The examples are described once in `schematics/generate.py`, which produces the drawing, parts, nets and SPICE deck, so they never disagree.

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
- Claude's answers, imported skills and lesson notes are rendered as safe Markdown: no images of any kind (a remote image could carry data away in its URL) and no HTML; on top of that the interface never loads anything from the network.
- Only the Jarvis daemon may put a message "from Jarvis" into the chat over D-Bus (`org.jarvis.Desktop1.Open`).
- The speech server's socket is 0600 and accepts only the same user's processes; the ElevenLabs key is never passed on a command line.
- Updates: the package URL is pinned to this repository; size, the GitHub SHA-256 and `.deb` metadata are checked; installation goes through Discover with confirmation.

## Development

```sh
sudo apt install build-essential cmake ninja-build dpkg-dev qt6-base-dev qt6-declarative-dev qt6-declarative-dev-tools qt6-l10n-tools \
  qml6-module-qtquick qml6-module-qtquick-controls qml6-module-qtquick-layouts qml6-module-qtquick-templates \
  qml6-module-qtquick-window qml6-module-qtquick-dialogs qml6-module-qtqml-workerscript qml6-module-qt-labs-platform \
  qt6-shadertools-dev \
  python3 python3-venv python3-speechd alsa-utils poppler-utils speech-dispatcher speech-dispatcher-espeak-ng espeak-ng
cmake -S . -B build -G Ninja -DCMAKE_BUILD_TYPE=Release -DCMAKE_INSTALL_PREFIX=/usr
cmake --build build -j 4
ctest --test-dir build --output-on-failure
JARVIS_TEST_BUILD="$PWD/build" dbus-run-session --config-file=tests/test-bus.conf -- python3 tests/integration.py
```

Source strings are English; the Russian translation lives in `i18n/jarvis_ru.ts`. After changing strings run `/usr/lib/qt6/bin/lupdate -locations none -no-obsolete *.cpp *.h *.qml -ts i18n/jarvis_ru.ts` and translate the new messages (the `translations` test checks completeness). To run a development daemon next to an installed one: `JARVIS_DBUS_SERVICE=org.jarvis.DaemonDev ./build/jarvisd`.
