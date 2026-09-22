// Copyright (c) 2025 OceanBase.
//
// Licensed under the Apache License, Version 2.0 (the "License");
// you may not use this file except in compliance with the License.
// You may obtain a copy of the License at
//
//     http://www.apache.org/licenses/LICENSE-2.0
//
// Unless required by applicable law or agreed to in writing, software
// distributed under the License is distributed on an "AS IS" BASIS,
// WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
// See the License for the specific language governing permissions and
// limitations under the License.

const LOCK_WAIT_MS = 15000;
const ACCESS_HANDLE_LOCK_ERRORS = ['NoModificationAllowedError', 'InvalidStateError'];

async function retryLocked(operation, deadline, errorNames = ['NoModificationAllowedError']) {
  for (;;) {
    try {
      return await operation();
    } catch (error) {
      if (!errorNames.includes(error.name)) throw error;
      if (performance.now() >= deadline) {
        throw new Error(`The stored database files did not become available (${error.name}: ${error.message}). Try New Instance again.`);
      }
      await new Promise(resolve => setTimeout(resolve, 100));
    }
  }
}

async function waitForFiles(directory, deadline) {
  for await (const handle of directory.values()) {
    if (handle.kind === 'directory') {
      await waitForFiles(handle, deadline);
    } else {
      const access = await retryLocked(() => handle.createSyncAccessHandle(), deadline, ACCESS_HANDLE_LOCK_ERRORS);
      access.close();
    }
  }
}

async function clearStorage() {
  const deadline = performance.now() + LOCK_WAIT_MS;
  const root = await navigator.storage.getDirectory();
  await waitForFiles(root, deadline);
  const names = [];
  for await (const name of root.keys()) names.push(name);
  for (const name of names) {
    await retryLocked(() => root.removeEntry(name, {recursive: true}), deadline);
  }
  for await (const name of root.keys()) {
    throw new Error(`The stored database still contains ${name}. Try New Instance again.`);
  }
}

self.onmessage = async ({data}) => {
  self.onmessage = null;
  try {
    if (data.op !== 'clear') throw new Error('Unknown storage cleanup operation');
    await clearStorage();
    self.postMessage({cleared: true});
  } catch (error) {
    self.postMessage({error: {name: error.name, message: error.message}});
  }
};
