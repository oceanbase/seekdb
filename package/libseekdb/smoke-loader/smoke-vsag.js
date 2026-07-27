#!/usr/bin/env node
/**
 * Minimal smoke: vsag VECTOR INDEX + DBMS_HYBRID_SEARCH (same as nodejs_napi heavy tests).
 * Run from unpacked zip dir after seekdb.node is built (see test-packed-artifact-smoke.sh).
 */
'use strict';

const fs = require('fs');
const os = require('os');
const path = require('path');
// Resolved from cwd (unpacked zip tree), not __dirname (smoke-loader/).
const nodePath = path.join(process.cwd(), 'seekdb.node');
const seekdb = require(nodePath);

const dbDir = process.argv[2] || path.join(__dirname, 'smoke-vsag.db');

function bindingExitProbe(line) {
  if (process.env.SEEKDB_BINDING_EXIT_PROBE !== '1' && process.env.SEEKDB_NODE_BINDING_PROBE !== '1') {
    return;
  }
  try {
    const probePath = path.join(os.tmpdir(), `seekdb_binding_exit_probe_${process.pid}.log`);
    const fd = fs.openSync(probePath, 'a');
    try {
      fs.writeSync(fd, `${line}\n`);
      fs.fsyncSync(fd);
    } finally {
      fs.closeSync(fd);
    }
  } catch (_) {
    /* ignore */
  }
}

function exitWithCode(code) {
  bindingExitProbe(`before_process_exit code=${code}`);
  if (process.env.SEEKDB_BINDING_EXIT_PROBE === '1' || process.env.SEEKDB_NODE_BINDING_PROBE === '1') {
    process.exit(code);
  }
  try {
    seekdb.close();
  } catch (_) {
    /* ignore */
  }
  process.exit(code);
}

function fail(msg) {
  console.error('::error::', msg);
  exitWithCode(1);
}

function run() {
  console.log('[smoke-vsag] open', dbDir);
  seekdb.open(dbDir);

  let conn = null;
  try {
    conn = seekdb.connect('test', true);
    try {
      seekdb.query(conn, 'DROP TABLE IF EXISTS doc_table');
    } catch (_) { /* ignore */ }

    seekdb.query(
      conn,
      `CREATE TABLE doc_table (
            c1 INT,
            vector VECTOR(3),
            query VARCHAR(255),
            content VARCHAR(255),
            VECTOR INDEX idx1(vector) WITH (distance=l2, type=hnsw, lib=vsag),
            FULLTEXT idx2(query),
            FULLTEXT idx3(content)
        )`,
    );

    seekdb.query(
      conn,
      `INSERT INTO doc_table VALUES
            (1, '[1,2,3]', 'hello world', 'oceanbase Elasticsearch database'),
            (2, '[1,2,1]', 'hello world, what is your name', 'oceanbase mysql database')`,
    );

    const searchParams = JSON.stringify({
      query: { bool: { should: [{ match: { query: 'hello' } }] } },
      knn: { field: 'vector', k: 5, query_vector: [1, 2, 3] },
      _source: ['c1', 'query', 'content'],
    }).replace(/'/g, "''");

    const rows = seekdb
      .query(
        conn,
        `SELECT DBMS_HYBRID_SEARCH.SEARCH('doc_table', '${searchParams}') as result`,
      )
      .fetchAll();

    if (!rows.length) {
      fail('DBMS_HYBRID_SEARCH.SEARCH returned no rows');
    }
    console.log('[smoke-vsag] SEARCH ok, rows:', rows.length);
    seekdb.connectClose(conn);
    conn = null;
  } catch (e) {
    if (conn) {
      try {
        seekdb.connectClose(conn);
      } catch (_) { /* ignore */ }
    }
    fail(e.message || String(e));
  }

  console.log('[smoke-vsag] passed');
  exitWithCode(0);
}

try {
  run();
} catch (e) {
  fail(e.stack || e.message || String(e));
}
