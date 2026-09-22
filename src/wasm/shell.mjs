/*
 * Copyright (c) 2025 OceanBase.
 *
 * Licensed under the Apache License, Version 2.0 (the "License");
 * you may not use this file except in compliance with the License.
 * You may obtain a copy of the License at
 *
 *     http://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an "AS IS" BASIS,
 * WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 * See the License for the specific language governing permissions and
 * limitations under the License.
 */
import {CommandHistory, databaseAfterStatement, interactivePrompt, takeInteractiveStatement, takeStatement} from './shell-sql.mjs';
import {EXAMPLES} from './shell-examples.mjs';
import {TableFormatter, VerticalFormatter} from './shell-format.mjs';
import {ENGINE_VERSION} from './engine-version.mjs';

const MAX_RESULT_CHARACTERS = 100000;
const MAX_TRANSCRIPT_CHARACTERS = 1000000;
const STORAGE_KEY = 'seekdb-shell-storage-mode';
const QUERY_PREVIEW = {maxRows: 500, maxColumns: 100, maxCells: 20000, maxCellBytes: 480, maxBytes: 400000};
const element = id => document.getElementById(id);
const input = element('sql');
const transcript = element('transcript');
const outputLengths = new WeakMap();
let transcriptCharacters = 0;
const terminalScroll = element('terminal-scroll');
const exampleButtons = element('example-menu').querySelectorAll('button');
const menus = document.querySelectorAll('.menu');
const versionLabel = element('version-label');
versionLabel.textContent = ENGINE_VERSION;
const decoder = new TextDecoder();
const history = new CommandHistory();
const decode = value => value === null ? null : decoder.decode(value);
let database;
let session;
let SqlError;
let state = 'loading';
let currentDatabase = 'test';
let sqlOptions = {};
let controller;
let operationStarted;
let activityTimer;
let followOutput = true;
let activeEntry;
const persistentSupported = typeof navigator.storage?.getDirectory === 'function' && typeof navigator.locks?.request === 'function';
let storage = 'memory';
let pendingStorage;
let persistentClearPending = false;
let inputBuffer = '';
let inputEntry;
let delimiter = ';';
let acceptingInput = false;
let connectionId;
let activeQuery;
let cancellation;
let cancelRequested = false;
let cancelPending = false;
let terminalPointerActive = false;

function node(tag, className, text) {
  const result = document.createElement(tag);
  if (className) result.className = className;
  if (text !== undefined) result.textContent = shortenText(text === null ? '' : String(text), MAX_RESULT_CHARACTERS - 1);
  return result;
}

function shortenText(text, limit) {
  if (text.length <= limit) return text;
  let end = limit;
  if (end > 0 && /[\ud800-\udbff]/u.test(text[end - 1]) && /[\udc00-\udfff]/u.test(text[end])) end--;
  return `${text.slice(0, end)}…`;
}

function elapsed(start) {
  return `${((performance.now() - start) / 1000).toFixed(3)} s`;
}

function setState(next, message) {
  state = next;
  const starting = ['loading', 'closing'].includes(next);
  element('status-dot').dataset.state = starting ? 'loading' : next;
  element('status-text').textContent = message;
  input.disabled = !['ready', 'running'].includes(next);
  input.readOnly = next === 'running';
  input.placeholder = starting ? 'loading…' : next === 'closed' || next === 'error' ? 'Choose New Instance to start a database' : '';
  for (const button of exampleButtons) button.disabled = next !== 'ready';
  element('database-name').textContent = database && !starting ? `seekdb [${currentDatabase ?? '(none)'}]` : 'seekdb';
  updatePrompt();
  element('status-dot').title = message;
  updateStorage();
  if (['loading', 'running', 'closing'].includes(next)) {
    if (activityTimer === undefined) {
      operationStarted = performance.now();
      element('activity-time').textContent = elapsed(operationStarted);
      activityTimer = setInterval(() => { element('activity-time').textContent = elapsed(operationStarted); }, 100);
    }
  } else {
    clearInterval(activityTimer);
    activityTimer = undefined;
    element('activity-time').textContent = '';
  }
}

function appendOutput(output) {
  transcript.append(output);
  updateOutput(output);
  scrollOutput();
}

function updateOutput(output) {
  if (output.parentNode !== transcript) return;
  let characters = output.textContent.length;
  while (characters > MAX_TRANSCRIPT_CHARACTERS && output.children.length > 1) {
    const first = output.firstElementChild;
    characters -= first.textContent.length;
    first.remove();
  }
  transcriptCharacters += characters - (outputLengths.get(output) ?? 0);
  outputLengths.set(output, characters);
  while (transcript.children.length > 40 || transcriptCharacters > MAX_TRANSCRIPT_CHARACTERS) {
    const first = transcript.firstElementChild;
    transcriptCharacters -= outputLengths.get(first) ?? 0;
    outputLengths.delete(first);
    first.remove();
  }
}

function replaceOutput(...outputs) {
  transcript.replaceChildren(...outputs);
  transcriptCharacters = 0;
  for (const output of outputs) {
    outputLengths.set(output, 0);
    updateOutput(output);
  }
}

function scrollOutput() {
  if (followOutput) terminalScroll.scrollTop = terminalScroll.scrollHeight;
}

function notice(message, error = false) {
  appendOutput(node('div', `notice${error ? ' error' : ''}`, message));
}

function resizeInput() {
  input.style.height = 'auto';
  input.style.height = `${Math.min(190, Math.max(20, input.scrollHeight))}px`;
  scrollOutput();
}

function setInput(value) {
  input.value = value;
  resizeInput();
  input.focus();
  input.setSelectionRange(value.length, value.length);
}

function updatePrompt() {
  const continuation = interactivePrompt(inputBuffer, {...sqlOptions, delimiter});
  element('prompt').textContent = continuation ? continuation.padStart(6) : `${element('database-name').textContent}>`;
}

function resetInputBuffer() {
  inputBuffer = '';
  inputEntry?.classList.remove('pending');
  inputEntry = undefined;
  updatePrompt();
}

function echoInput(line) {
  if (inputEntry?.parentNode !== transcript) {
    inputEntry = node('article', 'entry pending');
    appendOutput(inputEntry);
  }
  const command = node('pre', 'command-text');
  command.append(node('span', 'command-prompt', element('prompt').textContent), document.createTextNode(` ${shortenText(line, MAX_RESULT_CHARACTERS - 1)}`));
  inputEntry.append(command);
  updateOutput(inputEntry);
  scrollOutput();
}

function clearInput() {
  if (input.value || inputBuffer) echoInput(`${input.value}^C`);
  resetInputBuffer();
  history.resetNavigation();
  setInput('');
}

function commandOutput(sql) {
  const entry = node('article', 'entry');
  const line = node('div', 'command-line');
  const command = node('pre', 'command-text');
  const displaySql = shortenText(shortenText(sql, MAX_RESULT_CHARACTERS - 1).replaceAll('\n', '\n    -> '), MAX_RESULT_CHARACTERS - 1);
  command.append(node('span', 'command-prompt', element('prompt').textContent), document.createTextNode(` ${displaySql};`));
  line.append(command);
  entry.append(line);
  appendOutput(entry);
  return entry;
}

function updateContext(sql, event) {
  currentDatabase = databaseAfterStatement(sql, currentDatabase, sqlOptions);
  if ('affectedRows' in event) sqlOptions.noBackslashEscapes = Boolean(event.status & 512);
  element('database-name').textContent = `seekdb [${currentDatabase ?? '(none)'}]`;
  updatePrompt();
}

async function executeStatement(sql, signal, {entry = commandOutput(sql), vertical = false} = {}) {
  activeEntry = entry;
  activeQuery = {connectionId};
  const started = performance.now();
  let body;
  let table;
  let rows = 0n;
  let displayed = 0;
  let characters = 0;
  let limited = false;
  let textShortened = false;
  let columns = 0;
  try {
    for await (const event of session.query(sql, {signal, preview: QUERY_PREVIEW})) {
      if (event.kind === 'columns') {
        rows = 0n;
        displayed = 0;
        characters = 0;
        columns = event.columns.length;
        limited = columns > 100;
        textShortened = false;
        const resultColumns = event.columns.slice(0, 100).map(column => {
          const name = decode(column.name);
          if (name.length <= 120) return name;
          textShortened = true;
          return shortenText(name, 120);
        });
        const Formatter = vertical ? VerticalFormatter : TableFormatter;
        table = new Formatter(resultColumns, {maxCharacters: MAX_RESULT_CHARACTERS});
        body = node('pre', 'result-table');
        body.setAttribute('aria-label', 'Query result');
        body.tabIndex = 0;
        entry.append(body);
      } else if (event.kind === 'row') {
        rows += 1n;
        if (table.limited || displayed >= 500 || characters >= 100000 || displayed * Math.min(columns, 100) >= 20000) {
          limited = true;
          continue;
        }
        const row = [];
        for (const value of event.values.slice(0, 100)) {
          let text = decode(value);
          if (text !== null && text.length > 120) {
            text = shortenText(text, 120);
            textShortened = true;
          }
          characters += text === null ? 4 : text.length;
          row.push(text);
        }
        if (!table.append(row)) { limited = true; continue; }
        displayed++;
        if (displayed % 50 === 0) {
          body.textContent = table.format();
          updateOutput(entry);
          scrollOutput();
        }
      } else if (event.kind === 'complete') {
        if (event.preview) {
          rows = event.preview.rowCount;
          limited ||= event.preview.limited;
          textShortened ||= event.preview.truncated;
        }
        updateContext(sql, event);
        if (body) {
          const text = displayed ? table.format() : '';
          if (body.textContent !== text) body.textContent = text;
          body.hidden = rows === 0n;
        }
        if (limited) entry.append(node('p', 'result-limit', `Display limited: ${displayed} of ${rows} rows, ${Math.min(columns, 100)} of ${columns} columns. The full response was consumed.`));
        if (textShortened) entry.append(node('p', 'result-limit', 'Long values and column names are shortened to 120 characters for display.'));
        const summary = node('div', 'query-summary');
        const affected = event.affectedRows ?? 0n;
        const count = body ? rows === 0n ? 'Empty set' : `${rows} ${rows === 1n ? 'row' : 'rows'} in set` : `Query OK, ${affected} ${affected === 1n ? 'row' : 'rows'} affected`;
        const warnings = event.warnings ? ` · ${event.warnings} ${event.warnings === 1 ? 'warning' : 'warnings'}` : '';
        summary.append(node('span', '', `${count}${warnings}`), node('span', 'timing', `(${elapsed(started).replace(' s', ' sec')})`));
        entry.append(summary);
        updateOutput(entry);
        body = undefined;
        scrollOutput();
      }
    }
  } catch (error) {
    if (body && displayed) body.textContent = table.format();
    const message = signal.aborted ? 'Query cancelled. Remaining statements were not run.' : error instanceof SqlError ? `ERROR ${error.code} (${error.sqlState}): ${error.message}` : error.message;
    entry.append(node('div', 'entry-error', message));
    updateOutput(entry);
    throw error;
  } finally {
    activeQuery = undefined;
    activeEntry = undefined;
    scrollOutput();
  }
}

async function connectDefaultSession() {
  try {
    return await database.connect({database: 'test'});
  } catch (error) {
    if (!(error instanceof SqlError) || error.code !== 1049) throw error;
  }
  const connection = await database.connect();
  try {
    for await (const event of connection.query('CREATE DATABASE IF NOT EXISTS `test`')) {}
    for await (const event of connection.query('USE `test`')) {}
    return connection;
  } catch (error) {
    await connection.close().catch(() => {});
    throw error;
  }
}

async function reconnectSession() {
  await session?.close().catch(() => {});
  session = undefined;
  try {
    session = await (currentDatabase ? database.connect({database: currentDatabase}) : connectDefaultSession());
  } catch (error) {
    if (!(error instanceof SqlError) || error.code !== 1049) throw error;
    currentDatabase = 'test';
    session = await connectDefaultSession();
  }
  if (currentDatabase === null) currentDatabase = 'test';
  sqlOptions = {};
  await updateVersion();
}

async function submit(sql = input.value) {
  if (state !== 'ready' || !sql.trim()) return;
  const command = sql.trim().replace(/;$/, '').toLowerCase();
  if (command === '\\clear') { clearOutput(); setInput(''); return; }
  if (command === '\\tables') sql = 'SHOW TABLES;';
  if (command === '\\databases') sql = 'SHOW DATABASES;';
  resetInputBuffer();
  history.push(sql);
  setInput('');
  followOutput = true;
  scrollOutput();
  controller = new AbortController();
  cancelRequested = false;
  const signal = controller.signal;
  setState('running', 'Running SQL…');
  let remaining = sql;
  let statements = 0;
  let failed = false;
  try {
    for (;;) {
      if (cancelRequested) break;
      signal.throwIfAborted();
      const next = takeStatement(remaining, sqlOptions);
      if (!next) break;
      await executeStatement(next.statement, signal);
      statements++;
      remaining = next.rest;
    }
  } catch (error) {
    failed = true;
    if (error instanceof SyntaxError) notice(`${error.message}\nRemaining SQL was not run. Use ↑ to edit the original input.`, true);
    if (signal.aborted || !(error instanceof SqlError || error instanceof SyntaxError)) {
      try {
        await reconnectSession();
        notice('Session reconnected. Uncommitted changes were rolled back; session settings were reset.');
      } catch (reconnectError) {
        await database?.close().catch(() => {});
        database = undefined;
        session = undefined;
        notice(`The database stopped: ${reconnectError.message}\nChoose New Instance to start again.`, true);
      }
    }
  } finally {
    await cancellation;
    cancellation = undefined;
    controller = undefined;
    setState(database ? 'ready' : 'error', database ? failed ? 'Ready · previous batch stopped before completion' : `Ready · ${statements} ${statements === 1 ? 'statement' : 'statements'} completed` : 'Database unavailable');
    input.focus();
  }
}

async function submitLine() {
  if (state !== 'ready' || acceptingInput) return;
  acceptingInput = true;
  const lines = input.value.replace(/\r\n?/g, '\n').split('\n');
  setInput('');
  followOutput = true;
  controller = new AbortController();
  cancelRequested = false;
  try {
    for (const line of lines) {
      if (cancelRequested) break;
      const emptyBuffer = !inputBuffer.trim();
      echoInput(line);
      history.push(line);
      const textCommand = emptyBuffer ? line.trim().replace(/;$/, '').toLowerCase() : '';
      const delimiterCommand = emptyBuffer && line.trim().match(/^(?:delimiter|\\d)(?:\s+([\s\S]*))?$/iu);
      if (delimiterCommand) {
        const value = (delimiterCommand[1] ?? '').trim().replace(/^(['"])([\s\S]*)\1$/u, '$2');
        if (!value || /[\s\\]/u.test(value)) notice('ERROR: DELIMITER requires a nonempty value without whitespace or backslashes.', true);
        else delimiter = value;
        resetInputBuffer();
        continue;
      }
      if (textCommand === 'clear') { resetInputBuffer(); continue; }
      if (textCommand === '\\clear') { resetInputBuffer(); clearOutput(); continue; }
      let directSql = textCommand === '\\tables' ? 'SHOW TABLES' : textCommand === '\\databases' ? 'SHOW DATABASES' : undefined;
      if (emptyBuffer && /^use\s/iu.test(line.trim()) && interactivePrompt(line, {...sqlOptions, delimiter}) === '->'
        && !takeInteractiveStatement(line, {...sqlOptions, delimiter})) directSql = line.trim();
      inputBuffer += `${line}\n`;
      for (;;) {
        if (cancelRequested) break;
        const next = directSql ? {statement: directSql, rest: '', terminator: ''}
          : takeInteractiveStatement(inputBuffer, {...sqlOptions, delimiter});
        directSql = undefined;
        if (!next) break;
        if (next.command === 'clear') {
          inputBuffer = next.rest;
          continue;
        }
        if (next.command === 'print') {
          inputEntry.append(node('pre', 'command-text', next.statement));
          updateOutput(inputEntry);
          inputBuffer = next.statement + next.rest;
          continue;
        }
        inputBuffer = next.rest;
        if (!next.statement) {
          inputEntry.append(node('div', 'entry-error', 'ERROR: No query specified'));
          updateOutput(inputEntry);
          continue;
        }
        if (next.statement.includes('\n')) history.push(next.statement + next.terminator);
        setState('running', 'Running SQL…');
        try {
          await executeStatement(next.statement, controller.signal, {entry: inputEntry, vertical: next.vertical});
        } catch (error) {
          if (!(error instanceof SqlError)) throw error;
        }
      }
      if (!interactivePrompt(inputBuffer, {...sqlOptions, delimiter})) resetInputBuffer();
      updatePrompt();
    }
  } catch (error) {
    resetInputBuffer();
    if (!(error instanceof SqlError)) {
      try {
        await reconnectSession();
        notice('Session reconnected. Uncommitted changes were rolled back; session settings were reset.');
      } catch (reconnectError) {
        await database?.close().catch(() => {});
        database = undefined;
        session = undefined;
        notice(`The database stopped: ${reconnectError.message}\nChoose New Instance to start again.`, true);
      }
    }
  } finally {
    await cancellation;
    cancellation = undefined;
    if (cancelRequested) resetInputBuffer();
    controller = undefined;
    acceptingInput = false;
    setState(database ? 'ready' : 'error', database ? 'Ready · root · SQL runs locally in a Web Worker' : 'Database unavailable');
    input.focus();
  }
}

function cancelQuery() {
  if (!controller || cancelPending) return;
  cancelRequested = true;
  const query = activeQuery;
  if (!query) return;
  cancelPending = true;
  element('status-text').textContent = 'Interrupting query…';
  cancellation = (async () => {
    let control;
    try {
      control = await database.connect();
      if (activeQuery !== query) return;
      for await (const event of control.query(`KILL QUERY ${query.connectionId}`)) {}
    } catch (error) {
      if (activeQuery === query) notice(`Could not interrupt the query: ${error.message}`, true);
    } finally {
      await control?.close().catch(() => {});
      cancelPending = false;
    }
  })();
}

async function updateVersion() {
  for await (const event of session.query('SELECT VERSION(), CONNECTION_ID()')) {
    if (event.kind !== 'row') continue;
    const engineVersion = decode(event.values[0]);
    connectionId = decode(event.values[1]);
    if (!/^\d+$/u.test(connectionId)) throw new Error('The server returned an invalid connection ID.');
    if (engineVersion) {
      versionLabel.textContent = engineVersion.match(/seekdb-(v\S+)/i)?.[1] ?? engineVersion;
      versionLabel.title = `WebAssembly · ${engineVersion}`;
    }
  }
}

async function openDatabase() {
  setState('loading', 'Loading WebAssembly and starting seekdb. The first start can take a few seconds…');
  try {
    if (!globalThis.isSecureContext || !globalThis.crossOriginIsolated || typeof SharedArrayBuffer === 'undefined') {
      throw new Error('WebAssembly threads require a secure context with cross-origin isolation. Use tools/wasm/serve-shell.py on localhost, or serve over HTTPS with COOP: same-origin and COEP: require-corp.');
    }
    const module = await import('./database.mjs');
    SqlError = module.SqlError;
    database = await module.Database.open({moduleURL: new URL('./seekdb_wasm_database.mjs', import.meta.url), wasmURL: new URL('./seekdb_wasm_database.wasm', import.meta.url), storage});
    currentDatabase = 'test';
    session = await connectDefaultSession();
    await updateVersion();
    sqlOptions = {};
    try { localStorage.setItem(STORAGE_KEY, storage); } catch {}
    setState('ready', storage === 'opfs' ? 'Ready · root · data is kept in this browser' : 'Ready · root · SQL runs locally in a Web Worker');
    input.focus();
    return true;
  } catch (error) {
    await database?.close().catch(() => {});
    database = undefined;
    session = undefined;
    const hint = storage !== 'opfs' ? 'Check that the compiled .mjs and .wasm files are available, then choose New Instance.'
      : /another tab/.test(error.message) ? 'Close it there first and reload this page, or choose Memory from New Instance.'
      : /locked/.test(error.message) ? 'Another page may still be using the stored database. Close it there, then reload this page.'
      : 'Reload to reopen the stored database. Choose New Instance only to clear its data and start over.';
    notice(`Could not start seekdb: ${error.message}\n${hint}`, true);
    setState('error', 'Startup failed');
    return false;
  }
}

function updateStorage() {
  const persistent = (pendingStorage ?? storage) === 'opfs';
  const busy = ['loading', 'running', 'closing'].includes(state);
  element('storage-badge').textContent = persistent ? 'opfs://' : 'memory://';
  element('memory-button').disabled = busy;
  element('opfs-button').disabled = busy || !persistentSupported;
}

async function initialStorage() {
  let saved;
  try { saved = localStorage.getItem(STORAGE_KEY); } catch {}
  if (saved === 'opfs' && !persistentSupported) throw new Error('Reopening OPFS requires OPFS and Web Locks support in this browser.');
  if (saved === 'memory' || saved === 'opfs') return saved;
  if (!persistentSupported) return 'memory';
  try {
    const root = await navigator.storage.getDirectory();
    const store = await root.getDirectoryHandle('store');
    const sstable = await store.getDirectoryHandle('sstable');
    await sstable.getFileHandle('meta.db');
    return 'opfs';
  } catch (error) {
    if (error.name === 'NotFoundError') return 'memory';
    throw error;
  }
}

function closeMenus() {
  for (const menu of menus) menu.open = false;
}

async function createInstance(mode) {
  closeMenus();
  if (['loading', 'running', 'closing'].includes(state)) return;
  pendingStorage = mode;
  resetInputBuffer();
  delimiter = ';';
  persistentClearPending ||= Boolean(database && storage === 'opfs');
  replaceOutput();
  followOutput = true;
  setInput('');
  try {
    let clearedPersistent = false;
    if (database) {
      setState('closing', 'Discarding the previous database…');
      session = undefined;
      await database.discard();
      database = undefined;
      clearedPersistent = storage === 'opfs';
      if (clearedPersistent) persistentClearPending = false;
    }
    setState('loading', 'Clearing data and creating a new instance…');
    if ((mode === 'opfs' || persistentClearPending) && !clearedPersistent) {
      const {Database} = await import('./database.mjs');
      await Database.clearPersistentStorage();
      persistentClearPending = false;
    }
    storage = mode;
    await openDatabase();
  } catch (error) {
    notice(`Could not clear the stored database: ${error.message}\nClose any other page using this database, then try New Instance again.`, true);
    setState('error', 'Could not create a new instance');
  } finally {
    pendingStorage = undefined;
    updateStorage();
  }
}

function clearOutput() {
  const retained = activeEntry ?? inputEntry;
  replaceOutput(...(retained ? [retained] : []));
  followOutput = true;
  scrollOutput();
  if (state === 'ready') input.focus();
}

function focusInput() {
  if (!input.disabled) input.focus({preventScroll: true});
}

input.addEventListener('input', () => {
  history.resetNavigation();
  resizeInput();
});
terminalScroll.addEventListener('scroll', () => {
  followOutput = terminalScroll.scrollHeight - terminalScroll.scrollTop - terminalScroll.clientHeight < 60;
});
terminalScroll.addEventListener('pointerdown', () => { terminalPointerActive = true; });
document.addEventListener('pointerup', () => { terminalPointerActive = false; });
document.addEventListener('pointercancel', () => { terminalPointerActive = false; });
window.addEventListener('blur', () => { terminalPointerActive = false; });
terminalScroll.addEventListener('focus', () => {
  if (!terminalPointerActive && !window.getSelection().toString()) focusInput();
});
terminalScroll.addEventListener('click', event => {
  if (window.getSelection().toString() || event.target.closest('.result-table')) return;
  focusInput();
});
input.addEventListener('keydown', event => {
  if (event.isComposing) return;
  if (state !== 'ready') return;
  if (event.key === 'Enter') {
    event.preventDefault();
    void submitLine();
  } else if (event.key === 'ArrowUp' && !input.value.slice(0, input.selectionStart).includes('\n') && input.selectionStart === input.selectionEnd) {
    const previous = history.previous(input.value);
    if (previous !== undefined) { event.preventDefault(); setInput(previous); }
  } else if (event.key === 'ArrowDown' && !input.value.slice(input.selectionEnd).includes('\n') && input.selectionStart === input.selectionEnd) {
    const next = history.next();
    if (next !== undefined) { event.preventDefault(); setInput(next); }
  }
});
document.addEventListener('keydown', event => {
  if (event.key === 'Tab') { event.preventDefault(); focusInput(); return; }
  if (event.isComposing) return;
  if (event.ctrlKey && event.key.toLowerCase() === 'l') { event.preventDefault(); clearOutput(); }
  if (event.ctrlKey && event.key.toLowerCase() === 'c' && !window.getSelection().toString() && ['ready', 'running'].includes(state)) {
    event.preventDefault();
    if (state === 'running') cancelQuery();
    else clearInput();
  }
});
element('query-form').addEventListener('submit', event => { event.preventDefault(); void submitLine(); });
for (const button of exampleButtons) {
  button.addEventListener('click', () => {
    closeMenus();
    void submit(EXAMPLES[button.dataset.example]);
  });
}
element('clear-button').addEventListener('click', clearOutput);
element('memory-button').addEventListener('click', () => void createInstance('memory'));
element('opfs-button').addEventListener('click', () => void createInstance('opfs'));
document.addEventListener('click', event => {
  for (const menu of menus) if (!menu.contains(event.target)) menu.open = false;
});
window.addEventListener('pagehide', () => { void database?.close().catch(() => {}); });

setState('loading', 'Checking for a stored database…');
try {
  storage = await initialStorage();
  await openDatabase();
} catch (error) {
  notice(`Could not inspect stored data: ${error.message}\nReload to retry, or choose Memory from New Instance.`, true);
  setState('error', 'Could not inspect stored data');
}
