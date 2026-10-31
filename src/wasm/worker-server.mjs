// Copyright (c) 2026 OceanBase. SPDX-License-Identifier: Apache-2.0
import {openRuntime} from './runtime-host.mjs';
import {SqlError} from './mysql-wire.mjs';

export function errorRecord(error) {
  return {name: error.name ?? 'Error', message: error.message ?? String(error),
    code: error.code, sqlState: error.sqlState};
}

const BATCH_EVENTS = 64;
const BATCH_BYTES = 256 * 1024;
const BATCH_MILLISECONDS = 4;
const BATCH_READS = 1024;
const TIMED_OUT = Symbol('Timed out');

function previewOptions(options) {
  if (options === undefined) return;
  if (options === null || typeof options !== 'object' || Array.isArray(options)) {
    throw new TypeError('Invalid preview options');
  }
  const defaults = {maxRows: 500, maxColumns: 100, maxCells: 20000, maxCellBytes: 480, maxBytes: 400000};
  const maximum = {maxRows: 10000, maxColumns: 16384, maxCells: 1000000, maxCellBytes: 1048576, maxBytes: 16777216};
  for (const key of Object.keys(options)) {
    if (!Object.hasOwn(defaults, key)) throw new TypeError(`Unknown preview option: ${key}`);
  }
  const limits = {...defaults, ...options};
  for (const [key, value] of Object.entries(limits)) {
    if (!Number.isInteger(value) || value < (key === 'maxRows' ? 0 : 1) || value > maximum[key]) {
      throw new RangeError(`Invalid preview option: ${key}`);
    }
  }
  return limits;
}

function eventBytes(value) {
  if (value instanceof Uint8Array) return value.byteLength;
  if (Array.isArray(value)) return value.reduce((total, item) => total + eventBytes(item), 0);
  if (value && typeof value === 'object') return Object.values(value).reduce((total, item) => total + eventBytes(item), 0);
  return 0;
}

function previewEvent(stream, event) {
  if (!stream.preview) return event;
  if (event.kind === 'columns') {
    stream.previewResult = {rowCount: 0n, displayedRows: 0, bytes: 0, cells: 0,
      limited: event.columns.length > stream.preview.maxColumns, truncated: false};
  }
  const result = stream.previewResult;
  if (!result) return event;
  if (event.kind === 'complete') {
    stream.previewResult = undefined;
    return {...event, preview: {rowCount: result.rowCount, displayedRows: result.displayedRows,
      limited: result.limited, truncated: result.truncated}};
  }
  if (event.kind !== 'row') return event;
  result.rowCount++;
  const limits = stream.preview;
  const count = Math.min(event.values.length, limits.maxColumns);
  if (result.exhausted || result.displayedRows >= limits.maxRows || result.cells + count > limits.maxCells) {
    result.limited = result.exhausted = true;
    return;
  }
  const values = [];
  let bytes = 0;
  let truncated = false;
  for (const value of event.values.slice(0, count)) {
    if (value === null) { values.push(null); continue; }
    let length = Math.min(value.length, limits.maxCellBytes);
    if (length < value.length) {
      while (length > 0 && (value[length] & 0xc0) === 0x80) length--;
      truncated = true;
    }
    bytes += length;
    if (result.bytes + bytes > limits.maxBytes) {
      result.limited = result.exhausted = true;
      return;
    }
    values.push(length < value.length ? value.slice(0, length) : value);
  }
  result.displayedRows++;
  result.cells += count;
  result.bytes += bytes;
  result.truncated ||= truncated;
  return {...event, values, truncated};
}

async function readBatch(stream) {
  const events = [];
  let bytes = 0;
  let timer;
  let deadline;
  let timeout;
  try {
    for (let reads = 0; reads < BATCH_READS; ++reads) {
      stream.controller.signal.throwIfAborted();
      if (deadline !== undefined && performance.now() >= deadline) break;
      let record = stream.buffered;
      stream.buffered = undefined;
      if (!record) {
        stream.reading ??= Promise.resolve().then(() => stream.iterator.next()).then(
          result => ({result}), error => ({error}));
        record = timeout ? await Promise.race([stream.reading, timeout]) : await stream.reading;
        if (record === TIMED_OUT) break;
        stream.reading = undefined;
      }
      stream.controller.signal.throwIfAborted();
      if (record.error) {
        if (!events.length) throw record.error;
        stream.buffered = record;
        break;
      }
      if (record.result.done) return {events, done: true};
      const event = record.previewed ? record.result.value : previewEvent(stream, record.result.value);
      if (deadline === undefined) {
        deadline = performance.now() + BATCH_MILLISECONDS;
        timeout = new Promise(resolve => { timer = setTimeout(() => resolve(TIMED_OUT), BATCH_MILLISECONDS); });
      }
      if (!event) continue;
      const size = eventBytes(event);
      if (events.length && bytes + size > BATCH_BYTES) {
        stream.buffered = {result: {value: event, done: false}, previewed: true};
        break;
      }
      events.push(event);
      bytes += size;
      if (event.kind !== 'row' || events.length >= BATCH_EVENTS || bytes >= BATCH_BYTES) break;
    }
    return {events, done: false};
  } finally { clearTimeout(timer); }
}

// Message handling remains asynchronous: cancel/close must run while a next()
// is waiting for SQL. Only one next() can be outstanding on each result stream.
export function installWorkerServer({listen, send, open = openRuntime}) {
  let runtime;
  let opening = false;
  let closing = false;
  let serial = 0;
  const sessions = new Map();
  const streams = new Map();
  async function dispatch(message) {
    const {op} = message;
    if (op === 'open') {
      if (opening) throw new Error('Worker already opened a database');
      opening = true;
      const {default: factory} = await import(message.moduleURL);
      let compiledModule = message.compiledModule;
      runtime = await open(factory, {budgets: message.budgets, storage: message.storage,
        wasmURL: message.wasmURL, compiledModule, onCompiledModule: module => { compiledModule = module; },
        onFatal: error => send({fatal: errorRecord(error)})});
      return message.wasmURL ? {compiledModule} : undefined;
    }
    if (!runtime || closing) throw new Error('Database is not open');
    if (op === 'connect') {
      const client = await runtime.connect(message.options);
      if (closing) { runtime.disconnect(client); throw new Error('Database is closing'); }
      const id = ++serial;
      sessions.set(id, {client, stream: null});
      return id;
    }
    if (op === 'close') {
      closing = true;
      // Close connections first to wake any blocked reader, then let the engine
      // roll back disconnected sessions and join its own services.
      for (const session of sessions.values()) runtime.disconnect(session.client);
      for (const stream of streams.values()) stream.controller.abort();
      sessions.clear();
      streams.clear();
      await runtime.close();
      return;
    }
    if (op === 'next' || op === 'cancel' || op === 'release') {
      const stream = streams.get(message.stream);
      if (!stream) {
        if (op !== 'next') return;
        throw new Error('Result stream is closed');
      }
      if (op !== 'next') {
        streams.delete(message.stream);
        stream.controller.abort();
        runtime.disconnect(stream.session.client);
        sessions.delete(stream.sessionId);
        await stream.iterator.return();
        return;
      }
      if (stream.pending) throw new Error('Result already has a pending read');
      stream.pending = true;
      try {
        const result = await readBatch(stream);
        if (result.done) {
          streams.delete(message.stream);
          stream.session.stream = null;
        }
        return result;
      } catch (error) {
        streams.delete(message.stream);
        stream.session.stream = null;
        if (!(error instanceof SqlError)) {
          runtime.disconnect(stream.session.client);
          sessions.delete(stream.sessionId);
        }
        throw error;
      } finally { stream.pending = false; }
    }
    const session = sessions.get(message.session);
    if (!session && op === 'disconnect') return;
    if (!session) throw new Error('Session is closed');
    if (op === 'disconnect') {
      runtime.disconnect(session.client);
      sessions.delete(message.session);
      if (session.stream !== null) {
        const stream = streams.get(session.stream);
        streams.delete(session.stream);
        stream?.controller.abort();
        await stream?.iterator.return();
      }
      return;
    }
    if (op === 'query') {
      if (session.stream !== null) throw new Error('Session already has an active query');
      const preview = previewOptions(message.preview);
      const id = ++serial;
      const controller = new AbortController();
      const iterator = session.client.query(message.sql, {signal: controller.signal});
      session.stream = id;
      streams.set(id, {session, sessionId: message.session, controller, iterator, pending: false, preview});
      return id;
    }
    throw new Error(`Unknown operation: ${op}`);
  }
  listen(message => {
    void dispatch(message).then(value => send({id: message.id, value}),
      error => send({id: message.id, error: errorRecord(error)}));
  });
}
