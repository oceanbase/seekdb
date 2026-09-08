// Copyright (c) 2026 OceanBase. SPDX-License-Identifier: Apache-2.0
import {ProtocolError} from './mysql-wire.mjs';

const PROTOCOL_41 = 0x200;
const TRANSACTIONS = 0x2000;
const SECURE_CONNECTION = 0x8000;
const MULTI_RESULTS = 0x20000;
const PLUGIN_AUTH = 0x80000;
const CONNECT_WITH_DB = 8;
const REQUIRED = PROTOCOL_41 | TRANSACTIONS | SECURE_CONNECTION | PLUGIN_AUTH;
const OPTIONAL = 1 | 4 | MULTI_RESULTS; // LONG_PASSWORD, LONG_FLAG, MULTI_RESULTS.
const PLUGIN = 'mysql_native_password';
const encoder = new TextEncoder();

function cstring(value, name) {
  if (typeof value !== 'string' || value.includes('\0')) throw new TypeError(`Invalid ${name}`);
  return encoder.encode(value + '\0');
}

function join(parts) {
  const bytes = new Uint8Array(parts.reduce((total, part) => total + part.length, 0));
  let offset = 0;
  for (const part of parts) { bytes.set(part, offset); offset += part.length; }
  return bytes;
}

// SHA-1 here implements MySQL's existing native-password challenge protocol.
// It is not used as a new password-storage format. The password is never sent.
export async function nativePasswordResponse(password, {plugin, scramble}, crypto = globalThis.crypto) {
  if (plugin !== PLUGIN) throw new ProtocolError(`Unsupported authentication plugin: ${plugin}`);
  if (!(scramble instanceof Uint8Array) || scramble.length !== 20) throw new ProtocolError('Invalid authentication challenge');
  if (typeof password !== 'string') throw new TypeError('Password must be a string');
  if (password.length === 0) return new Uint8Array();
  if (!crypto?.subtle) throw new Error('Web Crypto is required for password authentication');
  // Copy before awaiting: later transport reads must not mutate this challenge.
  const challenge = scramble.slice();
  const passwordBytes = encoder.encode(password);
  let first, second, combined, digest;
  try {
    first = new Uint8Array(await crypto.subtle.digest('SHA-1', passwordBytes));
    second = new Uint8Array(await crypto.subtle.digest('SHA-1', first));
    combined = join([challenge, second]);
    digest = new Uint8Array(await crypto.subtle.digest('SHA-1', combined));
    const response = new Uint8Array(20);
    for (let i = 0; i < 20; ++i) response[i] = first[i] ^ digest[i];
    return response;
  } finally {
    passwordBytes.fill(0);
    for (const bytes of [first, second, combined, digest]) bytes?.fill(0);
  }
}

// Only negotiate capabilities implemented by the text codec. In particular,
// no compression, LOCAL INFILE, session tracking, TLS or deprecated EOF flags.
export async function encodeLogin(greeting, {
  username = 'root', password = '', database, maxPacketBytes = 64 * 1024 * 1024,
} = {}) {
  if (!Number.isInteger(greeting.capabilities) || (greeting.capabilities & REQUIRED) !== REQUIRED) {
    throw new ProtocolError('Server lacks required MySQL 4.1 capabilities');
  }
  if (!Number.isInteger(maxPacketBytes) || maxPacketBytes < 1 || maxPacketBytes > 0xffffffff) {
    throw new RangeError('Invalid maximum packet size');
  }
  const user = cstring(username, 'username');
  const db = database === undefined ? null : cstring(database, 'database');
  let capabilities = (REQUIRED | OPTIONAL) & greeting.capabilities;
  if (db) {
    if (!(greeting.capabilities & CONNECT_WITH_DB)) throw new ProtocolError('Server does not support a login database');
    capabilities |= CONNECT_WITH_DB;
  }
  const auth = await nativePasswordResponse(password, greeting);
  const fixed = new Uint8Array(32);
  const view = new DataView(fixed.buffer);
  view.setUint32(0, capabilities, true);
  view.setUint32(4, maxPacketBytes, true);
  fixed[8] = 45; // utf8mb4_general_ci; query text is encoded as UTF-8.
  const parts = [fixed, user, new Uint8Array([auth.length]), auth];
  if (db) parts.push(db);
  parts.push(cstring(PLUGIN, 'plugin'));
  const payload = join(parts);
  auth.fill(0);
  return {payload, capabilities};
}
