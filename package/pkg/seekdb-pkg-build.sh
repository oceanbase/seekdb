#!/usr/bin/env bash
# Assemble a macOS .pkg installer from pre-compiled build artifacts.
# Completely independent of cmake install / cpack — mirrors the APK workflow.
#
# Prerequisites:
#   - A completed build: ./build.sh pkg debug --make -j24
#   - Xcode Command Line Tools (swiftc, pkgbuild, productbuild)
#
# Usage:
#   ./package/pkg/seekdb-pkg-build.sh [options] PROJECT_NAME VERSION RELEASE
#
# Example:
#   ./package/pkg/seekdb-pkg-build.sh --pkg seekdb 1.3.0 1
#   # produces: package/pkg/seekdb-1.3.0-1-macos15-arm64.pkg
#
# Env:
#   SEEKDB_BUILD   Build directory (default: <repo>/build_pkg_debug)

set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "$0")" && pwd)"
TOPDIR="$(cd "$SCRIPT_DIR/../.." && pwd)"
MACPKG_DIR="$TOPDIR/tools/macpkg"

SEEKDB_BUILD="${SEEKDB_BUILD:-$TOPDIR/build_release}"
MACOS_ARCH="$(uname -m)"
MACOS_VERSION_MAJOR="$(sw_vers -productVersion | cut -d. -f1)"

DO_BUILD=false
DO_PKG=false
DO_MENUBAR=true

usage() {
  cat <<'EOF'
Usage: seekdb-pkg-build.sh [options] PROJECT_NAME VERSION RELEASE

  PROJECT_NAME    Package name (e.g. seekdb)
  VERSION         Version string (e.g. 1.3.0)
  RELEASE         Release number (e.g. 1)

Options:
  --build         Run make in SEEKDB_BUILD before packaging
  --pkg           Assemble the .pkg installer
  --no-menubar    Skip building the menu bar app
  -h, --help      Show this help

Environment:
  SEEKDB_BUILD    CMake build directory (default: <repo>/build_release)

Typical workflow:
  cd <oceanbase-lite>
  ./build.sh release --make -j24
  ./package/pkg/seekdb-pkg-build.sh --pkg seekdb 1.3.0 1
EOF
}

while [[ $# -gt 0 ]]; do
  case "$1" in
    --build)      DO_BUILD=true;   shift ;;
    --pkg)        DO_PKG=true;     shift ;;
    --no-menubar) DO_MENUBAR=false; shift ;;
    -h|--help)    usage; exit 0 ;;
    -*)           echo "Unknown option: $1" >&2; usage >&2; exit 1 ;;
    *)            break ;;
  esac
done

PROJECT_NAME="${1:-}"
VERSION="${2:-}"
RELEASE="${3:-}"

if [[ "$DO_PKG" == true ]]; then
  if [[ -z "$PROJECT_NAME" || -z "$VERSION" || -z "$RELEASE" ]]; then
    echo "Error: PROJECT_NAME VERSION RELEASE are required when using --pkg." >&2
    usage >&2
    exit 1
  fi
fi

info() { printf '[seekdb-pkg-build] %s\n' "$*"; }
die()  { printf '[seekdb-pkg-build][ERROR] %s\n' "$*" >&2; exit 1; }

# ---------------------------------------------------------------------------
# Step 1: Optional make
# ---------------------------------------------------------------------------
if [[ "$DO_BUILD" == true ]]; then
  info "Building in $SEEKDB_BUILD ..."
  make -C "$SEEKDB_BUILD" -j"$(sysctl -n hw.ncpu)"
fi

[[ "$DO_PKG" == true ]] || { info "No --pkg flag; done."; exit 0; }

# ---------------------------------------------------------------------------
# Validate build artifacts
# ---------------------------------------------------------------------------
SEEKDB_BIN="$SEEKDB_BUILD/src/observer/seekdb"
[[ -x "$SEEKDB_BIN" ]] || die "seekdb binary not found: $SEEKDB_BIN (run build first)"

# ---------------------------------------------------------------------------
# Step 2: Build menu bar app
# ---------------------------------------------------------------------------
MENUBAR_SRC="$MACPKG_DIR/seekdbctl/menubar"
MENUBAR_BIN="$SEEKDB_BUILD/SeekDBMenuBar"

if [[ "$DO_MENUBAR" == true && -f "$MENUBAR_SRC/SeekDBMenuBar.swift" ]]; then
  info "Compiling SeekDB menu bar app ..."
  swiftc \
    -o "$MENUBAR_BIN" \
    -framework AppKit \
    -target arm64-apple-macosx13.0 \
    -O \
    "$MENUBAR_SRC/SeekDBMenuBar.swift"
  codesign --force --sign - "$MENUBAR_BIN"
  info "Menu bar app compiled: $MENUBAR_BIN"
fi

# ---------------------------------------------------------------------------
# Step 3: Assemble staging directory (the on-disk layout after install)
# ---------------------------------------------------------------------------
PKG_NAME="${PROJECT_NAME}-${VERSION}-${RELEASE}-macos${MACOS_VERSION_MAJOR}-${MACOS_ARCH}"
STAGING="$SEEKDB_BUILD/_pkg_staging"
rm -rf "$STAGING"

info "Assembling staging directory ..."

# --- binaries ---
install -d "$STAGING/opt/homebrew/bin"
install -m 755 "$SEEKDB_BIN" "$STAGING/opt/homebrew/bin/seekdb"
for script in seekdbctl seekdb_start seekdb_stop seekdb_status seekdb_config \
              seekdb_setup seekdb_cleanup seekdb_paths seekdb_uninstall; do
  src="$MACPKG_DIR/seekdbctl/$script"
  [[ -f "$src" ]] && install -m 755 "$src" "$STAGING/opt/homebrew/bin/$script"
done

# --- ob_admin / ob_error (optional) ---
[[ -x "$SEEKDB_BUILD/tools/ob_admin/ob_admin" ]] && \
  install -m 755 "$SEEKDB_BUILD/tools/ob_admin/ob_admin" "$STAGING/opt/homebrew/bin/"
[[ -x "$SEEKDB_BUILD/tools/ob_error/src/ob_error" ]] && \
  install -m 755 "$SEEKDB_BUILD/tools/ob_error/src/ob_error" "$STAGING/opt/homebrew/bin/"

# --- LaunchDaemon plist ---
install -d "$STAGING/Library/LaunchDaemons"
install -m 644 "$MACPKG_DIR/launchd/profile/com.seekdb.server.plist.in" \
  "$STAGING/Library/LaunchDaemons/com.seekdb.server.plist"

# --- helper scripts ---
install -d "$STAGING/opt/homebrew/libexec/seekdb/scripts"
install -m 755 "$MACPKG_DIR/launchd/profile/seekdb_launchd_start" \
  "$STAGING/opt/homebrew/libexec/seekdb/scripts/"
install -m 755 "$MACPKG_DIR/launchd/profile/seekdb_launchd_stop" \
  "$STAGING/opt/homebrew/libexec/seekdb/scripts/"
for py in import_time_zone_info.py import_srs_data.py; do
  [[ -f "$TOPDIR/tools/$py" ]] && install -m 755 "$TOPDIR/tools/$py" \
    "$STAGING/opt/homebrew/libexec/seekdb/"
done

# --- config ---
install -d "$STAGING/opt/homebrew/etc/seekdb"
install -m 644 "$MACPKG_DIR/launchd/profile/seekdb.cnf" "$STAGING/opt/homebrew/etc/seekdb/"
for f in default_parameter.json default_system_variable.json; do
  src="$TOPDIR/src/share/parameter/$f"
  [[ -f "$src" ]] || src="$TOPDIR/src/share/system_variable/$f"
  [[ -f "$src" ]] && install -m 644 "$src" "$STAGING/opt/homebrew/etc/seekdb/"
done
# ob_system_variable_init.json (generated at build time)
[[ -f "$SEEKDB_BUILD/src/share/ob_system_variable_init.json" ]] && \
  install -m 644 "$SEEKDB_BUILD/src/share/ob_system_variable_init.json" "$STAGING/opt/homebrew/etc/seekdb/"
for f in oceanbase_upgrade_dep.yml deps_compat.yml; do
  [[ -f "$TOPDIR/tools/upgrade/$f" ]] && install -m 644 "$TOPDIR/tools/upgrade/$f" "$STAGING/opt/homebrew/etc/seekdb/"
done

# --- share: admin SQL ---
install -d "$STAGING/opt/homebrew/share/seekdb/admin"
SYS_PACK_DIR="$SEEKDB_BUILD/syspack_release"
[[ -d "$SYS_PACK_DIR" ]] && cp -R "$SYS_PACK_DIR/"* "$STAGING/opt/homebrew/share/seekdb/admin/" 2>/dev/null || true

# --- share: help ---
install -d "$STAGING/opt/homebrew/share/seekdb/help"
[[ -f "$TOPDIR/src/sql/fill_help_tables-ob.sql" ]] && \
  install -m 644 "$TOPDIR/src/sql/fill_help_tables-ob.sql" "$STAGING/opt/homebrew/share/seekdb/help/"

# --- share: timezone ---
install -d "$STAGING/opt/homebrew/share/seekdb/timezone"
for f in timezone_V1.log timezone.data timezone_name.data timezone_trans.data timezone_trans_type.data; do
  [[ -f "$TOPDIR/tools/$f" ]] && install -m 644 "$TOPDIR/tools/$f" "$STAGING/opt/homebrew/share/seekdb/timezone/"
done

# --- share: srs ---
install -d "$STAGING/opt/homebrew/share/seekdb/srs"
for f in spatial_reference_systems.data default_srs_data_mysql.sql; do
  [[ -f "$TOPDIR/tools/$f" ]] && install -m 644 "$TOPDIR/tools/$f" "$STAGING/opt/homebrew/share/seekdb/srs/"
done

# --- share: upgrade ---
install -d "$STAGING/opt/homebrew/share/seekdb/upgrade"
for f in upgrade_pre.py upgrade_post.py upgrade_checker.py upgrade_health_checker.py; do
  [[ -f "$TOPDIR/tools/upgrade/$f" ]] && install -m 644 "$TOPDIR/tools/upgrade/$f" "$STAGING/opt/homebrew/share/seekdb/upgrade/"
done

# --- SeekDB Monitor .app bundle ---
if [[ "$DO_MENUBAR" == true && -x "$MENUBAR_BIN" ]]; then
  APP_DIR="$STAGING/Applications/SeekDB Monitor.app/Contents"
  install -d "$APP_DIR/MacOS"
  install -m 755 "$MENUBAR_BIN" "$APP_DIR/MacOS/SeekDBMenuBar"
  # Generate Info.plist from template
  sed "s/@OceanBase_VERSION@/${VERSION}/g" "$MENUBAR_SRC/info.plist.in" > "$APP_DIR/Info.plist"
  codesign --force --sign - "$APP_DIR/MacOS/SeekDBMenuBar"
  codesign --force --sign - "$STAGING/Applications/SeekDB Monitor.app"
  info "Menu bar app bundled"
fi

info "Staging complete: $(find "$STAGING" -type f | wc -l | tr -d ' ') files"

# ---------------------------------------------------------------------------
# Step 4: Build .pkg with pkgbuild + productbuild
# ---------------------------------------------------------------------------
COMPONENT_PKG="$SEEKDB_BUILD/_pkg_component.pkg"
OUTPUT_PKG="$SCRIPT_DIR/${PKG_NAME}.pkg"

info "Building component package ..."
pkgbuild \
  --root "$STAGING" \
  --identifier "com.seekdb.server" \
  --version "$VERSION" \
  --scripts "$MACPKG_DIR/launchd/profile" \
  "$COMPONENT_PKG"

info "Building product archive ..."
# Create a minimal distribution.xml
DIST_XML="$SEEKDB_BUILD/_pkg_distribution.xml"
cat > "$DIST_XML" <<DISTEOF
<?xml version="1.0" encoding="utf-8"?>
<installer-gui-script minSpecVersion="2">
    <title>SeekDB ${VERSION}</title>
    <options customize="never" require-scripts="false"/>
    <domains enable_anywhere="false" enable_currentUserHome="false" enable_localSystem="true"/>
    <pkg-ref id="com.seekdb.server"/>
    <choices-outline>
        <line choice="default">
            <line choice="com.seekdb.server"/>
        </line>
    </choices-outline>
    <choice id="default"/>
    <choice id="com.seekdb.server" visible="false">
        <pkg-ref id="com.seekdb.server"/>
    </choice>
    <pkg-ref id="com.seekdb.server" version="${VERSION}" onConclusion="none">component.pkg</pkg-ref>
</installer-gui-script>
DISTEOF

productbuild \
  --distribution "$DIST_XML" \
  --package-path "$(dirname "$COMPONENT_PKG")" \
  "$OUTPUT_PKG"

info "Package created: $OUTPUT_PKG"
ls -lh "$OUTPUT_PKG"

# Cleanup temp files
rm -f "$COMPONENT_PKG" "$DIST_XML"
rm -rf "$STAGING"

info "Done."
