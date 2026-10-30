#!/bin/sh
compiler=/root/hipvs-competition-build-20261030/tooling/rust-1.98.1/bin/cargo
if [ ! -x "$compiler" ]; then compiler=/usr/local/bin/cargo; fi
exec "$compiler" --config 'source.mirror.registry="sparse+https://mirrors.tuna.tsinghua.edu.cn/crates.io-index/"' "$@"
