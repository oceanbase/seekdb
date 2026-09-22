#!/usr/bin/env python3
# Copyright (c) 2025 OceanBase.
#
# Licensed under the Apache License, Version 2.0 (the "License");
# you may not use this file except in compliance with the License.
# You may obtain a copy of the License at
#
#     http://www.apache.org/licenses/LICENSE-2.0
#
# Unless required by applicable law or agreed to in writing, software
# distributed under the License is distributed on an "AS IS" BASIS,
# WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
# See the License for the specific language governing permissions and
# limitations under the License.

import argparse
import os
import shutil
import tempfile
from functools import partial
from http.server import SimpleHTTPRequestHandler, ThreadingHTTPServer
from pathlib import Path
from urllib.parse import unquote, urlsplit


ROOT = Path(__file__).resolve().parents[2]
GENERATED_ASSETS = ("seekdb_wasm_database.mjs", "seekdb_wasm_database.wasm", "engine-version.mjs")
SOURCE_ASSETS = (
    "shell.html", "shell.css", "shell.mjs", "shell-sql.mjs", "shell-examples.mjs", "shell-format.mjs", "database.mjs",
    "database-worker.mjs", "storage-cleanup-worker.mjs", "worker-server.mjs", "runtime-host.mjs",
    "mysql-client.mjs", "mysql-transport.mjs", "mysql-wire.mjs", "mysql-auth.mjs",
)


class ShellHandler(SimpleHTTPRequestHandler):
    extensions_map = {
        **SimpleHTTPRequestHandler.extensions_map,
        ".mjs": "text/javascript",
        ".wasm": "application/wasm",
        ".css": "text/css",
        ".html": "text/html; charset=utf-8",
    }

    def __init__(self, *args, assets, **kwargs):
        self.assets = assets
        super().__init__(*args, **kwargs)

    def asset_path(self):
        path = unquote(urlsplit(self.path).path)
        return "/shell.html" if path == "/" else path

    def send_head(self):
        if self.asset_path() not in self.assets:
            self.send_error(404, "Unknown shell asset")
            return None
        return super().send_head()

    def translate_path(self, path):
        return str(self.assets[self.asset_path()])

    def end_headers(self):
        self.send_header("Cross-Origin-Opener-Policy", "same-origin")
        self.send_header("Cross-Origin-Embedder-Policy", "require-corp")
        self.send_header("Cache-Control", "no-store")
        super().end_headers()


def stage_assets(build_dir):
    for name in GENERATED_ASSETS:
        if not (build_dir / name).is_file():
            raise FileNotFoundError(
                f"Missing {build_dir / name}. Build the seekdb_wasm_database "
                "CMake target, or pass --build-dir with a compatible existing build."
            )
    for name in SOURCE_ASSETS:
        if not (ROOT / "src/wasm" / name).is_file():
            raise FileNotFoundError(f"Missing shell source: {ROOT / 'src/wasm' / name}")
    stage_dir = ROOT / "build_wasm_shell"
    if stage_dir.is_symlink():
        raise ValueError(f"Staging directory must not be a symlink: {stage_dir}")
    stage_dir.mkdir(exist_ok=True)
    assets = {f"/{name}": ROOT / "src/wasm" / name for name in SOURCE_ASSETS}
    for name in GENERATED_ASSETS:
        target = stage_dir / name
        with tempfile.NamedTemporaryFile(dir=stage_dir, delete=False) as temporary:
            temporary_path = Path(temporary.name)
        try:
            shutil.copy2(build_dir / name, temporary_path)
            os.replace(temporary_path, target)
        finally:
            temporary_path.unlink(missing_ok=True)
        assets[f"/{name}"] = target
    return assets


def main():
    parser = argparse.ArgumentParser(
        description="Run the seekdb WebAssembly shell using an existing engine build."
    )
    parser.add_argument(
        "--build-dir", type=Path, default=ROOT / "build_wasm_engine",
        help="directory containing seekdb_wasm_database.mjs and .wasm (default: build_wasm_engine)",
    )
    parser.add_argument("--port", type=int, default=8767, help="loopback port (default: 8767)")
    args = parser.parse_args()
    if not 0 <= args.port <= 65535:
        parser.error("--port must be between 0 and 65535")
    try:
        assets = stage_assets(args.build_dir.expanduser().resolve())
        handler = partial(ShellHandler, assets=assets)
        with ThreadingHTTPServer(("127.0.0.1", args.port), handler) as server:
            print(f"http://127.0.0.1:{server.server_port}/shell.html", flush=True)
            print("SQL runs in your browser. New Instance clears data and starts an empty Memory or OPFS database.", flush=True)
            try:
                server.serve_forever()
            except KeyboardInterrupt:
                pass
    except (OSError, ValueError) as error:
        parser.error(str(error))


if __name__ == "__main__":
    main()
