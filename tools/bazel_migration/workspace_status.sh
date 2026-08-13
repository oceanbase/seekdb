#!/usr/bin/env bash
set -euo pipefail

workspace="${BUILD_WORKSPACE_DIRECTORY:-.}"

if ! git_revision="$(git -C "${workspace}" rev-parse HEAD 2>/dev/null)"; then
  git_revision="unknown"
fi

if ! git_branch="$(git -C "${workspace}" rev-parse --abbrev-ref HEAD 2>/dev/null)"; then
  git_branch="unknown"
fi

printf 'STABLE_SEEKDB_GIT_REVISION %s\n' "${git_revision}"
printf 'STABLE_SEEKDB_GIT_BRANCH %s\n' "${git_branch}"
