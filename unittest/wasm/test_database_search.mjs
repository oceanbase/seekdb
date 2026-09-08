// Copyright (c) 2026 OceanBase. SPDX-License-Identifier: Apache-2.0
// Real SQL search paths, using the packaged Worker API and host-supplied vectors.
import assert from 'node:assert/strict';
import {Worker} from 'node:worker_threads';
import {pathToFileURL} from 'node:url';
import {resolve} from 'node:path';
const moduleURL = pathToFileURL(resolve(process.argv[2]));
const {Database} = await import(new URL('./database.mjs', moduleURL));
const workers = new Set();
const workerFactory = url => {
  const worker = new Worker(url, {workerData: {serverURL: new URL('./worker-server.mjs', moduleURL).href}});
  workers.add(worker);
  worker.on('exit', () => workers.delete(worker));
  return worker;
};
const timer = setTimeout(() => { console.error('database-search: timeout'); process.exit(124); }, 120000);
let db;
try {
  db = await Database.open({moduleURL, workerFactory,
    workerURL: new URL('./database_node_worker.mjs', import.meta.url)});
  const session = await db.connect();
  const text = new TextDecoder();
  async function query(sql) {
    console.log(`database-search: query ${sql}`);
    const rows = [];
    for await (const event of session.query(sql)) {
      if (event.kind === 'row') rows.push(event.values.map(value => value === null ? null : text.decode(value)));
    }
    return rows;
  }
  await query('CREATE DATABASE search_test');
  await query('USE search_test');
  assert.deepEqual(await query('SELECT l2_distance([1,0,0], [4,0,0])'), [['3']]);
  await query('CREATE TABLE vectors (id INT PRIMARY KEY, embedding VECTOR(3), VECTOR INDEX hnsw_idx(embedding) WITH (distance=l2, type=hnsw, lib=vsag))');
  await query(`INSERT INTO vectors VALUES ${Array.from({length: 32}, (_, i) => `(${i + 1}, '[${i + 1},0,0]')`).join(',')}`);
  const exact = 'SELECT id FROM vectors ORDER BY l2_distance(embedding, [0,0,0]) LIMIT 3';
  const ann = 'SELECT id FROM vectors ORDER BY l2_distance(embedding, [0,0,0]) APPROXIMATE LIMIT 3';
  assert.deepEqual(await query(exact), [['1'], ['2'], ['3']]);
  assert.deepEqual(await query(ann), [['1'], ['2'], ['3']]);
  const plan = await query(`EXPLAIN ${ann}`);
  console.log('database-search: HNSW plan', JSON.stringify(plan));
  assert.ok(plan.flat().some(line => line.includes('VECTOR INDEX SCAN') && line.includes('hnsw_idx')),
    'ANN query must actually use the HNSW index');
  await query('BEGIN');
  await query("UPDATE vectors SET embedding = '[100,0,0]' WHERE id = 1");
  await query('ROLLBACK');
  assert.deepEqual(await query(ann), [['1'], ['2'], ['3']]);
  await query('BEGIN');
  await query("UPDATE vectors SET embedding = '[100,0,0]' WHERE id = 1");
  await query('DELETE FROM vectors WHERE id = 2');
  await query('COMMIT');
  assert.deepEqual(await query(exact), [['3'], ['4'], ['5']]);
  assert.deepEqual(await query(ann), [['3'], ['4'], ['5']]);
  console.log('database-search: exact distance and SQL HNSW mutation visibility passed');
  await query('CREATE TABLE articles (id INT PRIMARY KEY, body VARCHAR(1024), FULLTEXT INDEX body_idx(body))');
  await query("INSERT INTO articles VALUES (1, 'oceanbase database'), (3, 'oceanbase vector'), (4, 'browser local'), (5, 'oceanbase search')");
  const fulltext = "SELECT id FROM articles WHERE MATCH(body) AGAINST('oceanbase') ORDER BY id";
  assert.deepEqual(await query(fulltext), [['1'], ['3'], ['5']]);
  await query('BEGIN');
  await query("UPDATE articles SET body = 'browser only' WHERE id = 1");
  await query('ROLLBACK');
  assert.deepEqual(await query(fulltext), [['1'], ['3'], ['5']]);
  await query("UPDATE articles SET body = 'browser only' WHERE id = 1");
  await query('DELETE FROM articles WHERE id = 5');
  assert.deepEqual(await query(fulltext), [['3']]);
  console.log('database-search: SQL fulltext mutation visibility passed');
  // Combine ANN candidates and a text predicate in SQL; this establishes a
  // composed search path, not ranking-fusion or large-dataset recall evidence.
  assert.deepEqual(await query(`SELECT candidates.id FROM (${ann}) candidates JOIN articles a ON a.id = candidates.id WHERE MATCH(a.body) AGAINST('oceanbase') ORDER BY candidates.id`), [['3']]);
  await query('DROP DATABASE search_test');
  await db.close();
  assert.equal(workers.size, 0);
  console.log('database-search: all assertions passed');
} catch (error) {
  console.error('database-search: failed', error);
  throw error;
} finally {
  if (db) await db.close().catch(() => {});
  await Promise.all([...workers].map(worker => worker.terminate()));
  clearTimeout(timer);
}
