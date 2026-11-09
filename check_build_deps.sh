#!/bin/bash
# check_build_deps.sh: 编译前检查SeekDB依赖  bison flex gcc g++ cmake make
deps=("bison" "flex" "gcc" "g++" "cmake" "make")
all_ok=true

for cmd in "${deps[@]}"; do
  if ! command -v "$cmd" &> /dev/null; then
    echo "ERROR: Missing dependency: $cmd"
    all_ok=false
  fi
done

if [ "$all_ok" = true ]; then
  echo "INFO: All build dependencies are ready, you can start compilation."
  exit 0
else
  echo "WARN: Dependencies missing, please install the above packages before compiling"
  exit 1
fi
