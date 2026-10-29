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

const engineURL = name => new URL(`./${name}`, import.meta.url);
const MODULE_URL = engineURL('seekdb_wasm_database.mjs');
const WASM_URL = engineURL('seekdb_wasm_database.wasm');
const engineCompilation = globalThis.crossOriginIsolated ? compileEngine() : undefined;
engineCompilation?.catch(() => {});

async function compileEngine() {
  const response = await fetch(WASM_URL);
  if (!response.ok) throw new Error(`Could not load WebAssembly: HTTP ${response.status}`);
  const streaming = typeof WebAssembly.compileStreaming === 'function'
    && response.headers.get('Content-Type')?.split(';', 1)[0].trim() === 'application/wasm';
  return streaming ? WebAssembly.compileStreaming(response) : WebAssembly.compile(await response.arrayBuffer());
}

const MAX_RESULT_CHARACTERS = 100000;
const MAX_TRANSCRIPT_CHARACTERS = 1000000;
const STORAGE_KEY = 'seekdb-shell-storage-mode';
const QUERY_PREVIEW = {maxRows: 500, maxColumns: 100, maxCells: 20000, maxCellBytes: 480, maxBytes: 400000};
const STARTUP_STAGES = ['engine', 'session', 'ready'];
const ANALYTICS_CAMPAIGN = 'web_shell_launch';
const ATTRIBUTION_PARAMETERS = ['utm_source', 'utm_medium', 'utm_campaign'];
const locationParameters = new URLSearchParams(location.search);
const launchAttribution = Object.fromEntries(ATTRIBUTION_PARAMETERS.flatMap(name => {
  const value = locationParameters.get(name)?.trim().slice(0, 100);
  return value ? [[name, value]] : [];
}));
const element = id => document.getElementById(id);
const input = element('sql');
const transcript = element('transcript');
const outputLengths = new WeakMap();
let transcriptCharacters = 0;
const terminalScroll = element('terminal-scroll');
const exampleButtons = document.querySelectorAll('[data-example]');
const menus = document.querySelectorAll('.menu');
const versionLabel = element('version-label');
versionLabel.textContent = ENGINE_VERSION;
const decoder = new TextDecoder();
const history = new CommandHistory();
const decode = value => value === null ? null : decoder.decode(value);
let database;
let Database;
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
// Reset Local Database removes only the engine's seekdb directory in OPFS.
// Resolve explicit URLs, saved choices and legacy OPFS data before opening.
let storage = 'memory';
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
let databaseOpened = false;
let readyTracked = false;
let firstQuerySucceeded = false;
let resetBlocked = false;

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

function setStartupStage(stage, title, {complete = false} = {}) {
  const status = element('runtime-status');
  status.dataset.stage = stage;
  element('status-stage').textContent = title;
  const current = STARTUP_STAGES.indexOf(stage);
  for (const step of status.querySelectorAll('[data-startup-step]')) {
    const index = STARTUP_STAGES.indexOf(step.dataset.startupStep);
    if (current < 0 || index > current) delete step.dataset.state;
    else step.dataset.state = index < current || complete ? 'complete' : 'active';
  }
}

function trackEvent(eventName, parameters = {}) {
  const payload = {campaign: ANALYTICS_CAMPAIGN, ...launchAttribution, ...parameters};
  const customName = eventName.replace(/^shell_/u, '').replaceAll('_', '-');
  window.dispatchEvent(new CustomEvent(`seekdb-shell:${customName}`, {detail: payload}));
  try {
    if (typeof window.gtag === 'function') window.gtag('event', eventName, payload);
    else {
      window.dataLayer ??= [];
      window.dataLayer.push(['event', eventName, payload]);
    }
    window._hmt?.push(['_trackEvent', 'seekdb_shell', eventName, String(parameters.source ?? '')]);
  } catch (error) {
    console.warn('Could not publish analytics event', error);
  }
}

function trackFirstQuerySuccess(source, started) {
  if (firstQuerySucceeded) return;
  firstQuerySucceeded = true;
  trackEvent('shell_first_query_success', {
    source,
    duration_ms: Math.round(performance.now() - started),
    engine_version: versionLabel.textContent || ENGINE_VERSION,
    storage,
  });
}

function persistentStorage() {
  return storage === 'opfs';
}

function resetActionLabel() {
  return persistentStorage() ? 'Reset Local Database' : 'New Memory Instance';
}

function readyStorageMessage() {
  return persistentStorage()
    ? 'Ready · root · database saved in this browser'
    : 'Ready · root · temporary in-memory database';
}

function setState(next, message) {
  state = next;
  const starting = ['loading', 'closing'].includes(next);
  element('runtime-status').dataset.state = next;
  if (next === 'running') element('status-stage').textContent = 'Running query';
  else if (next === 'closing') element('status-stage').textContent = 'Resetting session';
  else if (next === 'ready') element('status-stage').textContent = 'Ready';
  else if (next === 'error') element('status-stage').textContent = 'Startup failed';
  element('status-dot').dataset.state = starting ? 'loading' : next;
  element('status-text').textContent = message;
  input.disabled = !['ready', 'running'].includes(next);
  input.readOnly = next === 'running';
  input.placeholder = starting
    ? 'loading…'
    : next === 'closed' || next === 'error'
      ? resetBlocked ? 'Close the other tab or window, then refresh' : `Use ${resetActionLabel()} to start over`
      : '';
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

async function executeStatement(sql, signal, {entry = commandOutput(sql), vertical = false, source = 'editor'} = {}) {
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
        trackFirstQuerySuccess(source, started);
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

async function submit(sql = input.value, {source = 'editor'} = {}) {
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
      await executeStatement(next.statement, signal, {source});
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
        notice(`The database stopped: ${reconnectError.message}\nUse ${resetActionLabel()} to start again.`, true);
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
        notice(`The database stopped: ${reconnectError.message}\nUse ${resetActionLabel()} to start again.`, true);
      }
    }
  } finally {
    await cancellation;
    cancellation = undefined;
    if (cancelRequested) resetInputBuffer();
    controller = undefined;
    acceptingInput = false;
    setState(database ? 'ready' : 'error', database ? readyStorageMessage() : 'Database unavailable');
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

async function initialStorage() {
  const requested = locationParameters.get('storage');
  if (requested === 'memory' || requested === 'opfs') return requested;
  let saved;
  try { saved = localStorage.getItem(STORAGE_KEY); } catch {}
  if (saved === 'memory' || saved === 'opfs') return saved;
  if (typeof navigator.storage?.getDirectory !== 'function') return 'memory';
  try {
    // Preserve databases created before the shell remembered a storage choice.
    const root = await navigator.storage.getDirectory();
    const data = await root.getDirectoryHandle('seekdb');
    const store = await data.getDirectoryHandle('store');
    const local = await store.getDirectoryHandle('local');
    await local.getFileHandle('data');
    return 'opfs';
  } catch (error) {
    if (error.name === 'NotFoundError') return 'memory';
    throw error;
  }
}

async function openDatabase() {
  const started = performance.now();
  const firstStart = !databaseOpened;
  resetBlocked = false;
  setStartupStage('engine', 'Loading engine');
  setState('loading', firstStart
    ? 'Loading, compiling, and starting the seekdb WebAssembly engine (~57 MB uncompressed)…'
    : persistentStorage()
      ? 'Starting the browser-persistent seekdb database…'
      : 'Starting a fresh in-memory seekdb database…');
  try {
    if (!globalThis.isSecureContext || !globalThis.crossOriginIsolated || typeof SharedArrayBuffer === 'undefined') {
      throw new Error('WebAssembly threads require a secure context with cross-origin isolation. Use tools/wasm/serve-shell.py on localhost, or serve over HTTPS with COOP: same-origin and COEP: require-corp.');
    }
    if (firstStart) {
      storage = await initialStorage();
      updateStorage();
    }
    const module = await import('./database.mjs');
    Database = module.Database;
    SqlError = module.SqlError;
    database = await Database.open({moduleURL: MODULE_URL, wasmURL: WASM_URL, storage, compiledModule: engineCompilation});
    const connectStarted = performance.now();
    setStartupStage('session', 'Opening SQL session');
    setState('loading', persistentStorage()
      ? 'Opening the database stored in this browser and starting a local SQL session…'
      : 'Creating a temporary in-memory database and starting a local SQL session…');
    currentDatabase = 'test';
    session = await connectDefaultSession();
    const versionStarted = performance.now();
    await updateVersion();
    const versionFinished = performance.now();
    sqlOptions = {};
    databaseOpened = true;
    try { localStorage.setItem(STORAGE_KEY, storage); } catch {}
    setStartupStage('ready', 'Ready', {complete: true});
    setState('ready', readyStorageMessage());
    if (!readyTracked) {
      readyTracked = true;
      trackEvent('shell_engine_ready', {
        startup_ms: Math.round(performance.now() - started),
        engine_version: versionLabel.textContent || ENGINE_VERSION,
        storage,
      });
    }
    input.focus();
    performance.measure('seekdb-shell-startup', {
      start: started,
      end: performance.now(),
      detail: {storage, ...database.startupTimings,
        connectMs: versionStarted - connectStarted,
        versionQueryMs: versionFinished - versionStarted},
    });
    return true;
  } catch (error) {
    await database?.close().catch(() => {});
    database = undefined;
    session = undefined;
    const lockConflict = error.message === 'The persistent database is already open in another tab or window';
    resetBlocked = lockConflict;
    setStartupStage('error', 'Startup failed');
    const hint = lockConflict
      ? 'Close the other seekdb Shell tab or window, then refresh this page. Your stored database has not been changed.'
      : persistentStorage()
        ? 'Check that the compiled .mjs and .wasm files are available, then refresh. You can choose Temporary (Memory) to continue without changing stored data. Only use Reset Local Database to recover corrupted browser storage; it deletes stored data.'
        : 'Check that the compiled .mjs and .wasm files are available, then refresh or use New Memory Instance.';
    notice(`Could not start seekdb: ${error.message}\n${hint}`, true);
    setState('error', 'Startup failed');
    return false;
  }
}

function updateStorage() {
  const busy = ['loading', 'running', 'closing'].includes(state);
  const persistent = persistentStorage();
  const storageMode = element('storage-mode');
  const resetButton = element('reset-button');
  storageMode.value = storage;
  storageMode.disabled = busy;
  element('storage-badge').textContent = persistent ? 'opfs://' : 'memory://';
  element('storage-badge').title = persistent
    ? 'Origin Private File System; database data survives refresh in this browser'
    : 'Temporary browser memory; refreshing or closing this tab clears all database data';
  element('storage-notice-title').textContent = persistent ? 'Saved in this browser.' : 'Temporary memory session.';
  element('storage-notice-text').textContent = persistent
    ? 'Database data survives refresh. Output and command history do not; Reset Local Database deletes the stored database.'
    : 'Refresh or tab close clears this temporary database; your saved OPFS database is not affected. Switching resets output and command history.';
  resetButton.textContent = resetActionLabel();
  resetButton.disabled = busy || (persistent && resetBlocked);
  resetButton.title = persistent && resetBlocked
    ? 'Close the other seekdb Shell tab or window, then refresh this page'
    : persistent
      ? 'Delete the database stored in this browser and start over'
      : 'Discard the temporary database and start a new in-memory instance';
}

function closeMenus() {
  for (const menu of menus) menu.open = false;
}

async function switchStorage(nextStorage) {
  const storageMode = element('storage-mode');
  if (!['opfs', 'memory'].includes(nextStorage) || nextStorage === storage) return;
  if (['loading', 'running', 'closing'].includes(state)) {
    storageMode.value = storage;
    return;
  }
  const confirmed = window.confirm(persistentStorage()
    ? 'Switch to Temporary (Memory)?\n\nYour persistent database will be kept and will be available when you switch back. Data created in memory is cleared by a refresh or when this tab closes.'
    : 'Switch to Persistent (OPFS)?\n\nThe current in-memory database will be discarded. Your previously stored persistent database, if any, will be opened.');
  if (!confirmed) {
    storageMode.value = storage;
    return;
  }
  closeMenus();
  setState('closing', 'Switching storage mode…');
  const previousDatabase = database;
  database = undefined;
  session = undefined;
  try {
    await (persistentStorage() ? previousDatabase?.close() : previousDatabase?.discard());
    const url = new URL(location.href);
    url.searchParams.set('storage', nextStorage);
    trackEvent('shell_storage_mode_change', {from: storage, to: nextStorage});
    location.replace(url.href);
  } catch (error) {
    storageMode.value = storage;
    notice(`Could not switch storage mode: ${error.message}`, true);
    setState('error', 'Could not switch storage mode · refresh to reconnect');
  }
}

async function resetDatabase() {
  if (persistentStorage() && resetBlocked) return;
  const persistent = persistentStorage();
  const confirmed = window.confirm(persistent
    ? `Reset the local seekdb database?\n\nThis permanently deletes the database in this site's seekdb storage directory (${location.origin}). Other site files are kept.`
    : 'Start a new in-memory database?\n\nThis permanently deletes the current temporary database.');
  if (!confirmed) return;
  closeMenus();
  if (['loading', 'running', 'closing'].includes(state)) return;
  resetInputBuffer();
  delimiter = ';';
  replaceOutput();
  followOutput = true;
  setInput('');
  try {
    if (database) {
      setState('closing', 'Discarding the previous database…');
      session = undefined;
      await database.discard();
      database = undefined;
    } else if (Database && persistent) {
      setState('closing', 'Clearing the stored database…');
      await Database.clearPersistentStorage();
    }
    setStartupStage('engine', 'Loading engine');
    setState('loading', persistent ? 'Creating a new persistent database…' : 'Creating a new in-memory database…');
    await openDatabase();
  } catch (error) {
    notice(`Could not start a new database: ${error.message}\nTry ${resetActionLabel()} again.`, true);
    setState('error', 'Could not start a new database');
  } finally {
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
    void submit(EXAMPLES[button.dataset.example], {source: `example_${button.dataset.example}`});
  });
}
element('clear-button').addEventListener('click', clearOutput);
element('storage-mode').addEventListener('change', event => { void switchStorage(event.target.value); });
element('reset-button').addEventListener('click', () => void resetDatabase());
document.addEventListener('click', event => {
  for (const menu of menus) if (!menu.contains(event.target)) menu.open = false;
});
window.addEventListener('pagehide', () => { void database?.close().catch(() => {}); });
window.addEventListener('pageshow', event => {
  // pagehide closes the native runtime. A page restored from the back-forward
  // cache must reload so its JavaScript state reconnects to the OPFS database.
  if (event.persisted) location.reload();
});

await openDatabase();
