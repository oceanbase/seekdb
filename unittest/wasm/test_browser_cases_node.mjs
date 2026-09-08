// Copyright (c) 2026 OceanBase. SPDX-License-Identifier: Apache-2.0
// Executes the portable browser cases in Node workers before browser delivery.
import {Worker} from 'node:worker_threads';
import {pathToFileURL} from 'node:url';
import {resolve} from 'node:path';
const moduleURL = pathToFileURL(resolve(process.argv[2]));
const {Database} = await import(new URL('./database.mjs', moduleURL));
const {runDatabaseBrowserCases} = await import(new URL('./database-browser-cases.mjs', moduleURL));
const workers = new Set();
const workerFactory = url => {
  const worker = new Worker(url, {workerData: {serverURL: new URL('./worker-server.mjs', moduleURL).href}});
  workers.add(worker);
  worker.on('exit', () => workers.delete(worker));
  return worker;
};
const timer = setTimeout(() => { console.error('browser-cases-node: timeout'); process.exit(124); }, 120000);
try {
  await runDatabaseBrowserCases({open: () => Database.open({moduleURL, workerFactory,
    workerURL: new URL('./database_node_worker.mjs', import.meta.url)})});
  if (workers.size) throw new Error('Worker leaked after close');
  console.log('browser-cases-node: all assertions passed');
} finally {
  clearTimeout(timer);
  await Promise.all([...workers].map(worker => worker.terminate()));
}
