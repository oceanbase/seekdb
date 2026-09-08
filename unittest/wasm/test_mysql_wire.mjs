// Copyright (c) 2026 OceanBase. SPDX-License-Identifier: Apache-2.0
import assert from 'node:assert/strict';
import {execFileSync} from 'node:child_process';
import {createHash} from 'node:crypto';
import {test} from 'node:test';
import {
  PacketDecoder, encodePacket, MAX_FRAME_PAYLOAD, ProtocolError,
  SqlError, decodeColumn, decodeTextRow, decodeOK, TextResultDecoder, decodeGreeting, decodeAuthSwitch,
} from '../../src/wasm/mysql-wire.mjs';
import {encodeLogin, nativePasswordResponse} from '../../src/wasm/mysql-auth.mjs';

assert.ok(process.env.SEEKDB_MYSQL_WIRE_FIXTURE, 'Compile mysql_wire_fixture.rs and set SEEKDB_MYSQL_WIRE_FIXTURE');
const fixture = Object.fromEntries(execFileSync(process.env.SEEKDB_MYSQL_WIRE_FIXTURE, {encoding: 'utf8'})
  .trim().split('\n').map(line => {
    const [name, hex] = line.split(' ');
    return [name, new Uint8Array(Buffer.from(hex, 'hex'))];
  }));
const text = bytes => new TextDecoder('utf-8', {fatal: true}).decode(bytes);

test('Rust frames decode at every transport split, with independent input ownership', () => {
  const frame = fixture.framed_row;
  assert.deepEqual(encodePacket(fixture.row, 255).frames[0], frame);
  for (let split = 0; split <= frame.length; ++split) {
    const decoder = new PacketDecoder({sequence: 255});
    const first = frame.slice(0, split);
    const packets = decoder.push(first);
    first.fill(0);
    packets.push(...decoder.push(frame.subarray(split)));
    decoder.finish();
    assert.equal(decoder.nextSequence, 0);
    assert.deepEqual(packets, [fixture.row]);
  }
});

test('multiple logical packets and bytewise delivery preserve sequence wrap', () => {
  const decoder = new PacketDecoder({sequence: 255});
  const wire = Buffer.concat([fixture.framed_row, encodePacket(fixture.ok, 0).frames[0]]);
  const packets = [];
  for (const byte of wire) packets.push(...decoder.push(new Uint8Array([byte])));
  assert.deepEqual(packets, [fixture.row, fixture.ok]);
  decoder.resetSequence(1);
  assert.deepEqual(decoder.push(encodePacket(new Uint8Array(), 1).frames[0]), [new Uint8Array()]);
});

test('0xffffff-byte continuation requires the terminal empty packet', () => {
  const payload = new Uint8Array(MAX_FRAME_PAYLOAD).fill(73);
  const {frames, nextSequence} = encodePacket(payload, 255);
  assert.equal(frames.length, 2);
  assert.deepEqual(frames[1], new Uint8Array([0, 0, 0, 0]));
  assert.equal(nextSequence, 1);
  const decoder = new PacketDecoder({sequence: 255});
  assert.deepEqual(decoder.push(frames[0]), []);
  assert.deepEqual(decoder.push(frames[1]), [payload]);
  decoder.finish();
  const incomplete = new PacketDecoder({sequence: 255});
  incomplete.push(frames[0]);
  assert.throws(() => incomplete.finish(), /Truncated/);
});

test('malformed streams fail before oversized allocation and cannot resume', () => {
  const decoder = new PacketDecoder({maxPacketBytes: 8});
  assert.throws(() => decoder.push(new Uint8Array([9, 0, 0, 0])), /limit/);
  assert.throws(() => decoder.push(new Uint8Array()), /failed/);
  assert.throws(() => new PacketDecoder().push(new Uint8Array([0, 0, 0, 1])), /sequence/);
  const partial = new PacketDecoder();
  partial.push(new Uint8Array([1, 0, 0, 0]));
  assert.throws(() => partial.resetSequence(0), /Truncated/);
});

test('production columns, binary cells, SQL NULL, and uint64 digits retain exact values', () => {
  const column = decodeColumn(fixture.integer_column);
  assert.equal(text(column.name), '编号');
  assert.equal(column.type, 8);
  assert.equal(column.flags, 32);
  const values = decodeTextRow(fixture.row, 3);
  assert.equal(BigInt(text(values[0])), 18446744073709551615n);
  assert.deepEqual(values[1], new Uint8Array([0, 255, 128, 1]));
  assert.equal(values[2], null);
  const mutable = fixture.row.slice();
  const owned = decodeTextRow(mutable, 3);
  mutable.fill(0);
  assert.deepEqual(owned, values);
  const ok = decodeOK(fixture.ok);
  assert.equal(ok.affectedRows, 9007199254740993n);
  assert.equal(ok.lastInsertId, 18446744073709551614n);
});

test('length-encoded values reject truncation, invalid prefixes, and trailing cells', () => {
  for (const bytes of [[0xff], [0xfc, 1], [0xfd, 1, 0], [0xfe, 1], [3, 65], [0, 0]]) {
    assert.throws(() => decodeTextRow(new Uint8Array(bytes), 1), ProtocolError);
  }
  assert.deepEqual(decodeTextRow(new Uint8Array([0]), 1), [new Uint8Array()]);
  // A row starting with 0xfe and a nine-byte length is not an EOF marker.
  assert.deepEqual(decodeTextRow(new Uint8Array([0xfe, 1, 0, 0, 0, 0, 0, 0, 0, 65]), 1), [new Uint8Array([65])]);
});

function columns(result) {
  assert.equal(result.accept(fixture.header), null);
  for (const key of ['integer_column', 'binary_column', 'nullable_column']) {
    assert.equal(result.accept(fixture[key]), null);
  }
  const event = result.accept(fixture.eof);
  assert.equal(event.kind, 'columns');
  assert.equal(event.columns.length, 3);
}

test('streamed result state accepts rows and multiple results without losing status', () => {
  const result = new TextResultDecoder();
  columns(result);
  const row = result.accept(fixture.row);
  assert.equal(row.kind, 'row');
  assert.deepEqual(row.values, decodeTextRow(fixture.row, 3));
  const first = result.accept(fixture.more_eof);
  assert.equal(first.moreResults, true);
  assert.equal(first.warnings, 1);
  assert.equal(result.done, false);
  const last = result.accept(fixture.ok);
  assert.equal(last.affectedRows, 9007199254740993n);
  assert.equal(result.done, true);
  result.finish();
  assert.throws(() => result.accept(fixture.row), /complete/);
});

test('SQL errors preserve code and SQLSTATE, including after row delivery', () => {
  for (const inRows of [false, true]) {
    const result = new TextResultDecoder();
    if (inRows) { columns(result); result.accept(fixture.row); }
    assert.throws(() => result.accept(fixture.error), error =>
      error instanceof SqlError && error.code === 1064 && error.sqlState === '42000' && error.message === 'invalid SQL');
    result.finish();
  }
});

test('incomplete query, excessive columns, and unnegotiated local file requests fail', () => {
  const result = new TextResultDecoder();
  columns(result);
  assert.throws(() => result.finish(), /Truncated/);
  assert.throws(() => new TextResultDecoder({maxColumns: 2}).accept(fixture.header), /column count/);
  assert.throws(() => new TextResultDecoder().accept(new Uint8Array([0xfb])), /LOCAL INFILE/);
});

test('metadata budget is cumulative and malformed results cannot resume', () => {
  const result = new TextResultDecoder({maxMetadataBytes: fixture.integer_column.length});
  result.accept(fixture.header);
  result.accept(fixture.integer_column);
  assert.throws(() => result.accept(fixture.binary_column), /metadata.*limit/);
  assert.throws(() => result.accept(fixture.eof), /failed/);
  assert.equal(result.done, false);
});

function greeting() {
  const packets = new PacketDecoder().push(fixture.greeting);
  assert.equal(packets.length, 1);
  return decodeGreeting(packets[0]);
}

function parsedLogin(payload) {
  return Object.fromEntries(execFileSync(process.env.SEEKDB_MYSQL_WIRE_FIXTURE,
    ['parse-login'], {input: payload, encoding: 'utf8'}).trimEnd().split('\n').map(line => {
      const [key, hex = ''] = line.split(' ');
      return [key, new Uint8Array(Buffer.from(hex, 'hex'))];
    }));
}

test('production greeting retains unsigned connection id and all binary challenge bytes', () => {
  const server = greeting();
  assert.equal(server.version, '5.7.25');
  assert.equal(server.connectionId, 0xfedcba98);
  assert.equal(server.scramble.length, 20);
  assert.equal(server.scramble[0], 0);
  assert.equal(server.scramble[18], 18);
  assert.equal(server.scramble[19], 0);
  assert.equal(server.plugin, 'mysql_native_password');
  assert.equal(server.status, 2);
  const switched = decodeAuthSwitch(fixture.auth_switch);
  assert.deepEqual(switched, {plugin: server.plugin, scramble: server.scramble});
});

test('browser login is accepted by production Rust parser with constrained capabilities', async () => {
  const login = await encodeLogin(greeting(), {username: 'root', database: '测试', maxPacketBytes: 123456});
  const parsed = parsedLogin(login.payload);
  assert.equal(text(parsed.user), 'root');
  assert.equal(text(parsed.db), '测试');
  assert.equal(text(parsed.plugin), 'mysql_native_password');
  assert.deepEqual(parsed.auth, new Uint8Array());
  assert.equal(parsed.charset[0], 45);
  assert.equal(new DataView(parsed.capabilities.buffer).getUint32(0, true), login.capabilities);
  assert.equal(new DataView(login.payload.buffer).getUint32(4, true), 123456);
  assert.equal(login.capabilities & (0x20 | 0x80 | 0x800 | 0x800000 | 0x1000000), 0);
  assert.equal(Object.hasOwn(parsedLogin((await encodeLogin(greeting())).payload), 'db'), false);
});

test('native-password response matches server-side recovery, without sending password bytes', async () => {
  const password = 'test-口令';
  const server = greeting();
  const {payload} = await encodeLogin(server, {password});
  const response = parsedLogin(payload).auth;
  const sha1 = bytes => createHash('sha1').update(bytes).digest();
  const stage2 = sha1(sha1(Buffer.from(password)));
  const mask = sha1(Buffer.concat([server.scramble, stage2]));
  const recovered = Buffer.from(response.map((byte, i) => byte ^ mask[i]));
  assert.deepEqual(sha1(recovered), stage2);
  assert.equal(response.length, 20);
  assert.equal(Buffer.from(payload).includes(Buffer.from(password)), false);
  assert.deepEqual(await nativePasswordResponse(password, decodeAuthSwitch(fixture.auth_switch)), response);
});

test('authentication rejects malformed greetings, unsupported plugins and missing capabilities', async () => {
  const server = greeting();
  await assert.rejects(encodeLogin({...server, plugin: 'unknown'}), /Unsupported authentication/);
  await assert.rejects(encodeLogin({...server, capabilities: 0}), /capabilities/);
  await assert.rejects(encodeLogin(server, {username: 'root\0other'}), /username/);
  await assert.rejects(encodeLogin(server, {database: 'x\0y'}), /database/);
  await assert.rejects(nativePasswordResponse('x', server, {}), /Web Crypto/);
  const payload = new PacketDecoder().push(fixture.greeting)[0];
  for (let length = 0; length < payload.length; ++length) {
    assert.throws(() => decodeGreeting(payload.subarray(0, length)));
  }
  assert.throws(() => decodeAuthSwitch(fixture.auth_switch.subarray(0, 10)), /Unterminated/);
});
