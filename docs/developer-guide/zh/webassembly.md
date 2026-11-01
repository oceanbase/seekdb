# 构建 WebAssembly 版 seekdb

本文介绍如何用 Emscripten 将 seekdb 编译为在浏览器页面中运行的 WebAssembly 引擎，同时生成它的 JavaScript API 和浏览器 SQL shell。

## 前置条件

- macOS 或 Linux 主机
- Emscripten 6.0.12（即 `deps/init/wasm/emscripten-version` 中的版本），且其 `emcc` 在 `PATH` 中
- 已将 `rustup`、Python 3.12 及以上和 `curl` 加入 `PATH`
- 运行测试需要 Node.js 24 及以上
- 建议至少 20 GB 可用磁盘空间，用于依赖、SDK 和构建目录

## 构建

激活固定版本的 Emscripten SDK，准备依赖后用 Bazel 编译：

```bash
# emcc 使用项目固定版本的 Emscripten SDK。
export EMSCRIPTEN=/path/to/emsdk/upstream/emscripten
export PATH="$EMSCRIPTEN:$PATH"

python3 build.py init --wasm
python3 build.py build --config=wasm seekdb
```

`init --wasm` 准备本机依赖、固定版本的 Rust 工具链，以及 `deps/wasm` 下的 Wasm 基础库、适配后的头文件和 Emscripten sysroot。后续只需重复最后一条构建命令，其余源码库和 Rust 库由 Bazel 编译、缓存。只有需要更换 SDK 目录时，才为两条命令设置 `SEEKDB_WASM_DEPS`；它不负责选择目标平台。

引擎和配套 JS、HTML、CSS 文件位于 `build/bin/src/wasm/dist/`。`python3 build.py clean` 清理 Bazel 编译产物，保留 `deps/wasm`。

## 构建目录内容

- `seekdb_wasm_database.mjs` 和 `seekdb_wasm_database.wasm`：引擎、它的 Rust 库和第三方库链接成的一个模块，以及 Emscripten 的 JavaScript 运行时
- `database.mjs`：JavaScript API，以及它加载的 `database-worker.mjs`、`worker-server.mjs`、`runtime-host.mjs`、`storage-cleanup-worker.mjs` 和 `mysql-*.mjs`
- `shell.html`、`shell.css`、`shell.mjs` 和 `shell-*.mjs`：浏览器 SQL shell
- `engine-version.mjs`：shell 显示的引擎版本

这些文件需要通过 `localhost` 或 HTTPS 提供，响应带上 `Cross-Origin-Opener-Policy: same-origin` 和 `Cross-Origin-Embedder-Policy: require-corp`，`.wasm` 的类型为 `application/wasm`：引擎的线程需要 `SharedArrayBuffer`。以 `file://` 方式打开无法运行。

## 在页面中使用

用构建目录启动 shell，打开它输出的 URL：

```bash
python3 tools/wasm/serve-shell.py --build-dir build/bin/src/wasm/dist
```

shell 的命令和存储模式见 `docs/webassembly-shell.md`。

页面通过 `Database` 使用引擎。它在 Worker 中运行引擎，并在内存中用 MySQL 协议与引擎通信：

```js
import {Database} from './database.mjs';

const db = await Database.open({
  moduleURL: new URL('./seekdb_wasm_database.mjs', import.meta.url),
  wasmURL: new URL('./seekdb_wasm_database.wasm', import.meta.url),
  storage: 'opfs',  // 或默认的 'memory'
});
const session = await db.connect();
const text = new TextDecoder();
for await (const event of session.query('SELECT VERSION()')) {
  if (event.kind === 'row') console.log(event.values.map(v => v === null ? null : text.decode(v)));
}
await session.close();
await db.close();
```

- `storage: 'memory'` 把数据目录放在 Wasm 内存中，每次打开都是空的。`storage: 'opfs'` 把它放在 origin private file system 的 `seekdb` 目录中，需要浏览器支持 OPFS 和 Web Locks，同一个 origin 同时只能有一个页面打开。
- `query` 依次产生 `columns`、`row` 和 `complete` 事件。行中每一列的值是该列文本的 `Uint8Array` 字节，SQL NULL 为 `null`。
- 每次 `Database.open` 都会启动新的 Worker 和引擎。`close()` 等待引擎关闭并保留 OPFS 数据；`discard()` 立即终止 Worker，OPFS 模式下还会删除存储的文件；`Database.clearPersistentStorage()` 不打开引擎，直接删除存储的 OPFS 文件。
- 不会打开 TCP 端口，只能通过 `connect()` 连接。
