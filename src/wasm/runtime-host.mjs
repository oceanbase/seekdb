// Copyright (c) 2026 OceanBase. SPDX-License-Identifier: Apache-2.0
import {MySQLConnection} from './mysql-client.mjs';
import {WasmMemoryTransport} from './mysql-transport.mjs';

export function runtimeArguments(options = {}) {
  const defaults = {memoryMiB: 1024, allocatorMiB: 1536, cacheMiB: 64,
    memstoreMiB: 192, vectorMiB: 64, cpuCount: 2, datafileMiB: 32, logDiskMiB: 2048};
  for (const key of Object.keys(options)) if (!Object.hasOwn(defaults, key)) throw new TypeError(`Unknown budget: ${key}`);
  const budget = {...defaults, ...options};
  for (const [key, value] of Object.entries(budget)) {
    if (!Number.isInteger(value) || value < 1 || value > 4096) throw new RangeError(`Invalid budget: ${key}`);
  }
  if (budget.memoryMiB > budget.allocatorMiB || budget.allocatorMiB > 1536 || budget.cpuCount > 4
      || budget.cacheMiB + budget.memstoreMiB + budget.vectorMiB >= budget.memoryMiB) {
    throw new RangeError('Memory or CPU budgets exceed this build configuration');
  }
  return Object.values(budget).map(String);
}

export const STORAGE_MODES = ['memory', 'opfs'];

export function storageMode(storage = 'memory') {
  if (!STORAGE_MODES.includes(storage)) throw new TypeError(`Unknown storage: ${storage}`);
  return storage;
}

export function persistentStorageAvailable() {
  return typeof navigator !== 'undefined' && typeof navigator.storage?.getDirectory === 'function';
}

// Sync access handles are exclusive per file, and the previous owner of the
// data directory may still be releasing them when this Worker starts.
async function waitForUnlockedFiles(deadline = 3000) {
  const files = [];
  async function collect(directory) {
    for await (const handle of directory.values()) {
      if (handle.kind === 'directory') await collect(handle);
      else files.push(handle);
    }
  }
  await collect(await navigator.storage.getDirectory());
  const until = Date.now() + deadline;
  for (const file of files) {
    for (;;) {
      let access;
      try {
        access = await file.createSyncAccessHandle();
      } catch (error) {
        if (!['NoModificationAllowedError', 'InvalidStateError'].includes(error?.name)) {
          throw new Error(`Stored database file ${file.name} cannot be opened: ${error?.message ?? error}`);
        }
        if (Date.now() >= until) throw new Error(`Stored database file ${file.name} did not become available: ${error.name}: ${error.message}`);
        await new Promise(resolve => setTimeout(resolve, 100));
        continue;
      }
      access.close();
      break;
    }
  }
}

// Internal owner-side API. All calls occur in one Worker. Runtime exit, rather
// than just a stop flag, acknowledges that ObServer has joined and destroyed.
export async function openRuntime(factory, {budgets, storage, wasmURL, compiledModule,
  onCompiledModule = () => {}, onFatal = () => {}} = {}) {
  const args = [...runtimeArguments(budgets), storageMode(storage)];
  if (args.at(-1) === 'opfs') {
    if (!persistentStorageAvailable()) throw new Error('Persistent storage (OPFS) is not available in this Worker');
    await waitForUnlockedFiles();
  }
  let terminal;
  let finish;
  const exited = new Promise(resolve => { finish = resolve; });
  const end = result => {
    if (!terminal) {
      terminal = result;
      finish(result);
      if (result.error) onFatal(result.error);
    }
  };
  const options = {arguments: args,
    onExit: code => end({code}),
    onAbort: reason => end({error: new Error(`WASM runtime aborted: ${reason}`)})};
  let instantiationFailed;
  if (wasmURL !== undefined) {
    instantiationFailed = new Promise((_, reject) => {
      options.instantiateWasm = (imports, receive) => {
        void (async () => {
          let instance;
          if (compiledModule) {
            instance = new WebAssembly.Instance(compiledModule, imports);
          } else {
            const response = await fetch(wasmURL);
            if (!response.ok) throw new Error(`Could not load WebAssembly: HTTP ${response.status}`);
            const streaming = typeof WebAssembly.instantiateStreaming === 'function'
              && response.headers.get('Content-Type')?.split(';', 1)[0].trim() === 'application/wasm';
            const result = streaming
              ? await WebAssembly.instantiateStreaming(response, imports)
              : await WebAssembly.instantiate(await response.arrayBuffer(), imports);
            instance = result.instance;
            compiledModule = result.module;
          }
          receive(instance, compiledModule);
          onCompiledModule(compiledModule);
        })().catch(reject);
      };
    });
  }
  const initialized = factory(options);
  const module = await (instantiationFailed ? Promise.race([initialized, instantiationFailed]) : initialized);
  while (!terminal && module._seekdb_runtime_state() !== 1) {
    await new Promise(resolve => setTimeout(resolve, 5));
  }
  if (terminal) throw terminal.error ?? new Error(`Database startup failed (exit ${terminal.code})`);
  let closing;
  const clients = new Set();
  return {
    async connect({capacity = 65536, chunkBytes = 65536, maxPacketBytes = 64 * 1024 * 1024, ...auth} = {}) {
      if (closing || terminal) throw new Error('Database is closed');
      if (!Number.isInteger(capacity) || capacity < 1 || capacity > 16 * 1024 * 1024
          || !Number.isInteger(chunkBytes) || chunkBytes < 1 || chunkBytes > 1048576
          || !Number.isSafeInteger(maxPacketBytes) || maxPacketBytes < 1 || maxPacketBytes > 0xffffffff) {
        throw new RangeError('Invalid connection buffer limit');
      }
      const transport = new WasmMemoryTransport(module, module._seekdb_runtime_connect(capacity), {chunkBytes});
      let client;
      try { client = new MySQLConnection(transport, {maxPacketBytes}); }
      catch (error) { transport.close(); throw error; }
      clients.add(client);
      try {
        await client.authenticate(auth);
        if (closing || terminal) throw new Error('Database closed during authentication');
        return client;
      } catch (error) {
        clients.delete(client);
        if (!terminal) client.close();
        throw error;
      }
    },
    disconnect(client) {
      if (clients.delete(client) && !terminal) client.close();
    },
    close() {
      if (!closing) closing = (async () => {
        if (!terminal) {
          for (const client of clients) client.close();
          clients.clear();
          module._seekdb_runtime_close();
        }
        const result = await exited;
        if (result.error) throw result.error;
        if (result.code !== 0) throw new Error(`Database shutdown failed (exit ${result.code})`);
      })();
      return closing;
    },
  };
}
