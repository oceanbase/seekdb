#!/usr/bin/env bash
# Persist SeekDB CI caches on NFS while keeping all build-time reads/writes local.
# V1 uses adjacent SHA-256 metadata to detect corruption, not to authenticate
# writers. The shared NFS mount and jobs that can write it are a trust boundary.

set -euo pipefail

log()
{
  echo "[seekdb-cache] $*"
}

warn()
{
  echo "[seekdb-cache] WARNING: $*" >&2
}

die()
{
  warn "$*"
  return 1
}

require_command()
{
  command -v "$1" >/dev/null 2>&1 || die "required command not found: $1"
}

WORKSPACE="${GITHUB_WORKSPACE:-}"
NFS_ROOT="${SEEKDB_CACHE_NFS_ROOT:-}"
LOCAL_ROOT="${SEEKDB_CACHE_LOCAL_ROOT:-${RUNNER_TEMP:-/tmp}/seekdb-ci-cache}"
RUST_ROOT="${SEEKDB_RUST_CACHE_ROOT:-${RUNNER_TEMP:-/tmp}/seekdb-rust}"
DEP_CACHE_DIR="${DEP_CACHE_DIR:-$LOCAL_ROOT/deps}"
CCACHE_DIR="${CCACHE_DIR:-$LOCAL_ROOT/ccache}"
CARGO_HOME="${CARGO_HOME:-$RUST_ROOT/cargo}"
RUSTUP_HOME="${RUSTUP_HOME:-$RUST_ROOT/rustup}"
PR_NUMBER="${SEEKDB_PR_NUMBER:-}"
RUN_ID="${GITHUB_RUN_ID:-0}"
RUN_ATTEMPT="${GITHUB_RUN_ATTEMPT:-1}"
SOURCE_SHA="${SEEKDB_SOURCE_SHA:-${GITHUB_SHA:-unknown}}"
PLATFORM="${SEEKDB_CACHE_PLATFORM:-el9.x86_64}"
BUILD_MODE="${PACKAGE_TYPE:-release}"
PR_MAX_BYTES="${SEEKDB_PR_CACHE_MAX_BYTES:-3221225472}"
PR_GLOBAL_MAX_BYTES="${SEEKDB_PR_CACHE_GLOBAL_MAX_BYTES:-32212254720}"
PR_TTL_DAYS="${SEEKDB_PR_CACHE_TTL_DAYS:-7}"
BASELINE_FILE="$LOCAL_ROOT/ccache-baseline.files"
DOWNLOAD_DIR="$LOCAL_ROOT/downloads"

validate_environment()
{
  [[ -n "$WORKSPACE" && -d "$WORKSPACE" ]] || die "GITHUB_WORKSPACE is not a directory: $WORKSPACE"
  [[ -n "$NFS_ROOT" && "$NFS_ROOT" == /* && "$NFS_ROOT" != "/" ]] \
    || die "SEEKDB_CACHE_NFS_ROOT must be an absolute non-root path"
  [[ "$NFS_ROOT" != *"/../"* && "$NFS_ROOT" != */.. ]] \
    || die "SEEKDB_CACHE_NFS_ROOT must not contain parent traversal"
  [[ "$PLATFORM" =~ ^[A-Za-z0-9._-]+$ ]] || die "invalid cache platform: $PLATFORM"
  [[ "$RUN_ID" =~ ^[0-9]+$ ]] || die "invalid GITHUB_RUN_ID: $RUN_ID"
  [[ "$RUN_ATTEMPT" =~ ^[0-9]+$ ]] || die "invalid GITHUB_RUN_ATTEMPT: $RUN_ATTEMPT"
  [[ "$SOURCE_SHA" == "unknown" || "$SOURCE_SHA" =~ ^[0-9a-f]{40,64}$ ]] \
    || die "invalid source SHA: $SOURCE_SHA"
  [[ -z "$PR_NUMBER" || "$PR_NUMBER" =~ ^[0-9]+$ ]] || die "invalid PR number: $PR_NUMBER"
  [[ "$PR_MAX_BYTES" =~ ^[0-9]+$ ]] || die "invalid PR cache byte limit: $PR_MAX_BYTES"
  [[ "$PR_GLOBAL_MAX_BYTES" =~ ^[0-9]+$ ]] || die "invalid global PR cache byte limit: $PR_GLOBAL_MAX_BYTES"
  [[ "$PR_TTL_DAYS" =~ ^[0-9]+$ ]] || die "invalid PR cache TTL: $PR_TTL_DAYS"
  require_command sha256sum
  require_command tar
  require_command zstd
  mkdir -p "$LOCAL_ROOT" "$DOWNLOAD_DIR"
}

hash_inputs()
{
  local file rel digest
  for file in "$@"; do
    [[ -f "$file" ]] || die "cache key input not found: $file" || return 1
  done
  {
    for file in "$@"; do
      rel="${file#"$WORKSPACE"/}"
      digest="$(sha256sum "$file" | awk '{print $1}')"
      printf '%s=%s\n' "$rel" "$digest"
    done
  } | sha256sum | awk '{print $1}'
}

deps_digest()
{
  hash_inputs \
    "$WORKSPACE/deps/init/oceanbase.el9.x86_64.deps" \
    "$WORKSPACE/deps/init/dep_create.sh"
}

cargo_digest()
{
  local manifest
  local -a inputs=(
    "$WORKSPACE/rust/rust-toolchain.toml"
    "$WORKSPACE/.github/actions/setup-rust/action.yml"
  )
  while IFS= read -r manifest; do
    inputs+=("$manifest")
  done < <(find "$WORKSPACE/rust" -name Cargo.toml -type f | LC_ALL=C sort)
  # Cargo.lock is currently generated rather than tracked. Only include it in
  # the key if a future change commits it; otherwise restore and publish would
  # calculate different keys in the same job.
  if git -C "$WORKSPACE" ls-files --error-unmatch rust/Cargo.lock >/dev/null 2>&1; then
    inputs+=("$WORKSPACE/rust/Cargo.lock")
  fi
  hash_inputs "${inputs[@]}"
}

cache_key()
{
  local kind="$1" digest safe_mode
  safe_mode="${BUILD_MODE//[^A-Za-z0-9._-]/_}"
  case "$kind" in
    deps)
      digest="$(deps_digest)" || return 1
      printf 'deps-v1-%s-%s\n' "$PLATFORM" "$digest"
      ;;
    rustup)
      digest="$(hash_inputs \
        "$WORKSPACE/rust/rust-toolchain.toml" \
        "$WORKSPACE/.github/actions/setup-rust/action.yml")" || return 1
      printf 'rustup-v1-linux-x86_64-%s\n' "$digest"
      ;;
    cargo)
      digest="$(cargo_digest)" || return 1
      printf 'cargo-v1-rsproxy-%s\n' "$digest"
      ;;
    ccache)
      digest="$(deps_digest)" || return 1
      printf 'ccache-v1-%s-relwithdebinfo-unity-%s-%s\n' \
        "$PLATFORM" "$safe_mode" "$digest"
      ;;
    *)
      die "unknown cache kind: $kind"
      return 1
      ;;
  esac
}

cache_destination()
{
  case "$1" in
    deps) printf '%s\n' "$DEP_CACHE_DIR" ;;
    rustup) printf '%s\n' "$RUST_ROOT" ;;
    cargo) printf '%s\n' "$CARGO_HOME" ;;
    ccache) printf '%s\n' "$CCACHE_DIR" ;;
    *) return 1 ;;
  esac
}

master_snapshot_parent()
{
  printf '%s/master/%s/%s/snapshots\n' "$NFS_ROOT" "$1" "$2"
}

pr_snapshot_parent()
{
  [[ -n "$PR_NUMBER" ]] || return 1
  printf '%s/pr/pr-%s/ccache/%s/snapshots\n' "$NFS_ROOT" "$PR_NUMBER" "$1"
}

manifest_value()
{
  local manifest="$1" field="$2" count
  count="$(grep -c "^${field}=" "$manifest" 2>/dev/null || true)"
  [[ "$count" == "1" ]] || return 1
  sed -n "s/^${field}=//p" "$manifest"
}

archive_paths_are_safe()
{
  local archive="$1"
  zstd -d -q -c "$archive" | tar -tf - | awk '
    /^\// { exit 1 }
    {
      count = split($0, parts, "/")
      for (i = 1; i <= count; i++) {
        if (parts[i] == "..") { exit 1 }
      }
    }
    END { if (NR == 0) exit 1 }
  '
}

snapshot_directories()
{
  local parent="$1" order="$2" path
  [[ -d "$parent" ]] || return 0
  shopt -s nullglob
  for path in "$parent"/*; do
    [[ -d "$path" && ! -L "$path" ]] || continue
    printf '%s\n' "$path"
  done | if [[ "$order" == "newest" ]]; then sort -V -r; else sort -V; fi
  shopt -u nullglob
}

restore_snapshot()
{
  local snapshot="$1" expected_kind="$2" expected_scope="$3" expected_key="$4"
  local expected_pr="$5" destination="$6"
  local manifest="$snapshot/manifest" archive_name archive remote_sha actual_sha
  local kind scope key base_cache_key manifest_pr run_id run_attempt version size_bytes entry_count copy started
  started=$SECONDS

  [[ -f "$manifest" && ! -L "$manifest" ]] || return 1
  archive_name="$(manifest_value "$manifest" archive)" || return 1
  version="$(manifest_value "$manifest" version)" || return 1
  kind="$(manifest_value "$manifest" kind)" || return 1
  scope="$(manifest_value "$manifest" scope)" || return 1
  key="$(manifest_value "$manifest" key)" || return 1
  base_cache_key="$(manifest_value "$manifest" base_cache_key)" || return 1
  manifest_pr="$(manifest_value "$manifest" pr_number)" || return 1
  run_id="$(manifest_value "$manifest" run_id)" || return 1
  run_attempt="$(manifest_value "$manifest" run_attempt)" || return 1
  size_bytes="$(manifest_value "$manifest" size_bytes)" || return 1
  entry_count="$(manifest_value "$manifest" entry_count)" || return 1
  remote_sha="$(manifest_value "$manifest" sha256)" || return 1

  [[ "$version" == "1" ]] || return 1
  [[ "$archive_name" == "cache.tar.zst" ]] || return 1
  [[ "$kind" == "$expected_kind" && "$scope" == "$expected_scope" ]] || return 1
  [[ "$key" == "$expected_key" && "$base_cache_key" == "$expected_key" ]] || return 1
  [[ "$manifest_pr" == "$expected_pr" ]] || return 1
  [[ "$run_id" =~ ^[0-9]+$ && "$run_attempt" =~ ^[0-9]+$ ]] || return 1
  [[ "$size_bytes" =~ ^[0-9]+$ && "$entry_count" =~ ^[0-9]+$ ]] || return 1
  [[ "$remote_sha" =~ ^[0-9a-f]{64}$ ]] || return 1
  [[ "$(basename "$snapshot")" == "${run_id}-${run_attempt}" ]] || return 1

  archive="$snapshot/$archive_name"
  [[ -f "$archive" && ! -L "$archive" ]] || return 1
  copy="$DOWNLOAD_DIR/${expected_kind}-${expected_scope}-${run_id}-${RANDOM}.tar.zst"
  if ! cp -f -- "$archive" "$copy"; then
    warn "failed to copy cache snapshot: $snapshot"
    rm -f -- "$copy"
    return 1
  fi
  if [[ "$(stat -c%s "$copy")" != "$size_bytes" ]]; then
    warn "size mismatch, skipping snapshot: $snapshot"
    rm -f -- "$copy"
    return 1
  fi
  actual_sha="$(sha256sum "$copy" | awk '{print $1}')"
  if [[ "$actual_sha" != "$remote_sha" ]]; then
    warn "checksum mismatch, skipping snapshot: $snapshot"
    rm -f -- "$copy"
    return 1
  fi
  if ! archive_paths_are_safe "$copy"; then
    warn "unsafe or unreadable archive, skipping snapshot: $snapshot"
    rm -f -- "$copy"
    return 1
  fi

  mkdir -p "$destination"
  if ! zstd -d -q -c "$copy" | tar --no-same-owner --no-same-permissions -xf - -C "$destination"; then
    warn "failed to extract cache snapshot: $snapshot"
    rm -f -- "$copy"
    return 1
  fi
  rm -f -- "$copy"
  log "restored $expected_kind/$expected_scope from $(basename "$snapshot") in $((SECONDS - started))s"
  return 0
}

restore_latest_master()
{
  local kind="$1" key="$2" destination="$3" parent snapshot
  parent="$(master_snapshot_parent "$kind" "$key")"
  while IFS= read -r snapshot; do
    [[ -n "$snapshot" ]] || continue
    if restore_snapshot "$snapshot" "$kind" master "$key" "" "$destination"; then
      printf 'hit\n' > "$LOCAL_ROOT/restore-master-${kind}.status"
      return 0
    fi
  done < <(snapshot_directories "$parent" newest)
  printf 'miss\n' > "$LOCAL_ROOT/restore-master-${kind}.status"
  log "no valid master $kind cache found for $key"
  return 1
}

restore_pr_ccache_deltas()
{
  local key="$1" destination="$2" parent snapshot restored=0
  [[ -n "$PR_NUMBER" ]] || return 0
  parent="$(pr_snapshot_parent "$key")"
  while IFS= read -r snapshot; do
    [[ -n "$snapshot" ]] || continue
    if restore_snapshot "$snapshot" ccache pr "$key" "$PR_NUMBER" "$destination"; then
      restored=$((restored + 1))
    fi
  done < <(snapshot_directories "$parent" oldest)
  log "restored $restored PR ccache delta(s) for PR $PR_NUMBER"
}

restore_cache()
{
  local kind="$1" key destination started
  key="$(cache_key "$kind")" || return 1
  destination="$(cache_destination "$kind")" || return 1
  started=$SECONDS
  mkdir -p "$destination"
  restore_latest_master "$kind" "$key" "$destination" || true
  if [[ "$kind" == "ccache" ]]; then
    restore_pr_ccache_deltas "$key" "$destination"
  fi
  log "restore $kind finished in $((SECONDS - started))s (key=$key)"
}

ccache_file_list()
{
  local output="$1"
  mkdir -p "$CCACHE_DIR"
  find -P "$CCACHE_DIR" -type f -printf '%P\n' \
    | awk '
        $0 == "CACHEDIR.TAG" { next }
        $0 == "stats" || $0 ~ /\/stats$/ { next }
        $0 ~ /(^|\/)tmp\// { next }
        $0 ~ /(^|\/)[^/]*\.lock$/ { next }
        $0 ~ /(^|\/)\.nfs/ { next }
        /(^|\/)\.\.($|\/)/ || /^\// { next }
        { print }
      ' \
    | LC_ALL=C sort -u > "$output"
}

baseline_ccache()
{
  ccache_file_list "$BASELINE_FILE"
  log "recorded ccache baseline with $(wc -l < "$BASELINE_FILE") file(s)"
}

archive_file_list()
{
  local source_dir="$1" list_file="$2" output="$3"
  [[ -s "$list_file" ]] || return 1
  tar -C "$source_dir" --verbatim-files-from --no-recursion -cf - -T "$list_file" \
    | zstd -q -T0 -1 -f -o "$output"
}

create_archive()
{
  local kind="$1" output="$2" list_file="$3" path
  local -a cargo_paths=()
  ARCHIVE_ENTRY_COUNT=0
  case "$kind" in
    deps)
      find "$DEP_CACHE_DIR" -mindepth 3 -maxdepth 3 -path '*/3rd/DONE' -type f -print -quit \
        | grep -q . || return 1
      tar -C "$DEP_CACHE_DIR" -cf - . | zstd -q -T0 -1 -f -o "$output"
      ARCHIVE_ENTRY_COUNT="$(find -P "$DEP_CACHE_DIR" -type f | wc -l)"
      ;;
    rustup)
      [[ -x "$CARGO_HOME/bin/rustup" && -d "$RUSTUP_HOME/toolchains" ]] || return 1
      tar -C "$RUST_ROOT" \
        --exclude='rustup/downloads' \
        --exclude='rustup/tmp' \
        --exclude='cargo/config.toml' \
        --exclude='cargo/registry' \
        --exclude='cargo/git' \
        -cf - rustup cargo/bin | zstd -q -T0 -1 -f -o "$output"
      ARCHIVE_ENTRY_COUNT="$(find -P "$RUSTUP_HOME" "$CARGO_HOME/bin" -type f 2>/dev/null | wc -l)"
      ;;
    cargo)
      for path in registry/index registry/cache registry/src git/db git/checkouts; do
        [[ -d "$CARGO_HOME/$path" ]] && cargo_paths+=("$path")
      done
      (( ${#cargo_paths[@]} > 0 )) || return 1
      tar -C "$CARGO_HOME" -cf - "${cargo_paths[@]}" \
        | zstd -q -T0 -1 -f -o "$output"
      ARCHIVE_ENTRY_COUNT="$(find -P "${cargo_paths[@]/#/$CARGO_HOME/}" -type f 2>/dev/null | wc -l)"
      ;;
    ccache)
      if [[ -z "$list_file" ]]; then
        list_file="$LOCAL_ROOT/ccache-publish.files"
        ccache_file_list "$list_file"
      fi
      ARCHIVE_ENTRY_COUNT="$(wc -l < "$list_file")"
      (( ARCHIVE_ENTRY_COUNT > 0 )) || return 1
      archive_file_list "$CCACHE_DIR" "$list_file" "$output"
      ;;
    *)
      return 1
      ;;
  esac
}

write_manifest()
{
  local output="$1" kind="$2" scope="$3" key="$4" pr_number="$5"
  local sha="$6" size_bytes="$7" entry_count="$8"
  {
    echo "version=1"
    echo "kind=$kind"
    echo "scope=$scope"
    echo "key=$key"
    echo "base_cache_key=$key"
    echo "archive=cache.tar.zst"
    echo "sha256=$sha"
    echo "size_bytes=$size_bytes"
    echo "entry_count=$entry_count"
    echo "run_id=$RUN_ID"
    echo "run_attempt=$RUN_ATTEMPT"
    echo "source_sha=$SOURCE_SHA"
    echo "pr_number=$pr_number"
    echo "created_at=$(date -u +%Y-%m-%dT%H:%M:%SZ)"
  } > "$output"
}

safe_remove_tree()
{
  local path="$1"
  [[ -n "$path" && "$path" == "$NFS_ROOT/"* && "$path" != "$NFS_ROOT" && -d "$path" ]] \
    || return 1
  rm -rf -- "$path"
}

directory_size()
{
  du -sb "$1" 2>/dev/null | awk '{print $1}'
}

prune_snapshots()
{
  local parent="$1" keep="$2" max_bytes="${3:-0}"
  local -a snapshots=()
  local snapshot total=0
  mapfile -t snapshots < <(snapshot_directories "$parent" oldest)
  for snapshot in "${snapshots[@]}"; do
    total=$((total + $(directory_size "$snapshot")))
  done
  while (( ${#snapshots[@]} > keep || (max_bytes > 0 && total > max_bytes) )); do
    snapshot="${snapshots[0]}"
    total=$((total - $(directory_size "$snapshot")))
    log "pruning cache snapshot: $snapshot"
    safe_remove_tree "$snapshot"
    snapshots=("${snapshots[@]:1}")
  done
}

publish_archive()
{
  local kind="$1" scope="$2" key="$3" pr_number="$4" source_archive="$5"
  local entry_count="$6" parent manifest_file nfs_stage final_dir sha size_bytes final_name
  if [[ "$scope" == "master" ]]; then
    parent="$(master_snapshot_parent "$kind" "$key")"
  else
    parent="$(pr_snapshot_parent "$key")"
  fi
  final_name="${RUN_ID}-${RUN_ATTEMPT}"
  final_dir="$parent/$final_name"
  mkdir -p "$parent"
  if [[ -d "$final_dir" ]]; then
    log "snapshot already exists, skipping publish: $final_dir"
    return 0
  fi

  sha="$(sha256sum "$source_archive" | awk '{print $1}')"
  size_bytes="$(stat -c%s "$source_archive")"
  manifest_file="$(mktemp "$LOCAL_ROOT/manifest-${kind}.XXXXXX")"
  write_manifest "$manifest_file" "$kind" "$scope" "$key" "$pr_number" \
    "$sha" "$size_bytes" "$entry_count"

  nfs_stage="$parent/.tmp-${final_name}-${RANDOM}"
  mkdir "$nfs_stage"
  if ! cp -f -- "$source_archive" "$nfs_stage/cache.tar.zst" \
      || ! cp -f -- "$manifest_file" "$nfs_stage/manifest"; then
    safe_remove_tree "$nfs_stage" || true
    rm -f -- "$manifest_file"
    return 1
  fi
  chmod 0644 "$nfs_stage/cache.tar.zst" "$nfs_stage/manifest"
  if ! mv -T -- "$nfs_stage" "$final_dir"; then
    safe_remove_tree "$nfs_stage" || true
    rm -f -- "$manifest_file"
    return 1
  fi
  rm -f -- "$manifest_file"
  [[ "$scope" == "pr" ]] && touch "$(dirname "$(dirname "$(dirname "$parent")")")"
  log "published $scope $kind cache: $final_dir ($size_bytes bytes, $entry_count files)"
}

publish_master()
{
  local kind="$1" key archive status_file started keep=2
  key="$(cache_key "$kind")" || return 1
  status_file="$LOCAL_ROOT/restore-master-${kind}.status"
  if [[ "$kind" == "deps" || "$kind" == "rustup" ]]; then
    keep=1
    if [[ -f "$status_file" && "$(<"$status_file")" == "hit" ]]; then
      log "master $kind cache already restored; immutable snapshot publish skipped"
      return 0
    fi
  fi

  archive="$(mktemp "$LOCAL_ROOT/${kind}.XXXXXX.tar.zst")"
  started=$SECONDS
  if ! create_archive "$kind" "$archive" ""; then
    warn "no publishable $kind cache content found"
    rm -f -- "$archive"
    return 0
  fi
  publish_archive "$kind" master "$key" "" "$archive" "$ARCHIVE_ENTRY_COUNT"
  rm -f -- "$archive"
  prune_snapshots "$(master_snapshot_parent "$kind" "$key")" "$keep"
  log "publish master $kind finished in $((SECONDS - started))s"
}

publish_pr_ccache()
{
  local key current_list delta_list archive started
  [[ -n "$PR_NUMBER" ]] || {
    log "not a pull request; PR ccache publish skipped"
    return 0
  }
  [[ -f "$BASELINE_FILE" ]] || {
    warn "ccache baseline missing; PR delta publish skipped"
    return 0
  }
  key="$(cache_key ccache)" || return 1
  current_list="$LOCAL_ROOT/ccache-current.files"
  delta_list="$LOCAL_ROOT/ccache-delta.files"
  ccache_file_list "$current_list"
  comm -13 "$BASELINE_FILE" "$current_list" > "$delta_list"
  if [[ ! -s "$delta_list" ]]; then
    log "no new ccache files for PR $PR_NUMBER; delta publish skipped"
    return 0
  fi

  archive="$(mktemp "$LOCAL_ROOT/ccache-pr.XXXXXX.tar.zst")"
  started=$SECONDS
  if ! create_archive ccache "$archive" "$delta_list"; then
    warn "failed to create PR ccache delta"
    rm -f -- "$archive"
    return 1
  fi
  publish_archive ccache pr "$key" "$PR_NUMBER" "$archive" "$ARCHIVE_ENTRY_COUNT"
  rm -f -- "$archive"
  prune_snapshots "$(pr_snapshot_parent "$key")" 8 "$PR_MAX_BYTES"
  log "publish PR ccache delta finished in $((SECONDS - started))s"
}

prune_master_keys()
{
  local kind="$1" keep="$2" path
  local root="$NFS_ROOT/master/$kind"
  local -a keys=()
  [[ -d "$root" ]] || return 0
  mapfile -t keys < <(
    find "$root" -mindepth 1 -maxdepth 1 -type d -printf '%T@ %p\n' \
      | sort -n | cut -d' ' -f2-
  )
  while (( ${#keys[@]} > keep )); do
    path="${keys[0]}"
    log "pruning old master $kind key: $path"
    safe_remove_tree "$path"
    keys=("${keys[@]:1}")
  done
}

gc_pr_caches()
{
  local root="$NFS_ROOT/pr" path total=0 size
  local -a namespaces=()
  [[ -d "$root" ]] || return 0

  while IFS= read -r -d '' path; do
    log "pruning expired PR cache: $path"
    safe_remove_tree "$path"
  done < <(find "$root" -mindepth 1 -maxdepth 1 -type d -name 'pr-[0-9]*' \
    -mmin "+$((PR_TTL_DAYS * 1440))" -print0)

  mapfile -t namespaces < <(
    find "$root" -mindepth 1 -maxdepth 1 -type d -name 'pr-[0-9]*' -printf '%T@ %p\n' \
      | sort -n | cut -d' ' -f2-
  )
  for path in "${namespaces[@]}"; do
    total=$((total + $(directory_size "$path")))
  done
  while (( total > PR_GLOBAL_MAX_BYTES && ${#namespaces[@]} > 0 )); do
    path="${namespaces[0]}"
    size="$(directory_size "$path")"
    log "pruning PR cache for global limit: $path"
    safe_remove_tree "$path"
    total=$((total - size))
    namespaces=("${namespaces[@]:1}")
  done
  log "PR cache GC complete: $total bytes retained"
}

garbage_collect()
{
  prune_master_keys deps 2
  prune_master_keys rustup 2
  prune_master_keys cargo 2
  prune_master_keys ccache 2
  gc_pr_caches
}

usage()
{
  cat <<'EOF'
Usage:
  cache.sh restore <deps|rustup|cargo|ccache>
  cache.sh baseline-ccache
  cache.sh publish-master <deps|rustup|cargo|ccache>
  cache.sh publish-pr-ccache
  cache.sh gc
  cache.sh key <deps|rustup|cargo|ccache>
EOF
}

main()
{
  local command="${1:-}" kind="${2:-}"
  validate_environment
  case "$command" in
    restore)
      restore_cache "$kind"
      ;;
    baseline-ccache)
      baseline_ccache
      ;;
    publish-master)
      publish_master "$kind"
      ;;
    publish-pr-ccache)
      publish_pr_ccache
      ;;
    gc)
      garbage_collect
      ;;
    key)
      cache_key "$kind"
      ;;
    *)
      usage >&2
      return 2
      ;;
  esac
}

main "$@"
