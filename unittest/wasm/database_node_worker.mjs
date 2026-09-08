// Copyright (c) 2026 OceanBase. SPDX-License-Identifier: Apache-2.0
import {parentPort, workerData} from 'node:worker_threads';
const {installWorkerServer} = await import(workerData.serverURL);
installWorkerServer({listen: handler => parentPort.on('message', handler),
  send: message => parentPort.postMessage(message)});
