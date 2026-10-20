# Jarvis for VS Code

Your coding assistant from **Jarvis for Kubuntu**, running on this computer. It explains, fixes, reviews and documents code, writes tests and untangles compiler errors — and it learns your languages, projects and the solutions that worked for you.

## Where to find it

| Where | What |
|---|---|
| **Jarvis icon in the activity bar** (left) | Everything Jarvis can do, whether it is connected, whether Claude is on, and your recent answers. |
| **"Jarvis" in the status bar** (bottom right) or **Ctrl+Alt+J** | A menu of all actions; each says what it needs ("needs a selection"). |
| **Right-click in the editor → Jarvis** | The actions for the selected code. |
| **Light bulb on an error** (Ctrl+.) | "Explain the errors — Jarvis" for that error. |
| **Ctrl+Alt+K** | Ask a question about the code. |
| **Help → Get Started → Get started with Jarvis** | A five-step tour. |

## What it does

- **Ask about the code** — any programming question; the open file and the selection go along.
- **Explain the selection** — what the code does, step by step, and where the risks are.
- **Fix the selection** — corrected code. **Compare** shows your file and the fixed one side by side before **Replace selection**.
- **Write tests** — unit tests in the framework your project uses.
- **Add comments** — documentation comments without changing the logic; replace the selection in one click.
- **Review the file** — bugs first, then robustness and readability, with line numbers.
- **Explain the errors** — why the compiler or linter complains near the cursor and how to fix it.
- **Teach Jarvis** — save your own working code as the solution to a problem.

Every code block in an answer has **Copy**, **Insert at cursor** and **New file**. Rate answers with 👍 / 👎: good solutions become lessons Jarvis offers again for similar problems — even without the internet; bad ones are dropped.

## Requirements

The Jarvis app (`jarvisd`) on this computer. Answers come from Claude, so a Claude API key must be set in Jarvis (Menu → Control center → Application → Claude API settings); lessons you taught work without it.

## Privacy

Code is sent to Claude only when you run a Jarvis action, and only the selection (or, for a review, the file — up to 12,000 characters). With `jarvis.learnFromCoding` on (default), Jarvis also notes which languages and projects you work on, the frameworks named in project files (CMakeLists.txt, package.json, platformio.ini…) and compiler error messages; this stays in `~/.local/share/jarvis/code.json`. The extension talks to Jarvis over `$XDG_RUNTIME_DIR/jarvis-ide.sock`, a socket only your user can open.

## Settings

- `jarvis.learnFromCoding` — let Jarvis learn your programming (on).
- `jarvis.lightbulb` — offer Jarvis in the light bulb on errors (on).
- `jarvis.socketPath` — the daemon socket, if not the default.

License: same as Jarvis for Kubuntu (https://github.com/Bohdan99py/jarvis-kubuntu).
