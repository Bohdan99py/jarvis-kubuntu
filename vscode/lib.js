'use strict';
// Helpers without a VS Code dependency, so they can be tested with plain Node.
const fs = require('fs');
const path = require('path');

const EXTENSION_LANGUAGES = {
  '.cpp': 'cpp', '.cc': 'cpp', '.cxx': 'cpp', '.hpp': 'cpp', '.hh': 'cpp', '.h': 'cpp', '.c': 'c',
  '.py': 'python', '.js': 'javascript', '.mjs': 'javascript', '.ts': 'typescript', '.tsx': 'typescriptreact',
  '.jsx': 'javascriptreact', '.qml': 'qml', '.rs': 'rust', '.go': 'go', '.java': 'java', '.kt': 'kotlin',
  '.cs': 'csharp', '.sh': 'shellscript', '.lua': 'lua', '.dart': 'dart', '.swift': 'swift', '.php': 'php',
  '.rb': 'ruby', '.ino': 'arduino', '.glsl': 'glsl', '.sql': 'sql',
};

// Daemon socket: an explicit setting, else the runtime directory. Snap builds
// of VS Code may point XDG_RUNTIME_DIR at a private folder, so the real
// /run/user/<uid> comes first.
function socketCandidates(setting, env, uid) {
  if (setting) return [setting];
  const out = [];
  if (typeof uid === 'number') out.push(`/run/user/${uid}/jarvis-ide.sock`);
  if (env.XDG_RUNTIME_DIR) out.push(path.join(env.XDG_RUNTIME_DIR, 'jarvis-ide.sock'));
  return [...new Set(out)];
}

// Splits a stream into JSON lines; returns [messages, rest].
function parseLines(buffer) {
  const messages = [];
  let index;
  while ((index = buffer.indexOf('\n')) >= 0) {
    const line = buffer.slice(0, index);
    buffer = buffer.slice(index + 1);
    if (!line.trim()) continue;
    try { messages.push(JSON.parse(line)); } catch (e) { /* ignore a broken line */ }
  }
  return [messages, buffer];
}

function escapeHtml(text) {
  return String(text).replace(/[&<>"']/g, c => ({ '&': '&amp;', '<': '&lt;', '>': '&gt;', '"': '&quot;', "'": '&#39;' }[c]));
}

// Splits an answer into text and fenced code blocks.
function splitAnswer(text) {
  const parts = [];
  const re = /```([\w+#.-]*)\n([\s\S]*?)```/g;
  let last = 0;
  let m;
  while ((m = re.exec(text)) !== null) {
    if (m.index > last) parts.push({ kind: 'text', text: text.slice(last, m.index) });
    parts.push({ kind: 'code', language: m[1], text: m[2].replace(/\n$/, '') });
    last = re.lastIndex;
  }
  if (last < text.length) parts.push({ kind: 'text', text: text.slice(last) });
  return parts;
}

// Minimal, safe Markdown: everything is escaped first; only `code`, **bold**,
// headings and list bullets get markup.
function renderText(text) {
  return escapeHtml(text)
    .replace(/`([^`\n]+)`/g, '<code>$1</code>')
    .replace(/\*\*([^*\n]+)\*\*/g, '<strong>$1</strong>')
    .replace(/^#{1,4} (.*)$/gm, '<strong>$1</strong>')
    .replace(/^\s*[-*] (.*)$/gm, '• $1')
    .replace(/\n/g, '<br>');
}

function readSmall(file, max = 200 * 1024) {
  try {
    const stat = fs.statSync(file);
    if (!stat.isFile() || stat.size > max) return '';
    return fs.readFileSync(file, 'utf8');
  } catch (e) {
    return '';
  }
}

// Frameworks and tools named in a project's root files. Reads a handful of
// well-known files only; nothing leaves the computer except these names.
function detectFrameworks(root) {
  const found = new Set();
  const add = name => found.add(name);
  const cmake = readSmall(path.join(root, 'CMakeLists.txt'));
  if (cmake) {
    add('CMake');
    const known = { qt6: 'Qt 6', qt5: 'Qt 5', boost: 'Boost', opencv: 'OpenCV', sdl2: 'SDL2', sdl3: 'SDL3',
      kf6: 'KDE Frameworks', kf5: 'KDE Frameworks', gtest: 'GoogleTest', catch2: 'Catch2', vulkan: 'Vulkan', openssl: 'OpenSSL' };
    for (const m of cmake.matchAll(/find_package\s*\(\s*([A-Za-z0-9_]+)/gi)) {
      const key = m[1].toLowerCase();
      if (known[key]) add(known[key]);
    }
  }
  const pkg = readSmall(path.join(root, 'package.json'));
  if (pkg) {
    try {
      const json = JSON.parse(pkg);
      const deps = Object.assign({}, json.dependencies, json.devDependencies);
      add('Node.js');
      const known = { react: 'React', vue: 'Vue', svelte: 'Svelte', next: 'Next.js', express: 'Express', electron: 'Electron',
        typescript: 'TypeScript', vite: 'Vite', jest: 'Jest', vitest: 'Vitest', '@angular/core': 'Angular', 'react-native': 'React Native' };
      for (const name of Object.keys(deps || {})) if (known[name]) add(known[name]);
    } catch (e) { /* not JSON */ }
  }
  const python = readSmall(path.join(root, 'requirements.txt')) + '\n' + readSmall(path.join(root, 'pyproject.toml'));
  if (python.trim()) {
    add('Python');
    const known = { django: 'Django', flask: 'Flask', fastapi: 'FastAPI', numpy: 'NumPy', pandas: 'pandas', torch: 'PyTorch',
      tensorflow: 'TensorFlow', pyside6: 'PySide6', pyqt6: 'PyQt6', pytest: 'pytest', pygame: 'pygame' };
    for (const [key, name] of Object.entries(known)) if (new RegExp(`\\b${key}\\b`, 'i').test(python)) add(name);
  }
  const cargo = readSmall(path.join(root, 'Cargo.toml'));
  if (cargo) {
    add('Cargo');
    for (const [key, name] of Object.entries({ tokio: 'Tokio', serde: 'Serde', bevy: 'Bevy', axum: 'Axum' }))
      if (new RegExp(`^\\s*${key}\\s*=`, 'm').test(cargo)) add(name);
  }
  if (readSmall(path.join(root, 'go.mod'))) add('Go modules');
  const pio = readSmall(path.join(root, 'platformio.ini'));
  if (pio) {
    add('PlatformIO');
    if (/framework\s*=\s*arduino/i.test(pio)) add('Arduino');
    if (/framework\s*=\s*espidf/i.test(pio)) add('ESP-IDF');
    if (/esp32/i.test(pio)) add('ESP32');
  }
  if (readSmall(path.join(root, 'pubspec.yaml'))) add('Flutter');
  try {
    if (fs.readdirSync(root).some(f => f.endsWith('.pro'))) add('qmake');
  } catch (e) { /* unreadable */ }
  return [...found].slice(0, 20);
}

function languageOfFile(file) {
  return EXTENSION_LANGUAGES[path.extname(file).toLowerCase()] || '';
}

module.exports = { socketCandidates, parseLines, escapeHtml, splitAnswer, renderText, detectFrameworks, languageOfFile };
