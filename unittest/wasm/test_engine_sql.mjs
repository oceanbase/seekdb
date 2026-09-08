// Copyright (c) 2026 OceanBase. SPDX-License-Identifier: Apache-2.0
// End-to-end probe: actual ObServer, production NIO callbacks and SQL workers.
import assert from 'node:assert/strict';
import {createRequire} from 'node:module';
import {resolve} from 'node:path';
import {MySQLConnection} from '../../src/wasm/mysql-client.mjs';
import {WasmMemoryTransport} from '../../src/wasm/mysql-transport.mjs';
import {SqlError} from '../../src/wasm/mysql-wire.mjs';

const executable = resolve(process.argv[2]);
const start = performance.now();
process.argv = [process.execPath, executable, '--client-service'];
const module = createRequire(import.meta.url)(executable);
const initialized = new Promise(resolve => { module.onRuntimeInitialized = resolve; });
const timeout = setTimeout(() => {
  console.error('sql-probe: timed out');
  process.exit(124);
}, 120000);
timeout.unref();
const pause = () => new Promise(resolve => setTimeout(resolve, 10));
const text = new TextDecoder();
let connection;
const clients = [];
function createConnection() {
  const client = new MySQLConnection(new WasmMemoryTransport(module, module._seekdb_probe_connect(257), {chunkBytes: 31}));
  clients.push(client);
  return client;
}
async function consume(client, sql, options) {
  for await (const event of client.query(sql, options)) { /* release every event */ }
}
let success = false;
let ready = false;
try {
  await initialized;
  ready = true;
  while (module._seekdb_probe_state() === 0) await pause();
  assert.equal(module._seekdb_probe_state(), 1);
  module.getValue(0, 'i8'); // Refresh views after growth by a pthread.
  console.log(`sql-probe: ready milliseconds ${Math.round(performance.now() - start)}, linear memory bytes ${module.HEAPU8.byteLength}`);
  console.log('sql-probe: connecting');
  // Force fragmentation/backpressure through the real production reactor.
  connection = createConnection();
  await connection.authenticate();
  console.log('sql-probe: authenticated');
  async function query(sql) {
    const rows = [];
    for await (const event of connection.query(sql)) {
      if (event.kind === 'row') rows.push(event.values.map(value => value === null ? null : text.decode(value)));
    }
    console.log(`sql-probe: completed ${sql}`);
    return rows;
  }
  assert.deepEqual(await query('SELECT 1'), [['1']]);
  assert.deepEqual(await query("SELECT CAST(18446744073709551615 AS UNSIGNED), NULL, '中文'"), [['18446744073709551615', null, '中文']]);
  await assert.rejects(query('SELECT FROM'), SqlError);
  assert.deepEqual(await query('SELECT 2'), [['2']]);
  await query('CREATE DATABASE wasm_probe');
  await query('USE wasm_probe');
  await query('CREATE TABLE items (id BIGINT PRIMARY KEY, value VARCHAR(100))');
  await query("INSERT INTO items VALUES (1, 'original'), (2, 'delete')");
  await query('BEGIN');
  await query("UPDATE items SET value = 'rolled back' WHERE id = 1");
  await query('ROLLBACK');
  assert.deepEqual(await query('SELECT value FROM items WHERE id = 1'), [['original']]);
  await query('BEGIN');
  await query("UPDATE items SET value = 'committed' WHERE id = 1");
  await query('DELETE FROM items WHERE id = 2');
  await query('COMMIT');
  assert.deepEqual(await query('SELECT id, value FROM items ORDER BY id'), [['1', 'committed']]);
  await assert.rejects(query('SELECT FROM'), SqlError);
  assert.deepEqual(await query('SELECT COUNT(*) FROM items'), [['1']]);
  const invalidLogin = createConnection();
  await assert.rejects(invalidLogin.authenticate({password: 'incorrect'}),
    error => error instanceof SqlError && error.code === 1045);
  console.log('sql-probe: invalid authentication rejected');

  const rollbackClient = await createConnection().authenticate({database: 'wasm_probe'});
  await consume(rollbackClient, 'BEGIN');
  await consume(rollbackClient, "UPDATE items SET value = 'disconnect' WHERE id = 1");
  rollbackClient.close();
  rollbackClient.close();
  assert.deepEqual(await query('SELECT value FROM items WHERE id = 1'), [['committed']]);
  await query("UPDATE items SET value = 'after disconnect' WHERE id = 1");
  console.log('sql-probe: disconnected transaction released its write lock');

  const abandoned = await createConnection().authenticate();
  const iterator = abandoned.query('SELECT 1 UNION ALL SELECT 2');
  assert.equal((await iterator.next()).value.kind, 'columns');
  await iterator.return();
  await assert.rejects(consume(abandoned, 'SELECT 1'), /not open/);
  console.log('sql-probe: abandoned result closed its connection');

  const cancelled = await createConnection().authenticate();
  const controller = new AbortController();
  const pending = consume(cancelled, 'SELECT SLEEP(1)', {signal: controller.signal});
  const cancelTimer = setTimeout(() => controller.abort(), 20);
  try { await assert.rejects(pending, {name: 'AbortError'}); }
  finally { clearTimeout(cancelTimer); }
  await assert.rejects(consume(cancelled, 'SELECT 1'), /not open/);
  assert.deepEqual(await query('SELECT 3'), [['3']]);
  console.log('sql-probe: cancellation closed its connection and preserved peers');
  await query('DROP DATABASE wasm_probe');
  success = true;
  console.log('sql-probe: all assertions passed');
} catch (error) {
  console.error('sql-probe: failed', error);
} finally {
  for (const client of clients) client.close();
  if (ready) {
    module.getValue(0, 'i8');
    console.log(`sql-probe: final linear memory bytes ${module.HEAPU8.byteLength}`);
  }
  if (ready) module._seekdb_probe_finish(success ? 1 : 0);
  // Let ObServer stop, join and destroy; its process status is part of the test.
}
