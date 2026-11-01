#!/bin/sh
case "$PWD:$*" in
  "/workspace/amd-acceptance-20261101-6dc14ba3/entry-environment-audit/source/build_release:"*seekdb*)
    echo "[native-entry-probe] reached make in $PWD with args: $*"; exit 42 ;;
  *) exec /usr/bin/make "$@" ;;
esac
