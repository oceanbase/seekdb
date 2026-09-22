// Copyright (c) 2026 OceanBase. SPDX-License-Identifier: Apache-2.0
import {persistentStorageAvailable, runtimeArguments, storageMode} from './runtime-host.mjs';
import {SqlError} from './mysql-wire.mjs';
export {SqlError};

function restoreError(record) {
  if (record.name === 'SqlError') return new SqlError(record.code, record.sqlState, record.message);
  const error = new Error(record.message);
  error.name = record.name;
  return error;
}

const PERSISTENT_LOCK = 'seekdb-wasm-opfs';
let cachedWasm;

// WasmFS does not enforce file locks, so a Web Lock keeps the persistent
// database to one instance per origin across tabs and windows.
async function acquirePersistentLock() {
  if (typeof navigator === 'undefined' || !navigator.locks) {
    throw new Error('Persistent storage requires Web Locks support');
  }
  let release;
  const held = new Promise(resolve => { release = resolve; });
  const granted = await new Promise((resolve, reject) => {
    navigator.locks.request(PERSISTENT_LOCK, {ifAvailable: true}, lock => {
      resolve(lock !== null);
      return lock === null ? undefined : held;
    }).catch(reject);
  });
  if (!granted) throw new Error('The persistent database is already open in another tab or window');
  return release;
}

async function clearStorageInWorker() {
  const worker = new Worker(new URL('./storage-cleanup-worker.mjs', import.meta.url), {type: 'module'});
  try {
    await new Promise((resolve, reject) => {
      worker.addEventListener('message', ({data}) => {
        if (data.error) reject(restoreError(data.error));
        else if (data.cleared) resolve();
        else reject(new Error('The storage cleanup Worker returned an invalid response'));
      });
      const fail = event => reject(new Error(event.message ?? 'The storage cleanup Worker failed'));
      worker.addEventListener('error', fail);
      worker.addEventListener('messageerror', fail);
      worker.postMessage({op: 'clear'});
    });
  } finally {
    worker.terminate();
  }
}

// Each open owns a fresh Worker/module; close awaits native destruction before
// terminating it. Storage 'memory' starts empty on every open. Storage 'opfs'
// keeps the data directory in the origin private file system, one open at a time.
export class Database {
  #worker;
  #release;
  #requests = new Map();
  #serial = 0;
  #failure;
  #closing;
  #terminating;
  #persistent;
  #disposal;
  #disposalPending = false;
  #discarded = false;
  #cleared = false;

  static async open({moduleURL, wasmURL, budgets, storage = 'memory', workerURL = new URL('./database-worker.mjs', import.meta.url),
    workerFactory = url => new Worker(url, {type: 'module'})} = {}) {
    if (!moduleURL) throw new TypeError('moduleURL is required');
    if (wasmURL !== undefined) wasmURL = new URL(wasmURL, moduleURL).href;
    const cacheKey = wasmURL === undefined ? undefined : JSON.stringify([String(moduleURL), wasmURL]);
    runtimeArguments(budgets);
    storageMode(storage);
    if (typeof window !== 'undefined' && !globalThis.crossOriginIsolated) {
      throw new Error('seekdb requires cross-origin isolation (COOP/COEP) for pthreads');
    }
    if (storage === 'opfs' && !persistentStorageAvailable()) {
      throw new Error('This browser does not offer persistent storage (OPFS)');
    }
    const release = storage === 'opfs' ? await acquirePersistentLock() : undefined;
    let db;
    try {
      db = new Database(workerFactory(workerURL), release);
      const result = await db.#request('open', {moduleURL: String(moduleURL), wasmURL, budgets, storage,
        compiledModule: cacheKey !== undefined && cachedWasm?.key === cacheKey ? cachedWasm.module : undefined});
      if (cacheKey !== undefined && result?.compiledModule instanceof WebAssembly.Module) {
        cachedWasm = {key: cacheKey, module: result.compiledModule};
      }
      return db;
    } catch (error) {
      if (db) await db.#dispose();
      else release?.();
      throw error;
    }
  }

  // Removes everything in the origin private file system root, which is the
  // persistent data directory. Fails while a persistent database is open.
  static async clearPersistentStorage() {
    if (!persistentStorageAvailable()) throw new Error('This browser does not offer persistent storage (OPFS)');
    const release = await acquirePersistentLock();
    try {
      await clearStorageInWorker();
    } finally { release(); }
  }

  constructor(worker, release) {
    this.#worker = worker;
    this.#release = release;
    this.#persistent = release !== undefined;
    const message = data => {
      if (data.fatal) { this.#fatal(restoreError(data.fatal)); return; }
      const pending = this.#requests.get(data.id);
      if (!pending) return;
      this.#requests.delete(data.id);
      if (data.error) pending.reject(restoreError(data.error));
      else pending.resolve(data.value);
    };
    const error = event => this.#fatal(new Error(event.message ?? 'Database Worker failed'));
    if (worker.addEventListener) {
      worker.addEventListener('message', event => message(event.data));
      worker.addEventListener('error', error);
      worker.addEventListener('messageerror', error);
    } else {
      // Node worker_threads is used for integration tests of the same protocol.
      worker.on('message', message);
      worker.on('error', error);
      worker.on('messageerror', error);
      worker.on('exit', () => this.#fail(new Error('Database Worker exited')));
    }
  }

  #fail(error) {
    this.#failure ??= error;
    for (const pending of this.#requests.values()) pending.reject(this.#failure);
    this.#requests.clear();
  }

  #terminate() {
    this.#terminating ??= Promise.resolve().then(() => this.#worker.terminate());
    return this.#terminating;
  }

  #dispose() {
    if (!this.#disposal) {
      this.#disposalPending = true;
      this.#disposal = (async () => {
        try {
          await this.#terminate();
          if (this.#discarded && this.#persistent && !this.#cleared) {
            this.#release ??= await acquirePersistentLock();
            await clearStorageInWorker();
          }
          if (this.#discarded) this.#cleared = true;
          this.#release?.();
          this.#release = undefined;
        } finally {
          this.#disposalPending = false;
        }
      })();
    }
    return this.#disposal;
  }

  #fatal(error) {
    this.#fail(error);
    void this.#dispose().catch(() => {});
  }

  #request(op, body = {}) {
    if (this.#failure) return Promise.reject(this.#failure);
    const id = ++this.#serial;
    return new Promise((resolve, reject) => {
      this.#requests.set(id, {resolve, reject});
      try { this.#worker.postMessage({id, op, ...body}); }
      catch (error) { this.#requests.delete(id); reject(error); }
    });
  }

  async connect(options) {
    if (this.#closing) throw new Error('Database is closing');
    const session = await this.#request('connect', {options});
    if (this.#failure) throw this.#failure;
    if (this.#closing) throw new Error('Database is closing');
    let closed = false;
    let busy = false;
    let disconnecting;
    const request = (op, body) => this.#request(op, {session, ...body});
    const checkOpen = () => {
      if (this.#failure) throw this.#failure;
      if (closed || this.#closing) throw new Error('Session is closed');
    };
    return {
      // Events contain owned Uint8Array fields and BigInt counters, preserved by
      // structured clone. One pull yields a bounded batch of events.
      async *query(sql, {signal, preview} = {}) {
        checkOpen();
        if (busy) throw new Error('Session already has an active query');
        if (typeof sql !== 'string') throw new TypeError('SQL must be a string');
        signal?.throwIfAborted();
        busy = true;
        let stream;
        let done = false;
        let cancel;
        const abort = () => {
          closed = true;
          if (stream !== undefined) cancel ??= request('cancel', {stream}).catch(() => {});
        };
        signal?.addEventListener('abort', abort, {once: true});
        try {
          stream = await request('query', {sql, preview});
          if (signal?.aborted) { abort(); signal.throwIfAborted(); }
          for (;;) {
            const result = await request('next', {stream});
            signal?.throwIfAborted();
            for (const event of result.events) {
              signal?.throwIfAborted();
              checkOpen();
              yield event;
            }
            checkOpen();
            if (result.done) { done = true; return; }
          }
        } catch (error) {
          // SQL errors leave the protocol synchronized and the session usable.
          if (error instanceof SqlError) done = true;
          if (signal?.aborted) signal.throwIfAborted();
          throw error;
        } finally {
          signal?.removeEventListener('abort', abort);
          if (!done && stream !== undefined) {
            closed = true;
            await (cancel ?? request('release', {stream}).catch(() => {}));
          }
          busy = false;
        }
      },
      close() {
        closed = true;
        disconnecting ??= request('disconnect');
        return disconnecting;
      },
    };
  }

  close() {
    if (this.#discarded) return this.#dispose();
    if (!this.#closing) this.#closing = (async () => {
      try { await this.#request('close'); }
      finally {
        this.#fail(new Error('Database is closed'));
        await this.#dispose();
      }
    })();
    return this.#closing;
  }

  discard() {
    this.#discarded = true;
    this.#fail(new Error('Database was discarded'));
    if (!this.#disposalPending && !this.#cleared) this.#disposal = undefined;
    return this.#dispose();
  }
}
