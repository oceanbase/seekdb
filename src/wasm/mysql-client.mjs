// Copyright (c) 2026 OceanBase. SPDX-License-Identifier: Apache-2.0
import {PacketDecoder, encodePacket, decodeGreeting, decodeAuthSwitch, decodeOK,
  TextResultDecoder, ProtocolError, SqlError} from './mysql-wire.mjs';
import {encodeLogin, nativePasswordResponse} from './mysql-auth.mjs';

const utf8 = new TextEncoder();

export class MySQLConnection {
  #transport;
  #packets = new PacketDecoder();
  #pending = [];
  #busy = false;
  #closed = false;
  #authenticated = false;
  #maxPacketBytes;
  greeting;

  constructor(transport, {maxPacketBytes = 64 * 1024 * 1024} = {}) {
    if (!Number.isSafeInteger(maxPacketBytes) || maxPacketBytes < 1) throw new RangeError('Invalid packet limit');
    this.#transport = transport;
    this.#maxPacketBytes = maxPacketBytes;
    this.#packets = new PacketDecoder({maxPacketBytes});
  }

  async #readPacket() {
    while (!this.#pending.length) {
      const bytes = await this.#transport.read();
      if (bytes === null) {
        this.#packets.finish();
        throw new Error('Connection closed before the response completed');
      }
      this.#pending.push(...this.#packets.push(bytes));
    }
    return this.#pending.shift();
  }

  async #send(payload, sequence) {
    if (payload.length > this.#maxPacketBytes) throw new RangeError('Command exceeds packet limit');
    if (this.#pending.length) throw new ProtocolError('Unexpected pending response');
    const encoded = encodePacket(payload, sequence);
    this.#packets.resetSequence(encoded.nextSequence);
    for (const frame of encoded.frames) await this.#transport.write(frame);
  }

  async authenticate(options = {}) {
    if (this.#closed || this.#busy || this.#authenticated) throw new Error('Connection cannot authenticate in its current state');
    this.#busy = true;
    try {
      this.greeting = decodeGreeting(await this.#readPacket());
      const login = await encodeLogin(this.greeting, {...options, maxPacketBytes: this.#maxPacketBytes});
      try { await this.#send(login.payload, this.#packets.nextSequence); }
      finally { login.payload.fill(0); }
      let switches = 0;
      for (;;) {
        const response = await this.#readPacket();
        if (response[0] === 0) {
          decodeOK(response);
          this.#packets.finish();
          if (this.#pending.length) throw new ProtocolError('Unexpected authentication response');
          this.#authenticated = true;
          return this;
        }
        if (response[0] === 0xff) new TextResultDecoder().accept(response);
        if (response[0] !== 0xfe || ++switches > 2) throw new ProtocolError('Unsupported authentication exchange');
        const challenge = decodeAuthSwitch(response);
        const answer = await nativePasswordResponse(options.password ?? '', challenge);
        try { await this.#send(answer, this.#packets.nextSequence); }
        finally { answer.fill(0); }
      }
    } catch (error) {
      this.close();
      throw error;
    } finally { this.#busy = false; }
  }

  // Stream owned byte rows and metadata. The caller chooses charset and integer
  // conversion, and can release each event immediately after consumption.
  async *query(sql, {signal} = {}) {
    if (typeof sql !== 'string') throw new TypeError('SQL must be a string');
    if (this.#closed || !this.#authenticated) throw new Error('Connection is not open');
    if (this.#busy) throw new Error('A query is already active on this connection');
    signal?.throwIfAborted();
    const text = utf8.encode(sql);
    if (text.length + 1 > this.#maxPacketBytes) throw new RangeError('SQL exceeds packet limit');
    const payload = new Uint8Array(text.length + 1);
    payload[0] = 3; // COM_QUERY
    payload.set(text, 1);
    this.#busy = true;
    const result = new TextResultDecoder();
    const abort = () => this.close();
    signal?.addEventListener('abort', abort, {once: true});
    try {
      await this.#send(payload, 0);
      while (!result.done) {
        const event = result.accept(await this.#readPacket());
        if (result.done) {
          this.#packets.finish();
          if (this.#pending.length) throw new ProtocolError('Unexpected packet after query result');
        }
        if (event) yield event;
      }
    } catch (error) {
      if (!(error instanceof SqlError)) this.close();
      if (signal?.aborted) signal.throwIfAborted();
      throw error;
    } finally {
      signal?.removeEventListener('abort', abort);
      // Abandoning an iterator leaves unread protocol state. Closing also asks
      // the real server to cancel the connection; it never reports a commit.
      if (!result.done) this.close();
      this.#busy = false;
    }
  }

  close() {
    if (!this.#closed) {
      this.#closed = true;
      this.#transport.close();
    }
  }
}
