#!/bin/bash
# Build verification script for Codespace. Usage: bash sync.sh
cd /workspaces/seekdb
{
  echo "=== sync start $(date) ==="
  git branch --show-current
  git remote -v | head -2
  df -h / | tail -1
  FREE_PCT=$(df --output=pcent / | tail -1 | tr -dc '0-9')
  if [ "$FREE_PCT" -ge 85 ]; then
    rm -rf /workspaces/seekdb-fix ~/.cache/bazel/disk-cache
    echo "disk cleanup done"
  fi
  pkill -x seekdb 2>/dev/null && echo "killed seekdb"
  git stash -u >/dev/null 2>&1 || true
  git fetch origin 2026-eval
  git checkout -B 2026-eval origin/2026-eval
  git log --oneline -4
  df -h / | tail -1
  echo "=== init ==="
} > .out.log 2>&1
./build.py init >> .out.log 2>&1
echo "INIT_EXIT=$?" >> .out.log
./build.py build -j 8 seekdb > .build.full.log 2>&1
echo "BUILD_EXIT=$?" >> .out.log
grep -c "error:" .build.full.log >> .out.log 2>&1
echo "--- error summary ---" >> .out.log
grep -m 60 -A6 "error:" .build.full.log >> .out.log
echo "--- tail ---" >> .out.log
tail -n 60 .build.full.log >> .out.log
df -h / | tail -1 >> .out.log
git add .out.log .build.full.log
git -c user.name=TR -c user.email=tr@tr.tr commit -m "ci: build log" -- .out.log .build.full.log
git push origin 2026-eval
echo done
