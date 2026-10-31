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
import gzip
import hashlib
import json
import re
import runpy
import shutil
import subprocess
from functools import partial
from http.server import SimpleHTTPRequestHandler, ThreadingHTTPServer
from pathlib import Path
from urllib.parse import unquote, urlsplit


DEVELOPMENT = runpy.run_path(str(Path(__file__).resolve().parents[2] / "tools/wasm/serve-shell.py"))
ASSETS = DEVELOPMENT["SOURCE_ASSETS"] + DEVELOPMENT["GENERATED_ASSETS"]
CONTENT_TYPES = DEVELOPMENT["ShellHandler"].extensions_map
IMMUTABLE = "public, max-age=31536000, immutable"


def file_record(path, root):
    digest = hashlib.sha256()
    with path.open("rb") as source:
        for chunk in iter(lambda: source.read(1024 * 1024), b""):
            digest.update(chunk)
    return {"path": path.relative_to(root).as_posix(), "bytes": path.stat().st_size,
            "sha256": digest.hexdigest()}


def package(build_dir, output, brotli=False):
    if output.exists() or output.is_symlink():
        raise FileExistsError(f"Output directory already exists: {output}")
    for name in ASSETS:
        if not (build_dir / name).is_file():
            raise FileNotFoundError(f"Missing build asset: {build_dir / name}. Refresh the database target first.")
    brotli_command = shutil.which("brotli") if brotli else None
    if brotli and not brotli_command:
        raise ValueError("--brotli requires an existing brotli executable on PATH; omit it to package gzip only")
    output.mkdir(parents=True)
    try:
        copied = output / "assets"
        copied.mkdir()
        for name in ASSETS:
            shutil.copyfile(build_dir / name, copied / name)
        hashes = {name: file_record(copied / name, copied)["sha256"] for name in sorted(ASSETS)}
        build_id = hashlib.sha256(json.dumps(hashes, sort_keys=True).encode()).hexdigest()
        release = output / "releases" / build_id
        release.parent.mkdir()
        copied.rename(release)
        html = (release / "shell.html").read_text()
        if len(re.findall(r"<head\s*>", html, re.I)) != 1 or re.search(r"<base\b", html, re.I):
            raise ValueError("shell.html must contain one <head> and no <base> element")
        html = re.sub(r"<head\s*>", f'<head>\n  <base href="./releases/{build_id}/">', html, count=1, flags=re.I)
        for name in ("index.html", "shell.html"):
            (output / name).write_text(html)
        records = {}
        for path in [output / "index.html", output / "shell.html", *(release / name for name in ASSETS)]:
            identity = file_record(path, output)
            representations = {"identity": identity}
            compressed = path.with_name(path.name + ".gz")
            with path.open("rb") as source, compressed.open("wb") as target:
                with gzip.GzipFile(filename="", mode="wb", fileobj=target, compresslevel=6, mtime=0) as encoder:
                    shutil.copyfileobj(source, encoder)
            representations["gzip"] = file_record(compressed, output)
            if brotli_command:
                compressed = path.with_name(path.name + ".br")
                subprocess.run([brotli_command, "-q", "5", "-o", str(compressed), str(path)], check=True)
                representations["br"] = file_record(compressed, output)
            records["/" + identity["path"]] = {
                "content_type": CONTENT_TYPES[path.suffix],
                "cache_control": "no-cache" if path.parent == output else IMMUTABLE,
                "representations": representations,
            }
        manifest = {"version": 1, "build_id": build_id, "engine_sha256": hashes["seekdb_wasm_database.wasm"],
                    "input_sha256": hashes, "assets": records}
        (output / "manifest.json").write_text(json.dumps(manifest, indent=2, sort_keys=True) + "\n")
        return manifest
    except BaseException:
        shutil.rmtree(output)
        raise


def load_assets(root):
    root = root.resolve()
    manifest = json.loads((root / "manifest.json").read_text())
    if manifest["version"] != 1:
        raise ValueError("Unsupported release manifest version")
    for asset in manifest["assets"].values():
        for representation in asset["representations"].values():
            path = (root / representation["path"]).resolve()
            if not path.is_relative_to(root) or file_record(path, root) != representation:
                raise ValueError(f"Release asset does not match its manifest: {representation['path']}")
    return manifest["assets"]


def select_encoding(header, available):
    quality = {}
    for entry in header.split(","):
        parts = entry.strip().lower().split(";")
        if not parts[0]:
            continue
        value = 1.0
        for parameter in parts[1:]:
            key, separator, number = parameter.strip().partition("=")
            if key == "q" and separator:
                try:
                    value = float(number)
                except ValueError:
                    value = 0.0
        quality[parts[0]] = value if 0 <= value <= 1 else 0.0
    choices = []
    for preference, encoding in enumerate(("identity", "gzip", "br")):
        if encoding not in available:
            continue
        default = 0.0 if quality.get("*") == 0 else 1.0
        weight = quality.get(encoding, default if encoding == "identity" else quality.get("*", 0.0))
        if weight > 0:
            choices.append((weight, preference, encoding))
    return max(choices)[2] if choices else None


class ReleaseHandler(SimpleHTTPRequestHandler):
    def __init__(self, *args, root, assets, **kwargs):
        self.root = root
        self.assets = assets
        self.cache_control = "no-store"
        super().__init__(*args, **kwargs)

    def end_headers(self):
        self.send_header("Cross-Origin-Opener-Policy", "same-origin")
        self.send_header("Cross-Origin-Embedder-Policy", "require-corp")
        self.send_header("Cache-Control", self.cache_control)
        self.send_header("Vary", "Accept-Encoding")
        super().end_headers()

    def send_head(self):
        self.cache_control = "no-store"
        path = unquote(urlsplit(self.path).path)
        asset = self.assets.get("/index.html" if path == "/" else path)
        if asset is None:
            self.send_error(404, "Unknown release asset")
            return None
        encoding = select_encoding(self.headers.get("Accept-Encoding", ""), asset["representations"])
        if encoding is None:
            self.send_error(406, "No acceptable content encoding")
            return None
        representation = asset["representations"][encoding]
        source = (self.root / representation["path"]).open("rb")
        self.cache_control = asset["cache_control"]
        etag = '"' + representation["sha256"] + '"'
        conditions = [condition.strip() for condition in self.headers.get("If-None-Match", "").split(",")]
        unchanged = any(condition in ("*", etag, "W/" + etag) for condition in conditions)
        self.send_response(304 if unchanged else 200)
        self.send_header("Content-Type", asset["content_type"])
        self.send_header("ETag", etag)
        if encoding != "identity":
            self.send_header("Content-Encoding", encoding)
        if not unchanged:
            self.send_header("Content-Length", str(representation["bytes"]))
        self.end_headers()
        if unchanged:
            source.close()
            return None
        return source


def main():
    parser = argparse.ArgumentParser(description="Package and preview an immutable seekdb WebAssembly shell release.")
    subcommands = parser.add_subparsers(dest="command", required=True)
    pack = subcommands.add_parser("package", help="copy a complete build into a new release directory")
    pack.add_argument("output", type=Path, help="new output directory; existing directories are rejected")
    pack.add_argument("--build-dir", type=Path, default=DEVELOPMENT["ROOT"] / "build/bin/src/wasm/dist")
    pack.add_argument("--brotli", action="store_true", help="also precompress with an existing brotli executable")
    serve = subcommands.add_parser("serve", help="preview a packaged release with compression and isolation headers")
    serve.add_argument("directory", type=Path)
    serve.add_argument("--port", type=int, default=0, help="loopback port; 0 selects a free port (default: 0)")
    args = parser.parse_args()
    try:
        if args.command == "package":
            output = args.output.expanduser().absolute()
            manifest = package(args.build_dir.expanduser().resolve(), output, args.brotli)
            print(f"Release: {output}\nBuild: {manifest['build_id']}\nEngine SHA256: {manifest['engine_sha256']}")
        else:
            if not 0 <= args.port <= 65535:
                parser.error("--port must be between 0 and 65535")
            root = args.directory.expanduser().resolve()
            handler = partial(ReleaseHandler, root=root, assets=load_assets(root))
            with ThreadingHTTPServer(("127.0.0.1", args.port), handler) as server:
                print(f"http://127.0.0.1:{server.server_port}/shell.html", flush=True)
                try:
                    server.serve_forever()
                except KeyboardInterrupt:
                    pass
    except (OSError, ValueError, KeyError, subprocess.CalledProcessError) as error:
        parser.error(str(error))


if __name__ == "__main__":
    main()
