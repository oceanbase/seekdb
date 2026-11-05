// Copyright (c) 2026 OceanBase. SPDX-License-Identifier: Apache-2.0
// Browser-side codec for sql-nio's uncompressed MySQL 4.1 text protocol.
// Wire definitions: rust/sql-nio/src/{packet,response,codec}.rs.
export const MAX_FRAME_PAYLOAD = 0xffffff;
const DEFAULT_PACKET_LIMIT = 64 * 1024 * 1024;

export class ProtocolError extends Error {}
export class SqlError extends Error {
  constructor(code, sqlState, message) {
    super(message);
    this.name = 'SqlError';
    this.code = code;
    this.sqlState = sqlState;
  }
}

function checkSequence(sequence) {
  if (!Number.isInteger(sequence) || sequence < 0 || sequence > 255) {
    throw new RangeError('Invalid packet sequence');
  }
}

// Own partial input: callers may reuse their read buffer immediately after push.
// Sequence numbers span all result packets until the next command resets them.
export class PacketDecoder {
  #header = new Uint8Array(4);
  #headerUsed = 0;
  #frame = null;
  #frameUsed = 0;
  #parts = [];
  #size = 0;
  #sequence;
  #limit;
  #failed = false;

  constructor({sequence = 0, maxPacketBytes = DEFAULT_PACKET_LIMIT} = {}) {
    checkSequence(sequence);
    if (!Number.isSafeInteger(maxPacketBytes) || maxPacketBytes < 1) {
      throw new RangeError('Invalid packet size limit');
    }
    this.#sequence = sequence;
    this.#limit = maxPacketBytes;
  }

  get nextSequence() { return this.#sequence; }

  resetSequence(sequence) {
    checkSequence(sequence);
    this.finish();
    this.#sequence = sequence;
  }

  #fail(message) {
    this.#failed = true;
    this.#parts = [];
    this.#frame = null;
    throw new ProtocolError(message);
  }

  finish() {
    if (this.#failed) throw new ProtocolError('Packet decoder has failed');
    if (this.#headerUsed || this.#frame || this.#size) {
      this.#fail('Truncated MySQL packet');
    }
  }

  push(input) {
    if (this.#failed) throw new ProtocolError('Packet decoder has failed');
    if (!(input instanceof Uint8Array)) throw new TypeError('Expected Uint8Array');
    const packets = [];
    let offset = 0;
    while (offset < input.length) {
      if (this.#headerUsed < 4) {
        const length = Math.min(4 - this.#headerUsed, input.length - offset);
        this.#header.set(input.subarray(offset, offset + length), this.#headerUsed);
        this.#headerUsed += length;
        offset += length;
        if (this.#headerUsed < 4) break;
        const lengthBytes = this.#header[0] | (this.#header[1] << 8) | (this.#header[2] << 16);
        if (this.#header[3] !== this.#sequence) this.#fail('Unexpected MySQL packet sequence');
        if (lengthBytes > this.#limit - this.#size) this.#fail('MySQL packet exceeds configured limit');
        this.#sequence = (this.#sequence + 1) & 255;
        this.#frame = new Uint8Array(lengthBytes);
        this.#frameUsed = 0;
      }
      const length = Math.min(this.#frame.length - this.#frameUsed, input.length - offset);
      this.#frame.set(input.subarray(offset, offset + length), this.#frameUsed);
      this.#frameUsed += length;
      offset += length;
      if (this.#frameUsed !== this.#frame.length) break;
      this.#parts.push(this.#frame);
      this.#size += this.#frame.length;
      if (this.#frame.length < MAX_FRAME_PAYLOAD) {
        let packet = this.#parts[0];
        if (this.#parts.length > 1) {
          packet = new Uint8Array(this.#size);
          let at = 0;
          for (const part of this.#parts) { packet.set(part, at); at += part.length; }
        }
        packets.push(packet);
        this.#parts = [];
        this.#size = 0;
      }
      this.#frame = null;
      this.#headerUsed = 0;
    }
    return packets;
  }
}

export function encodePacket(payload, sequence = 0) {
  if (!(payload instanceof Uint8Array)) throw new TypeError('Expected Uint8Array');
  checkSequence(sequence);
  const frames = [];
  let offset = 0;
  for (;;) {
    const length = Math.min(MAX_FRAME_PAYLOAD, payload.length - offset);
    const frame = new Uint8Array(length + 4);
    frame.set([length & 255, (length >>> 8) & 255, (length >>> 16) & 255, sequence]);
    frame.set(payload.subarray(offset, offset + length), 4);
    frames.push(frame);
    sequence = (sequence + 1) & 255;
    offset += length;
    // An exact 0xffffff multiple needs an empty terminating frame.
    if (length < MAX_FRAME_PAYLOAD) return {frames, nextSequence: sequence};
  }
}

class Reader {
  constructor(bytes) {
    if (!(bytes instanceof Uint8Array)) throw new TypeError('Expected Uint8Array');
    this.bytes = bytes;
    this.offset = 0;
  }
  get remaining() { return this.bytes.length - this.offset; }
  take(length) {
    if (!Number.isSafeInteger(length) || length < 0 || length > this.remaining) {
      throw new ProtocolError('Truncated MySQL payload');
    }
    const result = this.bytes.subarray(this.offset, this.offset + length);
    this.offset += length;
    return result;
  }
  uint(width) {
    const bytes = this.take(width);
    let value = 0n;
    for (let i = width - 1; i >= 0; --i) value = (value << 8n) | BigInt(bytes[i]);
    return value;
  }
  byte() { return this.take(1)[0]; }
  nulBytes() {
    const end = this.bytes.indexOf(0, this.offset);
    if (end < 0) throw new ProtocolError('Unterminated MySQL string');
    const bytes = this.take(end - this.offset).slice();
    this.byte();
    return bytes;
  }
  lenenc() {
    const first = this.byte();
    if (first < 0xfb) return BigInt(first);
    if (first === 0xfb) return null;
    if (first === 0xfc) return this.uint(2);
    if (first === 0xfd) return this.uint(3);
    if (first === 0xfe) return this.uint(8);
    throw new ProtocolError('Invalid length-encoded integer');
  }
  lenencBytes(nullable = false) {
    const length = this.lenenc();
    if (length === null) {
      if (nullable) return null;
      throw new ProtocolError('Unexpected NULL length');
    }
    if (length > BigInt(this.remaining)) throw new ProtocolError('Truncated length-encoded value');
    return this.take(Number(length)).slice();
  }
  end() {
    if (this.remaining) throw new ProtocolError('Trailing MySQL payload bytes');
  }
}

export function decodeGreeting(payload) {
  if (payload[0] === 0xff) throw decodeError(payload);
  const reader = new Reader(payload);
  if (reader.byte() !== 10) throw new ProtocolError('Expected MySQL protocol version 10');
  const utf8 = new TextDecoder('utf-8', {fatal: true});
  const version = utf8.decode(reader.nulBytes());
  const connectionId = Number(reader.uint(4));
  const first = reader.take(8);
  if (reader.byte() !== 0) throw new ProtocolError('Invalid greeting filler');
  const low = Number(reader.uint(2));
  const charset = reader.byte();
  const status = Number(reader.uint(2));
  const capabilities = low + Number(reader.uint(2)) * 65536;
  const authLength = reader.byte();
  reader.take(10);
  // sql-nio advertises mysql_native_password with a 20-byte binary challenge
  // plus one terminator. Embedded zero bytes are part of the challenge.
  if (authLength !== 21) throw new ProtocolError('Unsupported greeting auth-data length');
  const scramble = new Uint8Array(20);
  scramble.set(first);
  scramble.set(reader.take(12), 8);
  if (reader.byte() !== 0) throw new ProtocolError('Invalid greeting auth terminator');
  const plugin = utf8.decode(reader.nulBytes());
  reader.end();
  return {version, connectionId, charset, status, capabilities, scramble, plugin};
}

export function decodeAuthSwitch(payload) {
  const reader = new Reader(payload);
  if (reader.byte() !== 0xfe) throw new ProtocolError('Expected auth-switch packet');
  const plugin = new TextDecoder('utf-8', {fatal: true}).decode(reader.nulBytes());
  if (reader.remaining !== 20 && reader.remaining !== 21) throw new ProtocolError('Invalid auth challenge length');
  const scramble = reader.take(20).slice();
  if (reader.remaining && reader.byte() !== 0) throw new ProtocolError('Invalid auth challenge terminator');
  return {plugin, scramble};
}

export function decodeColumn(payload) {
  const reader = new Reader(payload);
  const column = {};
  // Keep names as bytes as well as values: no implicit lossy charset conversion.
  for (const name of ['catalog', 'schema', 'table', 'originalTable', 'name', 'originalName']) {
    column[name] = reader.lenencBytes();
  }
  if (reader.lenenc() !== 12n) throw new ProtocolError('Invalid column metadata length');
  column.charset = Number(reader.uint(2));
  column.length = Number(reader.uint(4));
  column.type = reader.byte();
  column.flags = Number(reader.uint(2));
  column.decimals = reader.byte();
  reader.take(2);
  // sql-nio can append seekdb complex-type/default metadata.
  column.extension = reader.take(reader.remaining).slice();
  return column;
}

export function decodeTextRow(payload, columnCount) {
  if (!Number.isSafeInteger(columnCount) || columnCount < 0) throw new RangeError('Invalid column count');
  const reader = new Reader(payload);
  const values = [];
  for (let i = 0; i < columnCount; ++i) values.push(reader.lenencBytes(true));
  reader.end();
  return values;
}

function decodeError(payload) {
  const reader = new Reader(payload);
  if (reader.byte() !== 0xff) throw new ProtocolError('Expected error packet');
  const code = Number(reader.uint(2));
  if (reader.byte() !== 0x23) throw new ProtocolError('Expected MySQL 4.1 SQLSTATE');
  const text = new TextDecoder();
  return new SqlError(code, text.decode(reader.take(5)), text.decode(reader.take(reader.remaining)));
}

export function decodeOK(payload) {
  const reader = new Reader(payload);
  if (reader.byte() !== 0) throw new ProtocolError('Expected OK packet');
  const affectedRows = reader.lenenc(), lastInsertId = reader.lenenc();
  if (affectedRows === null || lastInsertId === null) throw new ProtocolError('NULL OK counter');
  const status = Number(reader.uint(2)), warnings = Number(reader.uint(2));
  // This codec does not negotiate CLIENT_SESSION_TRACK or CLIENT_DEPRECATE_EOF.
  const message = reader.take(reader.remaining).slice();
  return {affectedRows, lastInsertId, status, warnings, message};
}

function decodeEOF(payload) {
  const reader = new Reader(payload);
  if (reader.byte() !== 0xfe) throw new ProtocolError('Expected EOF packet');
  const warnings = Number(reader.uint(2)), status = Number(reader.uint(2));
  reader.end();
  return {warnings, status};
}

// Stream events instead of retaining an unbounded query result. The transport
// owns flow control and must stop using a connection after ProtocolError.
export class TextResultDecoder {
  #phase = 'header';
  #columns = [];
  #columnCount = 0;
  #maxColumns;
  #maxMetadataBytes;
  #metadataBytes = 0;
  constructor({maxColumns = 16384, maxMetadataBytes = 4 * 1024 * 1024} = {}) {
    if (!Number.isSafeInteger(maxColumns) || maxColumns < 1) throw new RangeError('Invalid column limit');
    if (!Number.isSafeInteger(maxMetadataBytes) || maxMetadataBytes < 1) throw new RangeError('Invalid metadata limit');
    this.#maxColumns = maxColumns;
    this.#maxMetadataBytes = maxMetadataBytes;
  }
  get done() { return this.#phase === 'done'; }
  #complete(result) {
    const moreResults = (result.status & 8) !== 0;
    this.#phase = moreResults ? 'header' : 'done';
    this.#columns = [];
    this.#metadataBytes = 0;
    return {kind: 'complete', ...result, moreResults};
  }
  accept(payload) {
    if (this.done) throw new ProtocolError('Result already complete');
    if (this.#phase === 'failed') throw new ProtocolError('Result decoder has failed');
    try {
      return this.#accept(payload);
    } catch (error) {
      if (!(error instanceof SqlError)) {
        this.#phase = 'failed';
        this.#columns = [];
      }
      throw error;
    }
  }
  #accept(payload) {
    if (!(payload instanceof Uint8Array) || payload.length === 0) throw new ProtocolError('Empty result packet');
    if (payload[0] === 0xff) {
      const error = decodeError(payload);
      this.#phase = 'done';
      throw error;
    }
    if (this.#phase === 'header') {
      if (payload[0] === 0) return this.#complete(decodeOK(payload));
      if (payload[0] === 0xfb) throw new ProtocolError('LOCAL INFILE was not negotiated');
      const reader = new Reader(payload);
      const count = reader.lenenc();
      if (count === null || count < 1n || count > BigInt(this.#maxColumns)) {
        throw new ProtocolError('Invalid result column count');
      }
      reader.end();
      this.#columnCount = Number(count);
      this.#phase = 'columns';
      return null;
    }
    if (this.#phase === 'columns') {
      if (payload.length > this.#maxMetadataBytes - this.#metadataBytes) {
        throw new ProtocolError('Result metadata exceeds configured limit');
      }
      this.#columns.push(decodeColumn(payload));
      this.#metadataBytes += payload.length;
      if (this.#columns.length === this.#columnCount) this.#phase = 'columnEOF';
      return null;
    }
    if (this.#phase === 'columnEOF') {
      decodeEOF(payload);
      this.#phase = 'rows';
      return {kind: 'columns', columns: this.#columns};
    }
    if (payload[0] === 0xfe && payload.length < 9) return this.#complete(decodeEOF(payload));
    return {kind: 'row', values: decodeTextRow(payload, this.#columnCount)};
  }
  finish() {
    if (!this.done) throw new ProtocolError('Truncated query result');
  }
}
