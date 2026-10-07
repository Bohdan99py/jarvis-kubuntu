# Jarvis for VS Code

Connects VS Code to the Jarvis assistant running on this computer (Jarvis for Kubuntu, `jarvisd`).

**Commands** (Command Palette → "Jarvis", the editor context menu, `Ctrl+Alt+J`):

- **Ask** — a programming question with the current file as context.
- **Explain / Fix / Write tests** for the selected code. Fixes can replace the selection in one click.
- **Explain errors here** — the diagnostics around the cursor.
- **Teach Jarvis this solution** — save the selected code as the answer to a problem.

Rate answers with 👍 / 👎: good solutions become lessons Jarvis reuses for similar problems (offline too), bad ones are dropped.

**What Jarvis learns** (setting `jarvis.learnFromCoding`, on by default): the languages and projects you work on, frameworks named in project files (CMakeLists.txt, package.json, requirements.txt, Cargo.toml, platformio.ini…) and compiler error messages. This stays in `~/.local/share/jarvis/code.json` on this computer. Code is sent to Claude only when you run a Jarvis command, and only if a Claude API key is set in Jarvis.

The extension talks to the daemon over `$XDG_RUNTIME_DIR/jarvis-ide.sock`, a socket only your user can open.

License: same as Jarvis for Kubuntu (https://github.com/Bohdan99py/jarvis-kubuntu).
