'use strict';
// Jarvis for VS Code: talks to the local jarvisd over its IDE socket.
// Code leaves the editor only when the user runs a Jarvis command; the
// learning signals (language, project name, frameworks, error messages)
// stay on this computer with the daemon.
const vscode = require('vscode');
const net = require('net');
const fs = require('fs');
const crypto = require('crypto');
const lib = require('./lib');

const MAX_SELECTION = 12000;
const REQUEST_TIMEOUT_MS = 150000;

const ru = (vscode.env.language || '').startsWith('ru');
const T = ru ? {
  connected: 'Jarvis подключён', disconnected: 'Jarvis не запущен — откройте приложение Jarvis',
  thinking: 'Jarvis думает…', askPrompt: 'Спросите Jarvis о коде', noEditor: 'Откройте файл в редакторе.',
  needSelection: 'Выделите код.', noDiagnostics: 'Здесь нет ошибок и предупреждений.',
  teachPrompt: 'Какую задачу решает выделенный код?', taught: 'Jarvis запомнил это решение.',
  insert: 'Вставить', replace: 'Заменить выделенное', copy: 'Копировать', copied: 'Скопировано.',
  good: '👍 Помогло', bad: '👎 Не то', teach: 'Научить', notConnected: 'Jarvis не запущен. Откройте приложение Jarvis или включите службу jarvis.service.',
  rejected: 'Jarvis отклонил запрос.', busy: 'Jarvis занят предыдущими запросами.', timeout: 'Jarvis не ответил вовремя.',
  answerTitle: 'Jarvis', teachHint: 'Выделите правильный код и выполните «Jarvis: научить».',
} : {
  connected: 'Jarvis connected', disconnected: 'Jarvis is not running — open the Jarvis app',
  thinking: 'Jarvis is thinking…', askPrompt: 'Ask Jarvis about your code', noEditor: 'Open a file in the editor.',
  needSelection: 'Select some code first.', noDiagnostics: 'No errors or warnings here.',
  teachPrompt: 'What problem does the selected code solve?', taught: 'Jarvis saved this solution.',
  insert: 'Insert', replace: 'Replace selection', copy: 'Copy', copied: 'Copied.',
  good: '👍 Helped', bad: '👎 Wrong', teach: 'Teach', notConnected: 'Jarvis is not running. Open the Jarvis app or enable jarvis.service.',
  rejected: 'Jarvis rejected the request.', busy: 'Jarvis is busy with earlier requests.', timeout: 'Jarvis did not answer in time.',
  answerTitle: 'Jarvis', teachHint: 'Select the right code and run "Jarvis: Teach Jarvis This Solution".',
};

class JarvisClient {
  constructor(onState) {
    this.onState = onState;
    this.socket = null;
    this.buffer = '';
    this.nextId = 1;
    this.pending = new Map();
    this.connected = false;
    this.retryMs = 1000;
    this.timer = null;
    this.disposed = false;
    this.claude = false;
  }

  path() {
    const setting = vscode.workspace.getConfiguration('jarvis').get('socketPath');
    const uid = typeof process.getuid === 'function' ? process.getuid() : undefined;
    const candidates = lib.socketCandidates(setting, process.env, uid);
    return candidates.find(p => fs.existsSync(p)) || candidates[0];
  }

  connect() {
    if (this.disposed || this.socket) return;
    const target = this.path();
    if (!target) return this.scheduleRetry();
    const socket = net.createConnection(target);
    this.socket = socket;
    socket.setEncoding('utf8');
    socket.on('connect', () => {
      this.connected = true;
      this.retryMs = 1000;
      this.request({ type: 'hello', client: 'vscode' }, 5000)
        .then(hello => { this.claude = !!hello.claude; this.onState(true); })
        .catch(() => this.onState(true));
    });
    socket.on('data', chunk => {
      this.buffer += chunk;
      if (this.buffer.length > 4 * 1024 * 1024) { socket.destroy(); return; }
      const [messages, rest] = lib.parseLines(this.buffer);
      this.buffer = rest;
      for (const m of messages) {
        const waiter = this.pending.get(m.id);
        if (waiter) {
          this.pending.delete(m.id);
          clearTimeout(waiter.timer);
          waiter.resolve(m);
        }
      }
    });
    socket.on('error', () => { /* 'close' follows */ });
    socket.on('close', () => {
      this.socket = null;
      this.buffer = '';
      const was = this.connected;
      this.connected = false;
      for (const [, waiter] of this.pending) { clearTimeout(waiter.timer); waiter.reject(new Error(T.notConnected)); }
      this.pending.clear();
      if (was) this.onState(false);
      this.scheduleRetry();
    });
  }

  scheduleRetry() {
    if (this.disposed || this.timer) return;
    this.timer = setTimeout(() => { this.timer = null; this.connect(); }, this.retryMs);
    this.retryMs = Math.min(this.retryMs * 2, 30000);
  }

  // Fire and forget (learning signals).
  notify(message) {
    if (this.connected && this.socket) this.socket.write(JSON.stringify(message) + '\n');
  }

  request(message, timeoutMs = REQUEST_TIMEOUT_MS) {
    if (!this.connected || !this.socket) return Promise.reject(new Error(T.notConnected));
    const id = this.nextId++;
    return new Promise((resolve, reject) => {
      const timer = setTimeout(() => { this.pending.delete(id); reject(new Error(T.timeout)); }, timeoutMs);
      this.pending.set(id, { resolve, reject, timer });
      this.socket.write(JSON.stringify(Object.assign({}, message, { id })) + '\n');
    });
  }

  dispose() {
    this.disposed = true;
    clearTimeout(this.timer);
    if (this.socket) this.socket.destroy();
  }
}

let client;
let statusItem;
let panel;
let lastAnswer = null; // { request, text, uri, selection, mode, problem }

function projectOf(uri) {
  const folder = uri && vscode.workspace.getWorkspaceFolder(uri);
  return folder ? folder.name : '';
}

function editorContext(editor, wholeErrors) {
  const doc = editor.document;
  const selection = editor.selection;
  let code = doc.getText(selection);
  if (code.length > MAX_SELECTION) code = code.slice(0, MAX_SELECTION);
  // Diagnostics in the selection, or around the cursor.
  const from = selection.isEmpty ? Math.max(0, selection.active.line - 3) : selection.start.line;
  const to = selection.isEmpty ? selection.active.line + 3 : selection.end.line;
  const diagnostics = vscode.languages.getDiagnostics(doc.uri)
    .filter(d => wholeErrors ? d.severity === vscode.DiagnosticSeverity.Error || (d.range.start.line >= from && d.range.start.line <= to)
                             : d.range.start.line >= from && d.range.start.line <= to)
    .slice(0, 10)
    .map(d => ({ line: d.range.start.line + 1, severity: ['error', 'warning', 'info', 'hint'][d.severity] || 'info',
                 message: String(d.message).slice(0, 500) }));
  if (!code && diagnostics.length) {
    // No selection: give the lines around the first diagnostic as code.
    const line = diagnostics[0].line - 1;
    const range = new vscode.Range(Math.max(0, line - 6), 0, Math.min(doc.lineCount - 1, line + 6), 10000);
    code = doc.getText(range).slice(0, MAX_SELECTION);
  }
  return {
    language: doc.languageId,
    file: vscode.workspace.asRelativePath(doc.uri, false),
    project: projectOf(doc.uri),
    selection: code,
    diagnostics,
  };
}

async function ask(mode, text) {
  const editor = vscode.window.activeTextEditor;
  if (!editor && mode !== 'ask') { vscode.window.showWarningMessage(T.noEditor); return; }
  const context = editor ? editorContext(editor, mode === 'error') : {};
  if ((mode === 'explain' || mode === 'fix' || mode === 'tests') && !context.selection) {
    vscode.window.showWarningMessage(T.needSelection); return;
  }
  if (mode === 'error' && !(context.diagnostics || []).length) { vscode.window.showInformationMessage(T.noDiagnostics); return; }
  try {
    const reply = await vscode.window.withProgress(
      { location: vscode.ProgressLocation.Notification, title: T.thinking, cancellable: false },
      () => client.request({ type: 'ask', mode, text: text || '', context }));
    if (!reply.ok) {
      vscode.window.showWarningMessage(reply.text === 'busy' ? T.busy : T.rejected);
      return;
    }
    lastAnswer = {
      request: reply.request, text: reply.text, mode,
      uri: editor ? editor.document.uri : null,
      selection: editor ? editor.selection : null,
    };
    showAnswer(lastAnswer);
  } catch (e) {
    vscode.window.showErrorMessage(e.message);
  }
}

function showAnswer(answer) {
  if (!panel) {
    panel = vscode.window.createWebviewPanel('jarvisAnswer', T.answerTitle, { viewColumn: vscode.ViewColumn.Beside, preserveFocus: true },
      { enableScripts: true, retainContextWhenHidden: true, localResourceRoots: [] });
    panel.onDidDispose(() => { panel = null; });
    panel.webview.onDidReceiveMessage(onPanelMessage);
  }
  const nonce = crypto.randomBytes(16).toString('base64');
  const parts = lib.splitAnswer(answer.text);
  let blocks = 0;
  const canReplace = answer.mode === 'fix' && answer.selection && !answer.selection.isEmpty;
  const body = parts.map(part => {
    if (part.kind === 'text') return `<div class="text">${lib.renderText(part.text)}</div>`;
    const index = blocks++;
    return `<div class="code"><div class="bar"><span>${lib.escapeHtml(part.language)}</span>
      <button data-action="insert" data-index="${index}">${canReplace ? T.replace : T.insert}</button>
      <button data-action="copy" data-index="${index}">${T.copy}</button></div>
      <pre><code>${lib.escapeHtml(part.text)}</code></pre></div>`;
  }).join('\n');
  panel.webview.html = `<!DOCTYPE html><html><head><meta charset="utf-8">
<meta http-equiv="Content-Security-Policy" content="default-src 'none'; style-src 'unsafe-inline'; script-src 'nonce-${nonce}';">
<style>
body { font-family: var(--vscode-font-family); color: var(--vscode-foreground); padding: 8px 14px; line-height: 1.5; }
.code { border: 1px solid var(--vscode-panel-border); border-radius: 6px; margin: 10px 0; overflow: hidden; }
.bar { display: flex; gap: 6px; align-items: center; padding: 4px 8px; background: var(--vscode-editorWidget-background); }
.bar span { flex: 1; opacity: .7; font-size: 12px; }
pre { margin: 0; padding: 10px; overflow-x: auto; background: var(--vscode-textCodeBlock-background); }
code { font-family: var(--vscode-editor-font-family); font-size: var(--vscode-editor-font-size); }
button { background: var(--vscode-button-secondaryBackground); color: var(--vscode-button-secondaryForeground); border: 0; border-radius: 4px; padding: 3px 10px; cursor: pointer; }
button:hover { background: var(--vscode-button-secondaryHoverBackground); }
.rate { margin-top: 16px; display: flex; gap: 8px; }
</style></head><body>
${body}
<div class="rate"><button data-action="good">${T.good}</button><button data-action="bad">${T.bad}</button></div>
<script nonce="${nonce}">
const vscode = acquireVsCodeApi();
document.addEventListener('click', e => {
  const b = e.target.closest('button');
  if (!b) return;
  vscode.postMessage({ action: b.dataset.action, index: Number(b.dataset.index || 0) });
});
</script></body></html>`;
  panel.reveal(vscode.ViewColumn.Beside, true);
}

async function onPanelMessage(message) {
  if (!lastAnswer) return;
  const codes = lib.splitAnswer(lastAnswer.text).filter(p => p.kind === 'code');
  if (message.action === 'copy' && codes[message.index]) {
    await vscode.env.clipboard.writeText(codes[message.index].text);
    vscode.window.setStatusBarMessage(T.copied, 2000);
  } else if (message.action === 'insert' && codes[message.index] && lastAnswer.uri) {
    const doc = await vscode.workspace.openTextDocument(lastAnswer.uri);
    const editor = await vscode.window.showTextDocument(doc, { preview: false });
    const code = codes[message.index].text;
    const replace = lastAnswer.mode === 'fix' && lastAnswer.selection && !lastAnswer.selection.isEmpty;
    await editor.edit(edit => {
      if (replace) edit.replace(lastAnswer.selection, code);
      else edit.insert(editor.selection.active, code);
    });
  } else if (message.action === 'good' || message.action === 'bad') {
    const good = message.action === 'good';
    try {
      const result = await client.request({ type: 'rate', request: lastAnswer.request, good }, 10000);
      if (good) vscode.window.showInformationMessage(result.text);
      else {
        const choice = await vscode.window.showInformationMessage(result.text, T.teach);
        if (choice === T.teach) vscode.window.showInformationMessage(T.teachHint);
      }
    } catch (e) {
      vscode.window.showErrorMessage(e.message);
    }
  }
}

async function teach() {
  const editor = vscode.window.activeTextEditor;
  if (!editor || editor.selection.isEmpty) { vscode.window.showWarningMessage(T.needSelection); return; }
  const problem = await vscode.window.showInputBox({ prompt: T.teachPrompt, ignoreFocusOut: true });
  if (!problem) return;
  try {
    const result = await client.request({
      type: 'teach', language: editor.document.languageId, problem,
      solution: '```' + editor.document.languageId + '\n' + editor.document.getText(editor.selection).slice(0, 2900) + '\n```',
    }, 10000);
    if (result.ok) vscode.window.showInformationMessage(T.taught);
    else vscode.window.showWarningMessage(result.text || T.rejected);
  } catch (e) {
    vscode.window.showErrorMessage(e.message);
  }
}

// ---- Learning signals ----------------------------------------------------------

function learning() {
  return vscode.workspace.getConfiguration('jarvis').get('learnFromCoding', true);
}

const lastBeat = new Map();
function heartbeat(doc, force) {
  if (!learning() || !doc || doc.uri.scheme !== 'file') return;
  const project = projectOf(doc.uri);
  const key = doc.languageId + '|' + project;
  const now = Date.now();
  if (!force && now - (lastBeat.get(key) || 0) < 30000) return;
  lastBeat.set(key, now);
  client.notify({ type: 'activity', language: doc.languageId, project });
}

async function scanWorkspace() {
  if (!learning()) return;
  for (const folder of vscode.workspace.workspaceFolders || []) {
    if (folder.uri.scheme !== 'file') continue;
    const counts = {};
    try {
      const files = await vscode.workspace.findFiles(new vscode.RelativePattern(folder, '**/*.*'),
        '**/{node_modules,build,dist,.git,target,venv,.venv,__pycache__}/**', 3000);
      for (const f of files) {
        const language = lib.languageOfFile(f.fsPath);
        if (language) counts[language] = (counts[language] || 0) + 1;
      }
    } catch (e) { /* scanning is best effort */ }
    const languages = Object.entries(counts).sort((a, b) => b[1] - a[1]).slice(0, 6).map(e => e[0]);
    client.notify({ type: 'workspace', project: folder.name, languages, frameworks: lib.detectFrameworks(folder.uri.fsPath) });
  }
}

const seenErrors = new Set();
function onDiagnostics(event) {
  if (!learning()) return;
  for (const uri of event.uris) {
    if (uri.scheme !== 'file') continue;
    const doc = vscode.workspace.textDocuments.find(d => d.uri.toString() === uri.toString());
    for (const d of vscode.languages.getDiagnostics(uri)) {
      if (d.severity !== vscode.DiagnosticSeverity.Error) continue;
      const key = uri.toString() + '|' + d.message;
      if (seenErrors.has(key)) continue;
      seenErrors.add(key);
      if (seenErrors.size > 2000) seenErrors.clear();
      client.notify({ type: 'diagnostic', language: doc ? doc.languageId : '', project: projectOf(uri), message: String(d.message).slice(0, 500) });
    }
  }
}

function setState(connected) {
  statusItem.text = connected ? '$(hubot) Jarvis' : '$(debug-disconnect) Jarvis';
  statusItem.tooltip = connected ? T.connected : T.disconnected;
  if (connected) scanWorkspace();
}

function activate(context) {
  statusItem = vscode.window.createStatusBarItem(vscode.StatusBarAlignment.Right, 100);
  statusItem.command = 'jarvis.ask';
  statusItem.show();
  client = new JarvisClient(setState);
  setState(false);
  client.connect();

  context.subscriptions.push(
    statusItem,
    { dispose: () => client.dispose() },
    vscode.commands.registerCommand('jarvis.ask', async () => {
      const text = await vscode.window.showInputBox({ prompt: T.askPrompt, ignoreFocusOut: true });
      if (text) ask('ask', text);
    }),
    vscode.commands.registerCommand('jarvis.explain', () => ask('explain')),
    vscode.commands.registerCommand('jarvis.fix', () => ask('fix')),
    vscode.commands.registerCommand('jarvis.tests', () => ask('tests')),
    vscode.commands.registerCommand('jarvis.error', () => ask('error')),
    vscode.commands.registerCommand('jarvis.teach', teach),
    vscode.window.onDidChangeActiveTextEditor(editor => editor && heartbeat(editor.document, true)),
    vscode.workspace.onDidChangeTextDocument(event => heartbeat(event.document, false)),
    vscode.workspace.onDidChangeWorkspaceFolders(scanWorkspace),
    vscode.languages.onDidChangeDiagnostics(onDiagnostics),
  );
}

function deactivate() {
  if (client) client.dispose();
}

module.exports = { activate, deactivate };
