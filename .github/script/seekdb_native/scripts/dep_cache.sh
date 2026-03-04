#!/bin/bash
# Stub for native runner: dep_cache 仅在存在 /etc/profile.d/dep_create.sh 时由 frame.sh 加载。
# 此处仅保证 DEP_CACHE_DIR 有默认值，避免 frame prepare 报错。
[[ -z "$DEP_CACHE_DIR" ]] && export DEP_CACHE_DIR="${DEP_CACHE_DIR:-$HOME/../../dep_cache}"
return 0
