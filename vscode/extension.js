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
  thinking: 'Jarvis думает…', askPrompt: 'Спросите Jarvis о коде (открытый файл пойдёт в контекст)', noEditor: 'Откройте файл в редакторе.',
  needSelection: 'Сначала выделите код.', noDiagnostics: 'Здесь нет ошибок и предупреждений.',
  teachPrompt: 'Какую задачу решает выделенный код?', taught: 'Jarvis запомнил это решение.',
  insert: 'Вставить у курсора', replace: 'Заменить выделенное', copy: 'Копировать', copied: 'Скопировано.',
  diff: 'Сравнить', newFile: 'Новый файл', diffTitle: 'Jarvis: ваш код ↔ предложенный',
  good: '👍 Помогло', bad: '👎 Не то', rateHint: 'Оценка учит Jarvis: хорошие решения он запомнит и предложит снова.',
  teach: 'Научить', notConnected: 'Jarvis не запущен. Откройте приложение Jarvis или включите службу jarvis.service.',
  rejected: 'Jarvis отклонил запрос.', busy: 'Jarvis занят предыдущими запросами.', timeout: 'Jarvis не ответил вовремя.',
  answerTitle: 'Jarvis', teachHint: 'Выделите правильный код и выполните «Jarvis: научить этому решению».',
  noClaude: 'Для ответов нужен ключ Claude API: в Jarvis → Меню → Центр управления → Приложение → Настройки Claude API. Уроки работают и без него.',
  claudeOn: 'Claude подключён', claudeOff: 'Нет ключа Claude — работают только уроки',
  menuTitle: 'Jarvis — что сделать?', needsSelection: 'нужно выделение', needsErrors: 'нет ошибок рядом', needsFile: 'откройте файл',
  lines: 'строки', whole: 'весь файл', truncated: 'файл длиннее 12 000 символов — отправлено начало', openFailed: 'Не удалось запустить jarvis.',
  modes: {
    ask: ['Спросить о коде', 'Вопрос по программированию; открытый файл и выделение идут в контекст', 'comment-discussion'],
    explain: ['Объяснить выделенное', 'Что делает этот код, шаг за шагом, и где риски', 'book'],
    fix: ['Исправить выделенное', 'Найти ошибки, предложить исправленный код и сравнить с вашим', 'wrench'],
    tests: ['Написать тесты', 'Юнит-тесты для выделенного кода в привычном для проекта фреймворке', 'beaker'],
    document: ['Добавить комментарии', 'Документирующие комментарии без изменения логики', 'note'],
    review: ['Ревью файла', 'Проверка всего файла: ошибки, надёжность, читаемость — с номерами строк', 'checklist'],
    error: ['Разобрать ошибки', 'Причина ошибок компилятора и линтера рядом с курсором и как их исправить', 'error'],
    teach: ['Научить Jarvis', 'Сохранить выделенный код как решение задачи — Jarvis предложит его снова', 'mortar-board'],
  },
} : {
  connected: 'Jarvis connected', disconnected: 'Jarvis is not running — open the Jarvis app',
  thinking: 'Jarvis is thinking…', askPrompt: 'Ask Jarvis about your code (the open file goes along as context)', noEditor: 'Open a file in the editor.',
  needSelection: 'Select some code first.', noDiagnostics: 'No errors or warnings here.',
  teachPrompt: 'What problem does the selected code solve?', taught: 'Jarvis saved this solution.',
  insert: 'Insert at cursor', replace: 'Replace selection', copy: 'Copy', copied: 'Copied.',
  diff: 'Compare', newFile: 'New file', diffTitle: 'Jarvis: your code ↔ proposed',
  good: '👍 Helped', bad: '👎 Wrong', rateHint: 'Rating teaches Jarvis: good solutions are remembered and offered again.',
  teach: 'Teach', notConnected: 'Jarvis is not running. Open the Jarvis app or enable jarvis.service.',
  rejected: 'Jarvis rejected the request.', busy: 'Jarvis is busy with earlier requests.', timeout: 'Jarvis did not answer in time.',
  answerTitle: 'Jarvis', teachHint: 'Select the right code and run "Jarvis: Teach Jarvis This Solution".',
  noClaude: 'Answers need a Claude API key: Jarvis → Menu → Control center → Application → Claude API settings. Lessons work without it.',
  claudeOn: 'Claude connected', claudeOff: 'No Claude key — only lessons work',
  menuTitle: 'Jarvis — what should I do?', needsSelection: 'needs a selection', needsErrors: 'no errors nearby', needsFile: 'open a file',
  lines: 'lines', whole: 'whole file', truncated: 'the file is longer than 12,000 characters — the beginning was sent', openFailed: 'Could not start jarvis.',
  modes: {
    ask: ['Ask about the code', 'A programming question; the open file and the selection go along as context', 'comment-discussion'],
    explain: ['Explain the selection', 'What the code does, step by step, and where the risks are', 'book'],
    fix: ['Fix the selection', 'Find the bugs, propose corrected code and compare it with yours', 'wrench'],
    tests: ['Write tests', 'Unit tests for the selected code in the framework your project uses', 'beaker'],
    document: ['Add comments', 'Documentation comments without changing the logic', 'note'],
    review: ['Review the file', 'The whole file checked: bugs, robustness, readability — with line numbers', 'checklist'],
    error: ['Explain the errors', 'Why the compiler or linter errors near the cursor happen and how to fix them', 'error'],
    teach: ['Teach Jarvis', 'Save the selected code as the solution to a problem — Jarvis offers it again', 'mortar-board'],
  },
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
let answers = [];         // newest first: { request, text, mode, uri, selection, file, lines, time, original }
let current = null;       // the answer shown in the panel
let actionsView;
let historyView;
const proposals = new Map(); // jarvis-proposed:/<n> → proposed file text for the diff view
let proposalCount = 0;

function projectOf(uri) {
  const folder = uri && vscode.workspace.getWorkspaceFolder(uri);
  return folder ? folder.name : '';
}

function editorContext(editor, mode) {
  const doc = editor.document;
  const selection = editor.selection;
  let code = mode === 'review' ? doc.getText() : doc.getText(selection);
  let truncated = false;
  if (code.length > MAX_SELECTION) { code = code.slice(0, MAX_SELECTION); truncated = true; }
  // Diagnostics in the selection, or around the cursor; for a review, all errors of the file.
  const from = selection.isEmpty ? Math.max(0, selection.active.line - 3) : selection.start.line;
  const to = selection.isEmpty ? selection.active.line + 3 : selection.end.line;
  const wholeErrors = mode === 'error' || mode === 'review';
  const diagnostics = vscode.languages.getDiagnostics(doc.uri)
    .filter(d => (wholeErrors && d.severity === vscode.DiagnosticSeverity.Error) || (d.range.start.line >= from && d.range.start.line <= to))
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
    context: {
      language: doc.languageId,
      file: vscode.workspace.asRelativePath(doc.uri, false),
      project: projectOf(doc.uri),
      selection: code,
      diagnostics,
    },
    truncated,
  };
}

// What an action needs and whether the editor has it: "" when ready, else the reason.
function missing(mode) {
  const editor = vscode.window.activeTextEditor;
  if (mode === 'ask') return '';
  if (!editor) return T.needsFile;
  if (['explain', 'fix', 'tests', 'document', 'teach'].includes(mode) && editor.selection.isEmpty) return T.needsSelection;
  if (mode === 'error') {
    const line = editor.selection.active.line;
    const near = vscode.languages.getDiagnostics(editor.document.uri).some(d => Math.abs(d.range.start.line - line) <= 3);
    if (!near) return T.needsErrors;
  }
  return '';
}

function describeTarget(editor, mode) {
  if (!editor) return '';
  const file = vscode.workspace.asRelativePath(editor.document.uri, false);
  if (mode === 'review') return `${file} · ${T.whole}`;
  const s = editor.selection;
  if (!s || s.isEmpty) return file;
  return `${file} · ${T.lines} ${s.start.line + 1}–${s.end.line + 1}`;
}

async function ask(mode, text) {
  const editor = vscode.window.activeTextEditor;
  const reason = missing(mode);
  if (reason) {
    vscode.window.showWarningMessage(mode === 'error' ? T.noDiagnostics : reason === T.needsSelection ? T.needSelection : T.noEditor);
    return;
  }
  const { context, truncated } = editor ? editorContext(editor, mode) : { context: {}, truncated: false };
  const entry = {
    request: 0, text: '', mode, question: text || '',
    uri: editor ? editor.document.uri : null,
    selection: editor ? editor.selection : null,
    original: editor ? editor.document.getText() : '',
    target: describeTarget(editor, mode) + (truncated ? ' · ' + T.truncated : ''),
    time: new Date(),
  };
  showPending(entry);
  try {
    const reply = await client.request({ type: 'ask', mode, text: text || '', context });
    if (!reply.ok) {
      vscode.window.showWarningMessage(reply.text === 'busy' ? T.busy : T.rejected);
      if (panel) panel.dispose();
      return;
    }
    entry.request = reply.request;
    entry.text = reply.text;
    answers.unshift(entry);
    answers = answers.slice(0, 30);
    historyView && historyView.refresh();
    showAnswer(entry);
  } catch (e) {
    if (panel) panel.dispose();
    vscode.window.showErrorMessage(e.message);
  }
}

function ensurePanel() {
  if (!panel) {
    panel = vscode.window.createWebviewPanel('jarvisAnswer', T.answerTitle, { viewColumn: vscode.ViewColumn.Beside, preserveFocus: true },
      { enableScripts: true, retainContextWhenHidden: true, localResourceRoots: [] });
    panel.onDidDispose(() => { panel = null; });
    panel.webview.onDidReceiveMessage(onPanelMessage);
  }
  return panel;
}

const STYLE = `
body { font-family: var(--vscode-font-family); color: var(--vscode-foreground); padding: 8px 14px; line-height: 1.55; }
.head { display: flex; gap: 8px; align-items: baseline; border-bottom: 1px solid var(--vscode-panel-border); padding-bottom: 8px; margin-bottom: 8px; }
.head .mode { font-weight: 600; font-size: 1.05em; }
.head .target { opacity: .7; font-size: 12px; }
.question { opacity: .85; font-style: italic; margin: 4px 0 10px; }
.code { border: 1px solid var(--vscode-panel-border); border-radius: 6px; margin: 10px 0; overflow: hidden; }
.bar { display: flex; gap: 6px; align-items: center; padding: 4px 8px; background: var(--vscode-editorWidget-background); flex-wrap: wrap; }
.bar span { flex: 1; opacity: .7; font-size: 12px; }
pre { margin: 0; padding: 10px; overflow-x: auto; background: var(--vscode-textCodeBlock-background); }
code { font-family: var(--vscode-editor-font-family); font-size: var(--vscode-editor-font-size); }
button { background: var(--vscode-button-secondaryBackground); color: var(--vscode-button-secondaryForeground); border: 0; border-radius: 4px; padding: 3px 10px; cursor: pointer; }
button.primary { background: var(--vscode-button-background); color: var(--vscode-button-foreground); }
button:hover { filter: brightness(1.15); }
.rate { margin-top: 18px; display: flex; gap: 8px; align-items: center; flex-wrap: wrap; }
.rate small { opacity: .65; }
.thinking { display: flex; gap: 10px; align-items: center; opacity: .85; margin-top: 16px; }
.dot { width: 8px; height: 8px; border-radius: 50%; background: var(--vscode-progressBar-background); animation: pulse 1s infinite ease-in-out; }
.dot:nth-child(2) { animation-delay: .15s } .dot:nth-child(3) { animation-delay: .3s }
@keyframes pulse { 0%, 100% { opacity: .2 } 50% { opacity: 1 } }`;

function page(entry, body, nonce) {
  const [title] = T.modes[entry.mode] || [entry.mode];
  const question = entry.question ? `<div class="question">${lib.escapeHtml(entry.question)}</div>` : '';
  return `<!DOCTYPE html><html><head><meta charset="utf-8">
<meta http-equiv="Content-Security-Policy" content="default-src 'none'; style-src 'unsafe-inline'; script-src 'nonce-${nonce}';">
<style>${STYLE}</style></head><body>
<div class="head"><span class="mode">${lib.escapeHtml(title)}</span><span class="target">${lib.escapeHtml(entry.target || '')}</span></div>
${question}${body}
<script nonce="${nonce}">
const vscode = acquireVsCodeApi();
document.addEventListener('click', e => {
  const b = e.target.closest('button');
  if (!b) return;
  vscode.postMessage({ action: b.dataset.action, index: Number(b.dataset.index || 0) });
});
</script></body></html>`;
}

function showPending(entry) {
  const nonce = crypto.randomBytes(16).toString('base64');
  ensurePanel().webview.html = page(entry, `<div class="thinking"><div class="dot"></div><div class="dot"></div><div class="dot"></div><span>${T.thinking}</span></div>`, nonce);
  panel.reveal(vscode.ViewColumn.Beside, true);
}

function canReplace(entry) {
  return (entry.mode === 'fix' || entry.mode === 'document') && entry.selection && !entry.selection.isEmpty;
}

function showAnswer(entry) {
  current = entry;
  const nonce = crypto.randomBytes(16).toString('base64');
  const parts = lib.splitAnswer(entry.text);
  let blocks = 0;
  const replace = canReplace(entry);
  const body = parts.map(part => {
    if (part.kind === 'text') return `<div class="text">${lib.renderText(part.text)}</div>`;
    const index = blocks++;
    const compare = replace ? `<button data-action="diff" data-index="${index}">${T.diff}</button>` : '';
    return `<div class="code"><div class="bar"><span>${lib.escapeHtml(part.language)}</span>
      ${compare}<button class="${replace ? 'primary' : ''}" data-action="insert" data-index="${index}">${replace ? T.replace : T.insert}</button>
      <button data-action="copy" data-index="${index}">${T.copy}</button>
      <button data-action="newfile" data-index="${index}">${T.newFile}</button></div>
      <pre><code>${lib.escapeHtml(part.text)}</code></pre></div>`;
  }).join('\n');
  const rate = `<div class="rate"><button data-action="good">${T.good}</button><button data-action="bad">${T.bad}</button><small>${T.rateHint}</small></div>`;
  ensurePanel().webview.html = page(entry, body + rate, nonce);
  panel.reveal(vscode.ViewColumn.Beside, true);
}

async function onPanelMessage(message) {
  const entry = current;
  if (!entry) return;
  const codes = lib.splitAnswer(entry.text).filter(p => p.kind === 'code');
  const block = codes[message.index];
  if (message.action === 'copy' && block) {
    await vscode.env.clipboard.writeText(block.text);
    vscode.window.setStatusBarMessage(T.copied, 2000);
  } else if (message.action === 'newfile' && block) {
    const doc = await vscode.workspace.openTextDocument({ content: block.text, language: block.language || undefined });
    await vscode.window.showTextDocument(doc, { preview: false });
  } else if (message.action === 'diff' && block && entry.uri && entry.selection) {
    // The whole file as it would be with the selection replaced, next to the real one.
    const doc = await vscode.workspace.openTextDocument(entry.uri);
    const proposed = lib.replaceRange(doc.getText(), doc.offsetAt(entry.selection.start), doc.offsetAt(entry.selection.end), block.text);
    const uri = vscode.Uri.parse(`jarvis-proposed:/${++proposalCount}/${encodeURIComponent(vscode.workspace.asRelativePath(entry.uri, false))}`);
    proposals.set(uri.toString(), proposed);
    await vscode.commands.executeCommand('vscode.diff', entry.uri, uri, T.diffTitle);
  } else if (message.action === 'insert' && block && entry.uri) {
    const doc = await vscode.workspace.openTextDocument(entry.uri);
    const editor = await vscode.window.showTextDocument(doc, { preview: false });
    await editor.edit(edit => {
      if (canReplace(entry)) edit.replace(entry.selection, block.text);
      else edit.insert(editor.selection.active, block.text);
    });
  } else if (message.action === 'good' || message.action === 'bad') {
    const good = message.action === 'good';
    try {
      const result = await client.request({ type: 'rate', request: entry.request, good }, 10000);
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

// ---- Menu, side bar, light bulb ---------------------------------------------------

const ORDER = ['ask', 'explain', 'fix', 'tests', 'document', 'review', 'error', 'teach'];

function run(mode) {
  if (mode === 'teach') return teach();
  if (mode === 'ask') return vscode.commands.executeCommand('jarvis.ask');
  return ask(mode);
}

async function menu() {
  const items = ORDER.map(mode => {
    const [title, detail, icon] = T.modes[mode];
    const reason = missing(mode);
    return { label: `$(${icon}) ${title}`, description: reason, detail, mode };
  });
  if (!client.connected) items.unshift({ label: `$(link-external) ${T.disconnected}`, detail: '', mode: 'open' });
  const pick = await vscode.window.showQuickPick(items, { placeHolder: T.menuTitle, matchOnDetail: true });
  if (!pick) return;
  if (pick.mode === 'open') return openApp();
  run(pick.mode);
}

class ActionsProvider {
  constructor() { this.emitter = new vscode.EventEmitter(); this.onDidChangeTreeData = this.emitter.event; }
  refresh() { this.emitter.fire(); }
  getTreeItem(item) { return item; }
  getChildren() {
    const items = [];
    const state = new vscode.TreeItem(client.connected ? T.connected : T.disconnected);
    state.iconPath = new vscode.ThemeIcon(client.connected ? 'pass-filled' : 'debug-disconnect');
    if (!client.connected) state.command = { command: 'jarvis.openApp', title: T.disconnected };
    items.push(state);
    if (client.connected) {
      const claude = new vscode.TreeItem(client.claude ? T.claudeOn : T.claudeOff);
      claude.iconPath = new vscode.ThemeIcon(client.claude ? 'sparkle' : 'warning');
      if (!client.claude) claude.tooltip = T.noClaude;
      items.push(claude);
    }
    for (const mode of ORDER) {
      const [title, detail, icon] = T.modes[mode];
      const item = new vscode.TreeItem(title);
      item.iconPath = new vscode.ThemeIcon(icon);
      item.description = missing(mode);
      item.tooltip = detail;
      item.command = { command: 'jarvis.' + (mode === 'error' ? 'error' : mode), title };
      items.push(item);
    }
    return items;
  }
}

class HistoryProvider {
  constructor() { this.emitter = new vscode.EventEmitter(); this.onDidChangeTreeData = this.emitter.event; }
  refresh() { this.emitter.fire(); }
  getTreeItem(item) { return item; }
  getChildren() {
    return answers.map((entry, index) => {
      const [title, , icon] = T.modes[entry.mode] || [entry.mode, '', 'comment'];
      const item = new vscode.TreeItem(entry.question ? entry.question.slice(0, 60) : title);
      item.description = `${entry.target ? entry.target.split(' · ')[0] + ' · ' : ''}${entry.time.toLocaleTimeString().slice(0, 5)}`;
      item.iconPath = new vscode.ThemeIcon(icon);
      item.tooltip = title;
      item.command = { command: 'jarvis.showAnswer', title, arguments: [index] };
      return item;
    });
  }
}

// "Jarvis: explain and fix this error" in the light bulb of an error.
const codeActions = {
  provideCodeActions(document, range, context) {
    if (!vscode.workspace.getConfiguration('jarvis').get('lightbulb', true)) return [];
    const errors = (context.diagnostics || []).filter(d => d.severity === vscode.DiagnosticSeverity.Error);
    if (!errors.length) return [];
    const action = new vscode.CodeAction(T.modes.error[0] + ' — Jarvis', vscode.CodeActionKind.QuickFix);
    action.diagnostics = errors;
    action.command = { command: 'jarvis.fixDiagnostic', title: action.title, arguments: [document.uri, errors[0].range] };
    return [action];
  },
};

async function fixDiagnostic(uri, range) {
  const doc = await vscode.workspace.openTextDocument(uri);
  const editor = await vscode.window.showTextDocument(doc, { preview: false });
  editor.selection = new vscode.Selection(range.start, range.start);
  return ask('error');
}

function openApp() {
  try {
    const child = require('child_process').spawn('jarvis', [], { detached: true, stdio: 'ignore' });
    child.on('error', () => vscode.window.showErrorMessage(T.openFailed));
    child.unref();
  } catch (e) {
    vscode.window.showErrorMessage(T.openFailed);
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
  if (actionsView) actionsView.refresh();
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
  statusItem.tooltip = connected ? `${T.connected} · ${client && client.claude ? T.claudeOn : T.claudeOff} · Ctrl+Alt+J` : T.disconnected;
  if (actionsView) actionsView.refresh();
  if (connected) scanWorkspace();
}

function activate(context) {
  statusItem = vscode.window.createStatusBarItem(vscode.StatusBarAlignment.Right, 100);
  statusItem.command = 'jarvis.menu';
  statusItem.show();
  client = new JarvisClient(setState);
  actionsView = new ActionsProvider();
  historyView = new HistoryProvider();
  setState(false);
  client.connect();

  context.subscriptions.push(
    statusItem,
    { dispose: () => client.dispose() },
    vscode.window.registerTreeDataProvider('jarvis.actions', actionsView),
    vscode.window.registerTreeDataProvider('jarvis.history', historyView),
    vscode.languages.registerCodeActionsProvider({ scheme: 'file' }, codeActions,
      { providedCodeActionKinds: [vscode.CodeActionKind.QuickFix] }),
    vscode.workspace.registerTextDocumentContentProvider('jarvis-proposed', {
      provideTextDocumentContent: uri => proposals.get(uri.toString()) || '',
    }),
    vscode.commands.registerCommand('jarvis.menu', menu),
    vscode.commands.registerCommand('jarvis.ask', async () => {
      const text = await vscode.window.showInputBox({ prompt: T.askPrompt, ignoreFocusOut: true });
      if (text) ask('ask', text);
    }),
    vscode.commands.registerCommand('jarvis.explain', () => ask('explain')),
    vscode.commands.registerCommand('jarvis.fix', () => ask('fix')),
    vscode.commands.registerCommand('jarvis.tests', () => ask('tests')),
    vscode.commands.registerCommand('jarvis.document', () => ask('document')),
    vscode.commands.registerCommand('jarvis.review', () => ask('review')),
    vscode.commands.registerCommand('jarvis.error', () => ask('error')),
    vscode.commands.registerCommand('jarvis.teach', teach),
    vscode.commands.registerCommand('jarvis.openApp', openApp),
    vscode.commands.registerCommand('jarvis.fixDiagnostic', fixDiagnostic),
    vscode.commands.registerCommand('jarvis.showAnswer', index => { if (answers[index]) showAnswer(answers[index]); }),
    vscode.commands.registerCommand('jarvis.clearHistory', () => { answers = []; historyView.refresh(); }),
    vscode.window.onDidChangeActiveTextEditor(editor => { if (editor) heartbeat(editor.document, true); actionsView.refresh(); }),
    vscode.window.onDidChangeTextEditorSelection(() => actionsView.refresh()),
    vscode.workspace.onDidChangeTextDocument(event => heartbeat(event.document, false)),
    vscode.workspace.onDidChangeWorkspaceFolders(scanWorkspace),
    vscode.languages.onDidChangeDiagnostics(onDiagnostics),
  );
}

function deactivate() {
  if (client) client.dispose();
}

module.exports = { activate, deactivate };
