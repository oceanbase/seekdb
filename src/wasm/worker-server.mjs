// Copyright (c) 2026 OceanBase. SPDX-License-Identifier: Apache-2.0
import {openRuntime} from './runtime-host.mjs';
import {SqlError} from './mysql-wire.mjs';

export function errorRecord(error) {
  return {name: error.name ?? 'Error', message: error.message ?? String(error),
    code: error.code, sqlState: error.sqlState};
}

// Message handling remains asynchronous: cancel/close must run while a next()
// is waiting for SQL. Only one next() can be outstanding on each result stream.
export function installWorkerServer({listen, send}) {
  let runtime;
  let opening = false;
  let closing = false;
  let serial = 0;
  const sessions = new Map();
  const streams = new Map();
  async function dispatch(message) {
    const {op} = message;
    if (op === 'open') {
      if (opening) throw new Error('Worker already opened a database');
      opening = true;
      const {default: factory} = await import(message.moduleURL);
      runtime = await openRuntime(factory, {budgets: message.budgets,
        onFatal: error => send({fatal: errorRecord(error)})});
      return;
    }
    if (!runtime || closing) throw new Error('Database is not open');
    if (op === 'connect') {
      const client = await runtime.connect(message.options);
      if (closing) { runtime.disconnect(client); throw new Error('Database is closing'); }
      const id = ++serial;
      sessions.set(id, {client, stream: null});
      return id;
    }
    if (op === 'close') {
      closing = true;
      // Close connections first to wake any blocked reader, then let the engine
      // roll back disconnected sessions and join its own services.
      for (const session of sessions.values()) runtime.disconnect(session.client);
      for (const stream of streams.values()) stream.controller.abort();
      sessions.clear();
      streams.clear();
      await runtime.close();
      return;
    }
    if (op === 'next' || op === 'cancel' || op === 'release') {
      const stream = streams.get(message.stream);
      if (!stream) {
        if (op !== 'next') return;
        throw new Error('Result stream is closed');
      }
      if (op !== 'next') {
        streams.delete(message.stream);
        stream.controller.abort();
        runtime.disconnect(stream.session.client);
        sessions.delete(stream.sessionId);
        await stream.iterator.return();
        return;
      }
      if (stream.pending) throw new Error('Result already has a pending read');
      stream.pending = true;
      try {
        const result = await stream.iterator.next();
        if (result.done) {
          streams.delete(message.stream);
          stream.session.stream = null;
        }
        return result;
      } catch (error) {
        streams.delete(message.stream);
        stream.session.stream = null;
        if (!(error instanceof SqlError)) {
          runtime.disconnect(stream.session.client);
          sessions.delete(stream.sessionId);
        }
        throw error;
      } finally { stream.pending = false; }
    }
    const session = sessions.get(message.session);
    if (!session && op === 'disconnect') return;
    if (!session) throw new Error('Session is closed');
    if (op === 'disconnect') {
      runtime.disconnect(session.client);
      sessions.delete(message.session);
      if (session.stream !== null) {
        const stream = streams.get(session.stream);
        streams.delete(session.stream);
        stream?.controller.abort();
        await stream?.iterator.return();
      }
      return;
    }
    if (op === 'query') {
      if (session.stream !== null) throw new Error('Session already has an active query');
      const id = ++serial;
      const controller = new AbortController();
      const iterator = session.client.query(message.sql, {signal: controller.signal});
      session.stream = id;
      streams.set(id, {session, sessionId: message.session, controller, iterator, pending: false});
      return id;
    }
    throw new Error(`Unknown operation: ${op}`);
  }
  listen(message => {
    void dispatch(message).then(value => send({id: message.id, value}),
      error => send({id: message.id, error: errorRecord(error)}));
  });
}
