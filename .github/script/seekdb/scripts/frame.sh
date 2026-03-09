#!/usr/bin/env bash
# 最小 frame：供 seekdb native CI（GitHub Actions）使用，无 Jenkins/Farm 依赖（无 dmidecode、/etc/hosts、git clone）。
# 仅提供 init/prepare/run/clean 壳，run 实际执行 obd_run_mysqltest。
function init() {
    ulimit -s 10240 2>/dev/null || true
    ulimit -c unlimited 2>/dev/null || true
    ulimit -n 655350 2>/dev/null || true
    return 0
}
function prepare() { return 0; }
function run() { obd_run_mysqltest "$@"; return $?; }
function clean() { return 0; }
function main() {
    init && prepare && run "$@" && clean
    return $?
}
