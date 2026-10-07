'use strict';
// Unit tests for the VS Code extension helpers (plain Node, no VS Code).
const assert = require('assert');
const fs = require('fs');
const os = require('os');
const path = require('path');
const lib = require('../vscode/lib');

// Socket path: setting wins, real runtime dir before a snap-private one.
assert.deepStrictEqual(lib.socketCandidates('/x.sock', {}, 1000), ['/x.sock']);
assert.deepStrictEqual(lib.socketCandidates('', { XDG_RUNTIME_DIR: '/run/user/1000/snap.code' }, 1000),
  ['/run/user/1000/jarvis-ide.sock', '/run/user/1000/snap.code/jarvis-ide.sock']);

// Line protocol: complete lines parsed, the partial rest kept, garbage skipped.
const [messages, rest] = lib.parseLines('{"id":1}\nnot json\n{"id":2}\n{"id":');
assert.deepStrictEqual(messages, [{ id: 1 }, { id: 2 }]);
assert.strictEqual(rest, '{"id":');

// Answers: code blocks split out, text escaped (no HTML injection from answers).
const parts = lib.splitAnswer('Fix:\n```cpp\nint x = 1;\n```\nDone <b>');
assert.deepStrictEqual(parts.map(p => p.kind), ['text', 'code', 'text']);
assert.strictEqual(parts[1].language, 'cpp');
assert.strictEqual(parts[1].text, 'int x = 1;');
const html = lib.renderText('**bold** `a<b>` <script>alert(1)</script>');
assert.ok(html.includes('<strong>bold</strong>'));
assert.ok(html.includes('<code>a&lt;b&gt;</code>'));
assert.ok(!html.includes('<script>'));

// Frameworks from project files.
const dir = fs.mkdtempSync(path.join(os.tmpdir(), 'jarvis-vscode-'));
try {
  fs.writeFileSync(path.join(dir, 'CMakeLists.txt'), 'project(x)\nfind_package(Qt6 REQUIRED)\nfind_package(GTest)\n');
  fs.writeFileSync(path.join(dir, 'package.json'), JSON.stringify({ dependencies: { react: '1' }, devDependencies: { jest: '1' } }));
  fs.writeFileSync(path.join(dir, 'platformio.ini'), '[env:esp32dev]\nboard = esp32dev\nframework = arduino\n');
  const found = lib.detectFrameworks(dir);
  for (const name of ['CMake', 'Qt 6', 'GoogleTest', 'Node.js', 'React', 'Jest', 'PlatformIO', 'Arduino', 'ESP32'])
    assert.ok(found.includes(name), `${name} in ${found}`);
} finally {
  fs.rmSync(dir, { recursive: true, force: true });
}

assert.strictEqual(lib.languageOfFile('/a/b/main.cpp'), 'cpp');
assert.strictEqual(lib.languageOfFile('/a/b/Main.qml'), 'qml');
assert.strictEqual(lib.languageOfFile('/a/b/notes.txt'), '');

console.log('PASS: vscode extension helpers');
