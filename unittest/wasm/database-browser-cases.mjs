// Copyright (c) 2026 OceanBase. SPDX-License-Identifier: Apache-2.0
import {Database, SqlError} from './database.mjs';

export async function runDatabaseBrowserCases({
  open = () => Database.open({moduleURL: new URL('./seekdb_wasm_database.mjs', import.meta.url)}),
  report = console.log,
} = {}) {
  const equal = (actual, expected) => {
    if (JSON.stringify(actual) !== JSON.stringify(expected)) {
      throw new Error(`Expected ${JSON.stringify(expected)}, received ${JSON.stringify(actual)}`);
    }
  };
  const rejects = async (promise, match) => {
    try { await promise; }
    catch (error) { if (match(error)) return; throw error; }
    throw new Error('Expected operation to reject');
  };
  const decoder = new TextDecoder();
  async function query(session, sql, options) {
    const rows = [];
    for await (const event of session.query(sql, options)) {
      if (event.kind === 'row') rows.push(event.values.map(value => value === null ? null : decoder.decode(value)));
      if (event.kind === 'complete' && 'affectedRows' in event) equal(typeof event.affectedRows, 'bigint');
    }
    return rows;
  }
  let db;
  try {
    const start = performance.now();
    db = await open();
    report(`open: ${Math.round(performance.now() - start)} ms`);
    const session = await db.connect({capacity: 257, chunkBytes: 31});
    equal(await query(session, "SELECT CAST(18446744073709551615 AS UNSIGNED), NULL, '中文'"),
      [['18446744073709551615', null, '中文']]);
    await rejects(query(session, 'SELECT FROM'), error => error instanceof SqlError && typeof error.code === 'number');
    equal(await query(session, 'SELECT 2'), [['2']]);
    await query(session, 'CREATE DATABASE browser_test');
    await query(session, 'USE browser_test');
    await query(session, 'CREATE TABLE items (id INT PRIMARY KEY, value VARCHAR(64))');
    await query(session, "INSERT INTO items VALUES (1, 'original'), (2, 'delete')");
    await query(session, 'BEGIN');
    await query(session, "UPDATE items SET value = 'rollback' WHERE id = 1");
    await query(session, 'ROLLBACK');
    equal(await query(session, 'SELECT value FROM items WHERE id = 1'), [['original']]);
    await query(session, 'BEGIN');
    await query(session, "UPDATE items SET value = 'commit' WHERE id = 1");
    await query(session, 'DELETE FROM items WHERE id = 2');
    await query(session, 'COMMIT');
    equal(await query(session, 'SELECT id, value FROM items'), [['1', 'commit']]);
    report('PASS: SQL, exact values, error recovery, CRUD, rollback and commit');
    const disconnected = await db.connect({database: 'browser_test'});
    await query(disconnected, 'BEGIN');
    await query(disconnected, "UPDATE items SET value = 'uncommitted' WHERE id = 1");
    await disconnected.close();
    await query(session, "UPDATE items SET value = 'released' WHERE id = 1");
    equal(await query(session, 'SELECT value FROM items WHERE id = 1'), [['released']]);
    const abandoned = await db.connect();
    const iterator = abandoned.query('SELECT 1 UNION ALL SELECT 2');
    equal((await iterator.next()).value.kind, 'columns');
    await iterator.return();
    await abandoned.close();
    const cancelled = await db.connect();
    const controller = new AbortController();
    const active = query(cancelled, 'SELECT SLEEP(1)', {signal: controller.signal});
    const timer = setTimeout(() => controller.abort(), 20);
    try { await rejects(active, error => error.name === 'AbortError'); }
    finally { clearTimeout(timer); }
    await cancelled.close();
    equal(await query(session, 'SELECT 3'), [['3']]);
    report('PASS: disconnect rollback, result release and cancellation preserving peers');
    await db.close();
    await db.close();
    report('PASS: native shutdown and Worker termination');
    db = await open();
    const fresh = await db.connect();
    const databases = await query(fresh, 'SHOW DATABASES');
    equal(databases.some(row => row[0] === 'browser_test'), false);
    equal(await query(fresh, 'SELECT 4'), [['4']]);
    await db.close();
    report('PASS: fresh MEMFS reopen');
  } finally { if (db) await db.close(); }
}
