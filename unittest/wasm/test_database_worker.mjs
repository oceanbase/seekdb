// Copyright (c) 2026 OceanBase. SPDX-License-Identifier: Apache-2.0
// Actual production module in independent workers, no fake SQL/transport.
import assert from 'node:assert/strict';
import {Worker} from 'node:worker_threads';
import {pathToFileURL} from 'node:url';
import {resolve} from 'node:path';

const moduleURL = pathToFileURL(resolve(process.argv[2])).href;
const {Database, SqlError} = await import(new URL('./database.mjs', moduleURL));
const workerURL = new URL('./database_node_worker.mjs', import.meta.url);
const workers = new Set();
const workerFactory = url => {
  const worker = new Worker(url, {workerData: {serverURL: new URL('./worker-server.mjs', moduleURL).href}});
  workers.add(worker);
  worker.on('exit', () => workers.delete(worker));
  return worker;
};
const open = budgets => Database.open({moduleURL, workerURL, workerFactory, budgets});
const text = new TextDecoder();
async function query(session, sql, options) {
  const rows = [];
  for await (const event of session.query(sql, options)) {
    if (event.kind === 'row') rows.push(event.values.map(value => value === null ? null : text.decode(value)));
    if (event.kind === 'complete' && 'affectedRows' in event) assert.equal(typeof event.affectedRows, 'bigint');
  }
  return rows;
}
const timeout = setTimeout(() => {
  console.error('database-worker: timed out');
  process.exit(124);
}, 120000);
let db;
try {
  await assert.rejects(open({allocatorMiB: 2048}), RangeError);
  assert.equal(workers.size, 0, 'invalid host budgets must fail before allocating a Worker');
  await assert.rejects(Database.open({moduleURL: new URL('./missing-runtime.mjs', import.meta.url), workerURL, workerFactory}));
  assert.equal(workers.size, 0, 'module load failure must terminate the Worker');
  db = await open();
  console.log('database-worker: opened');
  await assert.rejects(db.connect({password: 'incorrect'}), error => error instanceof SqlError && error.code === 1045);
  await assert.rejects(db.connect({capacity: 0}), {name: 'RangeError'});
  const session = await db.connect({capacity: 257, chunkBytes: 31});
  assert.deepEqual(await query(session, "SELECT CAST(18446744073709551615 AS UNSIGNED), NULL, '中文'"),
    [['18446744073709551615', null, '中文']]);
  await query(session, 'CREATE DATABASE worker_test');
  await query(session, 'USE worker_test');
  await query(session, 'CREATE TABLE items (id INT PRIMARY KEY, value VARCHAR(64))');
  await query(session, "INSERT INTO items VALUES (1, 'original'), (2, 'delete')");
  await query(session, 'BEGIN');
  await query(session, "UPDATE items SET value = 'rollback' WHERE id = 1");
  await query(session, 'ROLLBACK');
  assert.deepEqual(await query(session, 'SELECT value FROM items WHERE id = 1'), [['original']]);
  await query(session, 'BEGIN');
  await query(session, "UPDATE items SET value = 'commit' WHERE id = 1");
  await query(session, 'DELETE FROM items WHERE id = 2');
  await query(session, 'COMMIT');
  assert.deepEqual(await query(session, 'SELECT id, value FROM items'), [['1', 'commit']]);
  await assert.rejects(query(session, 'SELECT FROM'), SqlError);
  assert.deepEqual(await query(session, 'SELECT 2'), [['2']]);
  console.log('database-worker: SQL, exact values and transactions passed');

  const disconnected = await db.connect({database: 'worker_test'});
  await query(disconnected, 'BEGIN');
  await query(disconnected, "UPDATE items SET value = 'uncommitted' WHERE id = 1");
  await disconnected.close();
  await disconnected.close();
  await query(session, "UPDATE items SET value = 'after disconnect' WHERE id = 1");
  assert.deepEqual(await query(session, 'SELECT value FROM items WHERE id = 1'), [['after disconnect']]);

  const abandoned = await db.connect();
  const rows = abandoned.query('SELECT 1 UNION ALL SELECT 2');
  assert.equal((await rows.next()).value.kind, 'columns');
  await assert.rejects(query(abandoned, 'SELECT 2'), /active query/);
  await rows.return();
  await abandoned.close();
  await assert.rejects(query(abandoned, 'SELECT 1'), /closed/);

  const cancelled = await db.connect();
  const controller = new AbortController();
  const pending = query(cancelled, 'SELECT SLEEP(1)', {signal: controller.signal});
  const cancelTimer = setTimeout(() => controller.abort(), 20);
  try { await assert.rejects(pending, {name: 'AbortError'}); }
  finally { clearTimeout(cancelTimer); }
  await cancelled.close();
  assert.deepEqual(await query(session, 'SELECT 3'), [['3']]);
  console.log('database-worker: disconnect, release and cancellation passed');
  const closing = db.close();
  assert.equal(db.close(), closing);
  await closing;
  assert.equal(workers.size, 0);
  console.log('database-worker: shutdown and Worker termination passed');

  db = await open();
  const fresh = await db.connect();
  const databases = await query(fresh, 'SHOW DATABASES');
  assert.equal(databases.some(row => row[0] === 'worker_test'), false, 'MEMFS reopen must be documented as empty');
  assert.deepEqual(await query(fresh, 'SELECT 4'), [['4']]);
  // Closing the database wakes a result reader and joins the native engine.
  const active = query(fresh, 'SELECT SLEEP(1)');
  const rejected = assert.rejects(active);
  await new Promise(resolve => setTimeout(resolve, 20));
  await db.close();
  await rejected;
  assert.equal(workers.size, 0);
  console.log('database-worker: fresh reopen and close during query passed');
  await assert.rejects(open({logDiskMiB: 256}), /startup failed/);
  assert.equal(workers.size, 0);
  console.log('database-worker: startup failure cleaned up');
  console.log('database-worker: all assertions passed');
} finally {
  if (db) await db.close().catch(() => {});
  await Promise.all([...workers].map(worker => worker.terminate()));
  clearTimeout(timeout);
}
