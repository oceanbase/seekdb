#!/usr/bin/env bash
set -euo pipefail
workspace="${GITHUB_WORKSPACE:?}"
task_dir="${SEEKDB_TASK_DIR:?}"
cd "$workspace"
mkdir -p "$task_dir"
args=(--jobs="${BUILD_JOBS:-32}")
case "${PACKAGE_TYPE:-release}" in
  release) ;;
  sanity) args+=(--sanity) ;;
  *) echo "Unsupported package type: $PACKAGE_TYPE" >&2; exit 2 ;;
esac
if [[ -n "${BAZEL_DISK_CACHE:-}" ]]; then
  args+=(--disk_cache="$BAZEL_DISK_CACHE"
    --experimental_disk_cache_gc_max_size=500G --experimental_disk_cache_gc_idle_delay=0)
fi
if [[ -n "${FORWARDING_HOST:-}" ]]; then
  echo "$FORWARDING_HOST mirrors.oceanbase.com" >> /etc/hosts
fi
started=$SECONDS
# Trust the CI proxy's CA alongside the bundled public CAs.
bazel_java_home=$(python3 ./build.py info --batch java-home)
bazel_trust_store="$task_dir/bazel-cacerts"
install -m 644 "$bazel_java_home/lib/security/cacerts" "$bazel_trust_store"
"$bazel_java_home/bin/keytool" -importcert -noprompt -alias ci-root \
  -file /usr/local/share/ca-certificates/rootCA.crt \
  -keystore "$bazel_trust_store" -storepass changeit
args+=(--host_jvm_args="-Djavax.net.ssl.trustStore=$bazel_trust_store")
python3 ./build.py init 2>&1 | tee "$task_dir/compile_init.output"
# Cargo skips its cache locks on NFS. Fetch under a CI lock, then build without it.
flock --close "$CARGO_HOME/.seekdb-vendor.lock" \
  python3 ./build.py fetch "${args[@]}" //src/observer:seekdb 2>&1 | tee "$task_dir/compile_fetch.output"
python3 ./build.py build --nofetch "${args[@]}" //src/observer:seekdb 2>&1 | tee "$task_dir/compile.output"
echo "[compile.sh] total elapsed=$((SECONDS - started))s"
