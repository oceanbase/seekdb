// Copyright (c) 2026 OceanBase. SPDX-License-Identifier: Apache-2.0
import {installWorkerServer} from './worker-server.mjs';
installWorkerServer({listen: handler => { self.onmessage = event => handler(event.data); },
  send: message => self.postMessage(message)});
