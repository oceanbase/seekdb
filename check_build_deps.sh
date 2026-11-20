#!/bin/bash
deps=("bison" "flex" "gcc" "g++" "cmake" "make")
all_ok=0
for pkg in "${deps[@]}";do
  if ! command -v $pkg &> /dev/null;then
    echo "Missing dependency: $pkg"
    all_ok=1
  fi
done
if [ $all_ok -ne 0 ];then
  echo "Please install above dependencies before compiling SeekDB"
  exit 1
fi
echo "All build dependencies are ready."
exit 0
