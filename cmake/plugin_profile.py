#!/usr/bin/env python3
"""Declared native build profile; this is a dependency policy, not a sandbox."""
from __future__ import annotations
import argparse
import json
import pathlib
import re

def profile(manifest: pathlib.Path, source: pathlib.Path) -> dict:
    text = manifest.read_text(encoding="utf-8")
    # Existing public manifests do not need a newer TOML runtime.
    if not re.search(r"^\s*(api_profile|server_headers|exports)\s*=", text, re.M):
        return {"profile": "public", "headers": [], "exports": []}
    try:
        import tomllib
    except ImportError as error:
        raise ValueError("explicit native profiles require Python 3.11+ (tomllib)") from error
    data = tomllib.loads(text)
    selected = data.get("api_profile", "public")
    headers, exports = data.get("server_headers", []), data.get("exports", [])
    if selected not in ("public", "server-dev"):
        raise ValueError("unknown plugin api_profile")
    if not isinstance(headers, list) or not isinstance(exports, list):
        raise ValueError("server_headers and exports must be arrays")
    if selected == "public" and (headers or exports):
        raise ValueError("private headers/extra exports require server-dev profile")
    for header in headers:
        if not isinstance(header, str) or not re.fullmatch(r"[A-Za-z0-9_/.-]+\.(h|hpp)", header):
            raise ValueError("invalid declared server header")
        if any(part in ("", ".", "..") for part in header.split("/")):
            raise ValueError("server header must use a canonical include spelling")
        candidates = [source / root / header for root in ("src", "src/oblib", "src/query/api", "src/data_plane/api")]
        matches = {p.resolve() for p in candidates if p.is_file()}
        if len(matches) != 1 or not next(iter(matches)).is_relative_to((source / "src").resolve()):
            raise ValueError(f"server header missing, ambiguous or outside core: {header}")
    for symbol in exports:
        if not isinstance(symbol, str) or not re.fullmatch(r"[A-Za-z_][A-Za-z0-9_]*", symbol):
            raise ValueError("extra exports must be literal C symbol names")
        if symbol in ("seekdb_plugin_entry_v1", "seekdb_plugin_server_dev_entry_impl"):
            raise ValueError("reserved plugin entry symbol in exports")
    if len(headers) > 256 or len(exports) > 256 or len(set(headers)) != len(headers) or len(set(exports)) != len(exports):
        raise ValueError("duplicate or excessive profile declarations")
    return {"profile": selected, "headers": headers, "exports": exports}

def main():
    parser = argparse.ArgumentParser()
    parser.add_argument("--manifest", required=True, type=pathlib.Path)
    parser.add_argument("--source", required=True, type=pathlib.Path)
    args = parser.parse_args()
    try:
        print(json.dumps(profile(args.manifest, args.source)))
    except (OSError, ValueError) as error:
        parser.error(str(error))

if __name__ == "__main__":
    main()
