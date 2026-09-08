// Copyright (c) 2026 OceanBase. SPDX-License-Identifier: Apache-2.0
// A single JS owner drives the copying, nonblocking memory-transport ABI.
const pause = () => new Promise(resolve => setTimeout(resolve, 1));

export class WasmMemoryTransport {
  #module;
  #client;
  #buffer;
  #size;
  #operations = 0;

  constructor(module, client, {chunkBytes = 65536} = {}) {
    if (!Number.isInteger(chunkBytes) || chunkBytes < 1 || chunkBytes > 1048576) {
      throw new RangeError('Invalid transport chunk size');
    }
    if (!client) throw new Error('Memory connection admission failed');
    this.#module = module;
    this.#client = client;
    this.#size = chunkBytes;
    this.#buffer = module._malloc(chunkBytes);
    if (!this.#buffer) {
      this.close();
      throw new Error('Unable to allocate transport buffer');
    }
  }

  #check() {
    if (!this.#client) throw new Error('Memory connection is closed');
  }

  #heap() {
    // getValue refreshes Emscripten's exported views after growth by a pthread.
    // Obtain the view again for every copy; never retain it across an await.
    this.#module.getValue(this.#buffer, 'i8');
    return this.#module.HEAPU8;
  }

  async #yield() {
    if (++this.#operations % 32 === 0) await pause();
    this.#check();
  }

  async write(bytes) {
    if (!(bytes instanceof Uint8Array)) throw new TypeError('Expected Uint8Array');
    this.#check();
    for (let offset = 0; offset < bytes.length;) {
      await this.#yield();
      const length = Math.min(this.#size, bytes.length - offset);
      this.#heap().set(bytes.subarray(offset, offset + length), this.#buffer);
      const written = Number(this.#module._nio_memory_write(this.#client, this.#buffer, BigInt(length)));
      if (written === -2) { await pause(); continue; }
      if (!Number.isInteger(written) || written <= 0 || written > length) {
        throw new Error('Memory connection write failed');
      }
      offset += written;
    }
  }

  async read() {
    for (;;) {
      await this.#yield();
      const count = Number(this.#module._nio_memory_read(this.#client, this.#buffer, BigInt(this.#size)));
      if (count === -2) { await pause(); continue; }
      if (count === 0) return null;
      if (!Number.isInteger(count) || count < 0 || count > this.#size) {
        throw new Error('Memory connection read failed');
      }
      return this.#heap().slice(this.#buffer, this.#buffer + count);
    }
  }

  close() {
    // JS calls are serialized in the owning Worker. Mark closed before freeing
    // so an IO loop resuming after an await cannot use either released pointer.
    const client = this.#client;
    this.#client = 0;
    if (client) this.#module._nio_memory_close(client);
    if (this.#buffer) this.#module._free(this.#buffer);
    this.#buffer = 0;
  }
}
