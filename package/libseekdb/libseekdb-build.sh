#!/usr/bin/env bash
# Build libseekdb, on macOS bundle deps to libs/, then pack lib + libs/ + seekdb.h into a .zip
# Zip is written to this script's directory (package/libseekdb/).
#
# Windows (seekdb.dll): build with .\build.ps1 release --ninja --target libseekdb -DBUILD_EMBED_MODE=ON,
# then run libseekdb-build.ps1 in this directory (see README.md).
#
# Usage:
#   cd package/libseekdb && ./libseekdb-build.sh
#   BUILD_TYPE=debug ./libseekdb-build.sh
#   ./libseekdb-build.sh /path/to/dir-with-libseekdb   # pack from existing dir (no build)
#
# Android (NDK, arm64-v8a only): zip is libseekdb-android-arm64-v8a.zip — not host uname (e.g. darwin-*).
#   ./libseekdb-build.sh --android                    # use build_android_<BUILD_TYPE>, build if needed
#   ./libseekdb-build.sh /path/to/build_android_*/src/include   # pack only

set -e

# --- Parse flags (no LIBSEEKDB_* env vars) ---
android_build=false
remaining=()
while [[ $# -gt 0 ]]; do
  case "$1" in
    --android) android_build=true; shift ;;
    *) remaining+=("$1"); shift ;;
  esac
done
set -- "${remaining[@]}"

# --- Paths and config ---
SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
TOP_DIR="$(cd "$SCRIPT_DIR/../.." && pwd)"
BUILD_TYPE="${BUILD_TYPE:-release}"
BUILD_DIR="$TOP_DIR/build_${BUILD_TYPE}"
WORK_DIR=""
ANDROID_PACK=false
UNAME_S="$(uname -s)"
UNAME_M="$(uname -m)"

# --- Helpers ---
die() { echo "error: $*" >&2; exit 1; }

# List dependency paths from a dylib (one per line, trimmed). Skips first line (the dylib itself).
get_dylib_deps() {
  local dylib="$1"
  otool -L "$dylib" | tail -n +2 | while IFS= read -r line; do
    dep="${line%% (*}"
    printf '%s\n' "$(printf '%s' "$dep" | sed 's/^[[:space:]]*//;s/[[:space:]]*$//')"
  done
}

# Change a dependency reference in dylib: from_ref -> to_ref (exact match).
fix_dylib_ref() {
  local dylib="$1" from_ref="$2" to_ref="$3"
  install_name_tool -change "$from_ref" "$to_ref" "$dylib"
}

# Remove all LC_RPATH entries from dylib that look like absolute paths.
strip_rpaths() {
  local dylib="$1"
  while IFS= read -r rpath; do
    [[ -n "$rpath" ]] || continue
    echo "  delete_rpath: $rpath"
    install_name_tool -delete_rpath "$rpath" "$dylib"
  done < <(otool -l "$dylib" | awk '/[[:space:]]path[[:space:]]+\// { print $2 }')
}

# --- 1) Resolve WORK_DIR ---
if [[ -n "${1:-}" ]]; then
  WORK_DIR="$(cd "$1" && pwd)"
  echo "[BUILD] Using directory: $WORK_DIR (pack from existing tree; no build)"
  if [[ "$android_build" == true ]]; then
    ANDROID_PACK=true
    if [[ "$WORK_DIR" == *"/src/include" ]]; then
      BUILD_DIR="$(cd "$WORK_DIR/../.." && pwd)"
    fi
  fi
elif [[ "$android_build" == true ]]; then
  ANDROID_PACK=true
  BUILD_DIR="$TOP_DIR/build_android_${BUILD_TYPE}"
  WORK_DIR="$BUILD_DIR/src/include"
  echo "[BUILD] Android: BUILD_DIR=$BUILD_DIR"
else
  WORK_DIR="$BUILD_DIR/src/include"
fi

if [[ -z "${1:-}" ]]; then
  # --- 2) Build libseekdb if not present (main lib is always next to libs/, not inside) ---
  if [[ ! -f "$WORK_DIR/libseekdb.dylib" && ! -f "$WORK_DIR/libseekdb.so" ]]; then
    echo "[BUILD] Building libseekdb (BUILD_TYPE=$BUILD_TYPE)..."
    if [[ "$ANDROID_PACK" == true ]]; then
      if [[ ! -d "$BUILD_DIR" ]]; then
        (cd "$TOP_DIR" && ./build.sh "$BUILD_TYPE" --android --init --make) || exit 1
      else
        (cd "$TOP_DIR" && ./build.sh "$BUILD_TYPE" --android --make) || exit 1
      fi
      _j=$(nproc 2>/dev/null || sysctl -n hw.ncpu 2>/dev/null || echo 4)
      (cd "$BUILD_DIR" && make libseekdb -j"${_j}") || exit 1
    else
      if [[ ! -d "$BUILD_DIR" ]]; then
        (cd "$TOP_DIR" && ./build.sh "$BUILD_TYPE" --init --make) || exit 1
      else
        (cd "$TOP_DIR" && ./build.sh "$BUILD_TYPE" --make) || exit 1
      fi
    fi
  fi

  # --- 3) macOS: bundle deps to libs/, fix @loader_path ---
  # Layout: libseekdb.dylib at root, deps in libs/. When main is loaded from root,
  # @loader_path = root, so main's deps must be @loader_path/libs/xxx. When a dep in libs/
  # is loaded, @loader_path = libs/, so dep refs must be @loader_path/xxx (not libs/xxx).
  if [[ "$UNAME_S" == "Darwin" && -f "$WORK_DIR/libseekdb.dylib" ]]; then
    # If the dylib was already bundled (deps point to @loader_path/libs/), dylibbundler
    # would prompt for paths when libs/ is empty. Rebuild to get a clean dylib with absolute deps.
    if otool -L "$WORK_DIR/libseekdb.dylib" | grep -q '@loader_path/libs/'; then
      echo "[BUILD] libseekdb.dylib was already bundled; rebuilding to get clean dylib for this run..."
      rm -f "$WORK_DIR/libseekdb.dylib"
      (cd "$TOP_DIR" && ./build.sh "$BUILD_TYPE" -DBUILD_EMBED_MODE=ON --make) || exit 1
    fi

    # Save pristine dylib so we can restore after zip (keeps build dir clean; next run won't rebuild)
    cp "$WORK_DIR/libseekdb.dylib" "$WORK_DIR/libseekdb.dylib.orig"
    SAVED_PRISTINE_DYLIB="$WORK_DIR/libseekdb.dylib.orig"

    echo "[BUILD] Bundling libseekdb.dylib for macOS..."
    cd "$WORK_DIR"
    DYLIB_NAME="libseekdb.dylib"

    strip_rpaths "$DYLIB_NAME"

    if ! command -v dylibbundler &>/dev/null; then
      die "dylibbundler not found. Install with: brew install dylibbundler"
    fi
    rm -rf libs && mkdir -p libs
    dylibbundler -x "$DYLIB_NAME" -cd -b -p '@loader_path/libs'

    install_name_tool -id "@loader_path/$DYLIB_NAME" "$DYLIB_NAME"
    while IFS= read -r dep; do
      [[ -n "$dep" ]] || continue
      if [[ "$dep" == @loader_path/* ]]; then
        depname="${dep#@loader_path/}"
        [[ -f "libs/$depname" ]] && fix_dylib_ref "$DYLIB_NAME" "$dep" "@loader_path/libs/$depname"
      fi
    done < <(get_dylib_deps "$DYLIB_NAME")

    for d in libs/*.dylib; do
      [[ -f "$d" ]] || continue
      name="$(basename "$d")"
      while IFS= read -r dep; do
        [[ -n "$dep" ]] || continue
        if [[ "$dep" == @loader_path/libs/* ]]; then
          fix_dylib_ref "$d" "$dep" "@loader_path/${dep#@loader_path/libs/}"
        elif [[ "$dep" == @rpath/libs/* ]]; then
          fix_dylib_ref "$d" "$dep" "@loader_path/${dep#@rpath/libs/}"
        fi
      done < <(get_dylib_deps "$d")
      install_name_tool -id "@loader_path/$name" "$d"
    done

    echo "[BUILD] Bundle done: $DYLIB_NAME at root, deps in $WORK_DIR/libs"

    # Sign dylibs. Ad-hoc when CODESIGN_IDENTITY unset;
    # when set, use Developer ID and optionally CODESIGN_ENTITLEMENTS + --options runtime.
    if command -v codesign &>/dev/null; then
      SIGN_ID="${CODESIGN_IDENTITY:--}"
      ENTITLEMENTS_ARG=()
      [[ -n "${CODESIGN_ENTITLEMENTS:-}" && -f "${CODESIGN_ENTITLEMENTS}" ]] && ENTITLEMENTS_ARG=(--entitlements "$CODESIGN_ENTITLEMENTS")
      RUNTIME_ARG=()
      [[ -n "${CODESIGN_IDENTITY:-}" && "$SIGN_ID" != "-" ]] && RUNTIME_ARG=(--options runtime)
      echo "[BUILD] Signing dylibs for macOS (identity: ${SIGN_ID})..."
      for d in libs/*.dylib; do
        [[ -f "$d" ]] || continue
        codesign "${RUNTIME_ARG[@]}" "${ENTITLEMENTS_ARG[@]}" -f --sign "$SIGN_ID" "$d" || die "codesign failed: $d"
      done
      codesign "${RUNTIME_ARG[@]}" "${ENTITLEMENTS_ARG[@]}" -f --sign "$SIGN_ID" "$DYLIB_NAME" || die "codesign failed: $DYLIB_NAME"
      echo "[BUILD] Signing done."
    else
      echo "[BUILD] WARNING: codesign not found; dylibs are unsigned (may fail to load on macOS)."
    fi
    cd - >/dev/null
  fi
fi

# --- 4) Resolve main library and deps dir (main and libs/ are siblings) ---
MAIN_LIB=""
DEPS_DIR="$WORK_DIR/libs"
if [[ -f "$WORK_DIR/libseekdb.dylib" ]]; then
  MAIN_LIB="$WORK_DIR/libseekdb.dylib"
elif [[ -f "$WORK_DIR/libseekdb.so" ]]; then
  MAIN_LIB="$WORK_DIR/libseekdb.so"
else
  die "libseekdb.dylib or libseekdb.so not found in $WORK_DIR"
fi

HEADER="$TOP_DIR/src/include/seekdb.h"
[[ -f "$HEADER" ]] || die "seekdb.h not found: $HEADER"

# --- 5) OS / Arch for zip name ---
# Android (arm64-v8a only): fixed zip name; not host uname. Detect ELF .so on Mac without --android.
ZIP_USE_ANDROID_PREFIX="$ANDROID_PACK"
if [[ "$ZIP_USE_ANDROID_PREFIX" != true && "$UNAME_S" == "Darwin" && -f "$WORK_DIR/libseekdb.so" && ! -f "$WORK_DIR/libseekdb.dylib" ]]; then
  if command -v file >/dev/null 2>&1; then
    _so_info="$(file -b "$WORK_DIR/libseekdb.so" 2>/dev/null || true)"
    if echo "$_so_info" | grep -q 'ELF.*shared object'; then
      ZIP_USE_ANDROID_PREFIX=true
      echo "[BUILD] libseekdb.so is ELF (NDK): zip libseekdb-android-arm64-v8a.zip (not darwin-*)"
    fi
  fi
fi

if [[ "$ZIP_USE_ANDROID_PREFIX" == true ]]; then
  ZIP_NAME="libseekdb-android-arm64-v8a.zip"
  echo "[BUILD] Android artifact zip: $ZIP_NAME"
else
  case "$UNAME_S" in
    Darwin) OS="darwin" ;;
    Linux)  OS="linux"  ;;
    *)      die "unsupported OS: $UNAME_S" ;;
  esac
  if [[ -n "${ARCH:-}" ]]; then
    echo "[BUILD] Using ARCH from environment: $ARCH"
  else
    case "$UNAME_M" in
      arm64|aarch64) ARCH="arm64"   ;;
      x86_64|amd64)  ARCH="x86_64" ;;
      *)             die "unsupported arch: $UNAME_M" ;;
    esac
  fi
  ARCH_SUFFIX="${ARCH}"
  [[ "$ARCH" == "x86_64" ]] && ARCH_SUFFIX="x64"
  ZIP_NAME="libseekdb-${OS}-${ARCH_SUFFIX}.zip"
fi
MAIN_LIB_NAME="$(basename "$MAIN_LIB")"

# --- 6) Assemble and zip ---
OUTPUT_ZIP="$SCRIPT_DIR/$ZIP_NAME"
PACK_DIR="$(mktemp -d)"
trap "rm -rf '$PACK_DIR'" EXIT

cp "$HEADER" "$PACK_DIR/seekdb.h"
cp "$MAIN_LIB" "$PACK_DIR/$MAIN_LIB_NAME"
if [[ -d "$DEPS_DIR" ]]; then
  mkdir -p "$PACK_DIR/libs"
  for f in "$DEPS_DIR"/*; do
    [[ -f "$f" ]] || continue
    cp "$f" "$PACK_DIR/libs/"
  done
fi

(cd "$PACK_DIR" && zip -r "$OUTPUT_ZIP" . -x "*.DS_Store")
echo "[BUILD] Created: $OUTPUT_ZIP"

# Restore build dir to pristine dylib on macOS so next run does not trigger rebuild
if [[ -n "${SAVED_PRISTINE_DYLIB:-}" && -f "${SAVED_PRISTINE_DYLIB}" ]]; then
  cp "$SAVED_PRISTINE_DYLIB" "$WORK_DIR/libseekdb.dylib"
  rm -rf "$WORK_DIR/libs"
  rm -f "$SAVED_PRISTINE_DYLIB"
  echo "[BUILD] Restored build dir to pristine dylib (cleanup for next run)."
fi
