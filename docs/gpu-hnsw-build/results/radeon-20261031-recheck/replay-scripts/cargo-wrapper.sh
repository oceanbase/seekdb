#!/bin/sh
compiler=/workspace/hipvs-recheck-20261031/build-area/tooling/rust-1.98.1/bin/cargo
if [ ! -x "$compiler" ]; then compiler=/usr/local/bin/cargo; fi
exec "$compiler" --config 'source.mirror.registry="sparse+https://mirrors.tuna.tsinghua.edu.cn/crates.io-index/"' "$@"
