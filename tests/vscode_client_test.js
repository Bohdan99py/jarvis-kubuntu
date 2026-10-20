'use strict';
// Runs the real extension.js against a running jarvisd with a stub of the
// VS Code API: connect, report the workspace, ask about an error, render the
// answer, rate it. Called from tests/integration.py with JARVIS_SOCKET set.
const Module = require('module');
const assert = require('assert');

const commands = {};
const messages = [];
let html = '';
let onPanelMessage = null;
const trees = {};
const picks = [];
let lightbulb = null;
const diagnostics = [{ severity: 0, message: "'baz' was not declared in this scope", range: { start: { line: 2 } } }];
const doc = {
  uri: { scheme: 'file', fsPath: '/tmp/demo/main.cpp', toString: () => 'file:///tmp/demo/main.cpp' },
  languageId: 'cpp', lineCount: 10,
  getText: () => 'baz();',
};
const editor = { document: doc, selection: { isEmpty: true, active: { line: 2 }, start: { line: 2 }, end: { line: 2 } } };

const vscode = {
  env: { language: 'en', clipboard: { writeText: async () => {} } },
  window: {
    activeTextEditor: editor,
    createStatusBarItem: () => ({ show() {}, dispose() {} }),
    showInformationMessage: async m => { messages.push(m); },
    showWarningMessage: async m => { messages.push(m); },
    showErrorMessage: async m => { messages.push('error: ' + m); },
    setStatusBarMessage: () => {},
    withProgress: (options, task) => task(),
    createWebviewPanel: () => ({
      webview: { set html(v) { html = v; }, get html() { return html; }, onDidReceiveMessage: cb => { onPanelMessage = cb; } },
      onDidDispose: () => {}, reveal: () => {},
    }),
    onDidChangeActiveTextEditor: () => ({ dispose() {} }),
    onDidChangeTextEditorSelection: () => ({ dispose() {} }),
    registerTreeDataProvider: (id, provider) => { trees[id] = provider; return { dispose() {} }; },
    showInputBox: async () => undefined,
    showQuickPick: async items => { picks.push(items); return undefined; },
  },
  workspace: {
    getConfiguration: () => ({ get: (key, def) => (key === 'socketPath' ? process.env.JARVIS_SOCKET : def) }),
    workspaceFolders: [],
    getWorkspaceFolder: () => ({ name: 'demo' }),
    asRelativePath: () => 'main.cpp',
    textDocuments: [doc],
    onDidChangeTextDocument: () => ({ dispose() {} }),
    onDidChangeWorkspaceFolders: () => ({ dispose() {} }),
    findFiles: async () => [],
    registerTextDocumentContentProvider: () => ({ dispose() {} }),
  },
  languages: {
    getDiagnostics: () => diagnostics, onDidChangeDiagnostics: () => ({ dispose() {} }),
    registerCodeActionsProvider: (selector, provider) => { lightbulb = provider; return { dispose() {} }; },
  },
  commands: { registerCommand: (name, fn) => { commands[name] = fn; return { dispose() {} }; } },
  StatusBarAlignment: { Right: 2 }, ViewColumn: { Beside: -2 }, ProgressLocation: { Notification: 15 },
  DiagnosticSeverity: { Error: 0, Warning: 1 }, Range: function () {}, RelativePattern: function () {},
  EventEmitter: class { constructor() { this.event = () => ({ dispose() {} }); } fire() {} },
  TreeItem: class { constructor(label) { this.label = label; } },
  ThemeIcon: class { constructor(id) { this.id = id; } },
  CodeAction: class { constructor(title, kind) { this.title = title; this.kind = kind; } },
  CodeActionKind: { QuickFix: 'quickfix' },
  Uri: { parse: s => ({ toString: () => s }) },
};
const load = Module._load;
Module._load = function (request, ...rest) { return request === 'vscode' ? vscode : load.call(this, request, ...rest); };

const extension = require('../vscode/extension');
const context = { subscriptions: [] };

(async () => {
  extension.activate(context);
  for (let i = 0; i < 50 && !html && !messages.length; i++) await new Promise(r => setTimeout(r, 100));
  await new Promise(r => setTimeout(r, 500)); // connected + hello
  await commands['jarvis.error']();
  assert.ok(html.includes('Include the header'), 'answer rendered from the lesson: ' + html.slice(0, 300) + messages.join('|'));
  assert.ok(html.includes("Content-Security-Policy"), 'webview locked down');
  assert.ok(html.includes('Explain the errors') && html.includes('main.cpp'), 'answer header names the action and the file');
  const titles = trees['jarvis.actions'].getChildren().map(i => i.label);
  assert.ok(titles.includes('Jarvis connected') && titles.includes('Review the file'), 'side bar lists the state and the actions: ' + titles);
  assert.strictEqual(trees['jarvis.history'].getChildren().length, 1, 'the answer is in the history');
  const fixes = lightbulb.provideCodeActions(doc, {}, { diagnostics });
  assert.ok(fixes.length === 1 && fixes[0].command.command === 'jarvis.fixDiagnostic', 'light bulb offers Jarvis on an error');
  await commands['jarvis.menu']();
  assert.ok(picks[0].some(i => i.mode === 'fix' && i.description), 'menu explains what an action needs');
  await onPanelMessage({ action: 'good' });
  assert.ok(messages.some(m => /remember|запомнил/i.test(m)), 'rating acknowledged: ' + messages.join('|'));
  extension.deactivate();
  console.log('PASS: VS Code extension client against jarvisd');
  process.exit(0);
})().catch(e => { console.error(e); process.exit(1); });
