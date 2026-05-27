#!/usr/bin/env bash
# Assemble a macOS .pkg installer from pre-compiled build artifacts.
# Completely independent of cmake install / cpack — mirrors the APK workflow.
#
# Prerequisites:
#   - A completed build: ./build.sh release --make -j24
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
#   SEEKDB_BUILD   Build directory (default: <repo>/build_release)

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

HELPER_BIN="$SEEKDB_BUILD/SeekDBHelper"

if [[ "$DO_MENUBAR" == true && -f "$MENUBAR_SRC/SeekDBMenuBar.swift" ]]; then
  info "Compiling SeekDB menu bar app ..."
  swiftc \
    -o "$MENUBAR_BIN" \
    -framework AppKit \
    -framework Security \
    -target arm64-apple-macosx13.0 \
    -O \
    "$MENUBAR_SRC/SeekDBMenuBar.swift"
  codesign --force --sign - "$MENUBAR_BIN"
  info "Menu bar app compiled: $MENUBAR_BIN"

  info "Compiling privileged helper ..."
  swiftc \
    -o "$HELPER_BIN" \
    -framework Foundation \
    -target arm64-apple-macosx13.0 \
    -O \
    "$MENUBAR_SRC/SeekDBHelper.swift"
  codesign --force --sign - "$HELPER_BIN"
  info "Helper compiled: $HELPER_BIN"
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
install -d "$STAGING/opt/homebrew/lib/seekdb"
install -m 755 "$SEEKDB_BIN" "$STAGING/opt/homebrew/bin/seekdb"

# --- bundle non-system dylibs (recursive) ---
info "Bundling dynamic libraries ..."
DYLIB_DIR="$STAGING/opt/homebrew/lib/seekdb"

collect_non_system_deps() {
  otool -L "$1" 2>/dev/null | awk '/^\t/ {print $1}' | while read -r dep; do
    case "$dep" in
      /usr/lib/*|/System/*|*:) ;;
      *) echo "$dep" ;;
    esac
  done
}

# seed with seekdb's direct deps
for dep in $(collect_non_system_deps "$SEEKDB_BIN"); do
  dep_name="$(basename "$dep")"
  if [[ -f "$dep" && ! -f "$DYLIB_DIR/$dep_name" ]]; then
    cp "$dep" "$DYLIB_DIR/$dep_name"
    chmod 644 "$DYLIB_DIR/$dep_name"
  fi
done

# recursively resolve transitive deps
changed=1
while [[ "$changed" -eq 1 ]]; do
  changed=0
  for lib in "$DYLIB_DIR"/*.dylib; do
    for dep in $(collect_non_system_deps "$lib"); do
      dep_name="$(basename "$dep")"
      if [[ -f "$dep" && ! -f "$DYLIB_DIR/$dep_name" ]]; then
        cp "$dep" "$DYLIB_DIR/$dep_name"
        chmod 644 "$DYLIB_DIR/$dep_name"
        changed=1
      fi
    done
  done
done

BUNDLED_COUNT=$(ls "$DYLIB_DIR"/*.dylib 2>/dev/null | wc -l | tr -d ' ')
info "  bundled $BUNDLED_COUNT dylibs"

# rewrite paths: seekdb binary
for dep in $(collect_non_system_deps "$STAGING/opt/homebrew/bin/seekdb"); do
  dep_name="$(basename "$dep")"
  install_name_tool -change "$dep" "@executable_path/../lib/seekdb/$dep_name" \
    "$STAGING/opt/homebrew/bin/seekdb" 2>/dev/null
done

# rewrite paths: each dylib's deps + id
for lib in "$DYLIB_DIR"/*.dylib; do
  lib_name="$(basename "$lib")"
  install_name_tool -id "@loader_path/$lib_name" "$lib" 2>/dev/null || true
  for dep in $(collect_non_system_deps "$lib"); do
    dep_name="$(basename "$dep")"
    install_name_tool -change "$dep" "@loader_path/$dep_name" "$lib" 2>/dev/null
  done
done

# re-sign everything
info "Re-signing binaries ..."
for lib in "$DYLIB_DIR"/*.dylib; do
  codesign --force --sign - "$lib" 2>/dev/null || true
done
codesign --force --sign - "$STAGING/opt/homebrew/bin/seekdb" 2>/dev/null || true
for script in seekdbctl seekdb_start seekdb_stop seekdb_status seekdb_config \
              seekdb_setup seekdb_cleanup seekdb_paths seekdb_uninstall; do
  src="$MACPKG_DIR/seekdbctl/$script"
  if [[ -f "$src" ]]; then install -m 755 "$src" "$STAGING/opt/homebrew/bin/$script"; fi
done

# --- ob_admin / ob_error (optional, relink dylibs) ---
for tool_bin in "$SEEKDB_BUILD/tools/ob_admin/ob_admin" "$SEEKDB_BUILD/tools/ob_error/src/ob_error"; do
  if [[ -x "$tool_bin" ]]; then
    tool_name="$(basename "$tool_bin")"
    install -m 755 "$tool_bin" "$STAGING/opt/homebrew/bin/$tool_name"
    for dep in $(collect_non_system_deps "$tool_bin"); do
      dep_name="$(basename "$dep")"
      if [[ -f "$dep" && ! -f "$DYLIB_DIR/$dep_name" ]]; then
        cp "$dep" "$DYLIB_DIR/$dep_name"
        chmod 644 "$DYLIB_DIR/$dep_name"
      fi
      install_name_tool -change "$dep" "@executable_path/../lib/seekdb/$dep_name" \
        "$STAGING/opt/homebrew/bin/$tool_name" 2>/dev/null
    done
    codesign --force --sign - "$STAGING/opt/homebrew/bin/$tool_name" 2>/dev/null || true
  fi
done

# --- LaunchDaemon plists ---
install -d "$STAGING/Library/LaunchDaemons"
install -m 644 "$MACPKG_DIR/launchd/profile/com.seekdb.server.plist.in" \
  "$STAGING/Library/LaunchDaemons/com.seekdb.server.plist"

# --- Privileged helper ---
if [[ -x "$HELPER_BIN" ]]; then
  install -d "$STAGING/Library/PrivilegedHelperTools"
  install -m 755 "$HELPER_BIN" "$STAGING/Library/PrivilegedHelperTools/com.seekdb.helper"
  install -m 644 "$MENUBAR_SRC/com.seekdb.helper.plist" \
    "$STAGING/Library/LaunchDaemons/com.seekdb.helper.plist"
fi

# --- helper scripts ---
install -d "$STAGING/opt/homebrew/libexec/seekdb/scripts"
install -m 755 "$MACPKG_DIR/launchd/profile/seekdb_launchd_start" \
  "$STAGING/opt/homebrew/libexec/seekdb/scripts/"
install -m 755 "$MACPKG_DIR/launchd/profile/seekdb_launchd_stop" \
  "$STAGING/opt/homebrew/libexec/seekdb/scripts/"
for py in import_time_zone_info.py import_srs_data.py; do
  if [[ -f "$TOPDIR/tools/$py" ]]; then
    install -m 755 "$TOPDIR/tools/$py" "$STAGING/opt/homebrew/libexec/seekdb/"
  fi
done

# --- config ---
install -d "$STAGING/opt/homebrew/etc/seekdb"
install -m 644 "$MACPKG_DIR/launchd/profile/seekdb.cnf" "$STAGING/opt/homebrew/etc/seekdb/"
for f in default_parameter.json default_system_variable.json; do
  src="$TOPDIR/src/share/parameter/$f"
  if [[ ! -f "$src" ]]; then src="$TOPDIR/src/share/system_variable/$f"; fi
  if [[ -f "$src" ]]; then install -m 644 "$src" "$STAGING/opt/homebrew/etc/seekdb/"; fi
done
# ob_system_variable_init.json (generated at build time)
if [[ -f "$SEEKDB_BUILD/src/share/ob_system_variable_init.json" ]]; then
  install -m 644 "$SEEKDB_BUILD/src/share/ob_system_variable_init.json" "$STAGING/opt/homebrew/etc/seekdb/"
fi
for f in oceanbase_upgrade_dep.yml deps_compat.yml; do
  if [[ -f "$TOPDIR/tools/upgrade/$f" ]]; then
    install -m 644 "$TOPDIR/tools/upgrade/$f" "$STAGING/opt/homebrew/etc/seekdb/"
  fi
done

# --- share: admin SQL ---
install -d "$STAGING/opt/homebrew/share/seekdb/admin"
SYS_PACK_DIR="$SEEKDB_BUILD/syspack_release"
if [[ -d "$SYS_PACK_DIR" ]]; then
  cp -R "$SYS_PACK_DIR/"* "$STAGING/opt/homebrew/share/seekdb/admin/" 2>/dev/null || true
fi

# --- share: help ---
install -d "$STAGING/opt/homebrew/share/seekdb/help"
if [[ -f "$TOPDIR/src/sql/fill_help_tables-ob.sql" ]]; then
  install -m 644 "$TOPDIR/src/sql/fill_help_tables-ob.sql" "$STAGING/opt/homebrew/share/seekdb/help/"
fi

# --- share: timezone ---
install -d "$STAGING/opt/homebrew/share/seekdb/timezone"
for f in timezone_V1.log timezone.data timezone_name.data timezone_trans.data timezone_trans_type.data; do
  if [[ -f "$TOPDIR/tools/$f" ]]; then
    install -m 644 "$TOPDIR/tools/$f" "$STAGING/opt/homebrew/share/seekdb/timezone/"
  fi
done

# --- share: srs ---
install -d "$STAGING/opt/homebrew/share/seekdb/srs"
for f in spatial_reference_systems.data default_srs_data_mysql.sql; do
  if [[ -f "$TOPDIR/tools/$f" ]]; then
    install -m 644 "$TOPDIR/tools/$f" "$STAGING/opt/homebrew/share/seekdb/srs/"
  fi
done

# --- share: upgrade ---
install -d "$STAGING/opt/homebrew/share/seekdb/upgrade"
for f in upgrade_pre.py upgrade_post.py upgrade_checker.py upgrade_health_checker.py; do
  if [[ -f "$TOPDIR/tools/upgrade/$f" ]]; then
    install -m 644 "$TOPDIR/tools/upgrade/$f" "$STAGING/opt/homebrew/share/seekdb/upgrade/"
  fi
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
# Disable relocation for the .app bundle so it installs to /Applications
COMPONENT_PLIST="$SEEKDB_BUILD/_pkg_component.plist"
cat > "$COMPONENT_PLIST" <<'CPEOF'
<?xml version="1.0" encoding="UTF-8"?>
<plist version="1.0">
<array>
  <dict>
    <key>BundleIsRelocatable</key>
    <false/>
    <key>BundleIsVersionChecked</key>
    <false/>
    <key>BundleOverwriteAction</key>
    <string>upgrade</string>
    <key>RootRelativeBundlePath</key>
    <string>Applications/SeekDB Monitor.app</string>
  </dict>
</array>
</plist>
CPEOF

pkgbuild \
  --root "$STAGING" \
  --identifier "com.seekdb.server" \
  --version "$VERSION" \
  --scripts "$MACPKG_DIR/launchd/profile" \
  --component-plist "$COMPONENT_PLIST" \
  "$COMPONENT_PKG"

info "Building product archive ..."
# Create a minimal distribution.xml
DIST_XML="$SEEKDB_BUILD/_pkg_distribution.xml"
cat > "$DIST_XML" <<DISTEOF
<?xml version="1.0" encoding="utf-8"?>
<installer-gui-script minSpecVersion="2">
    <title>SeekDB ${VERSION}</title>
    <license file="LICENSE"/>
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
    <pkg-ref id="com.seekdb.server" version="${VERSION}" onConclusion="none">_pkg_component.pkg</pkg-ref>
</installer-gui-script>
DISTEOF

# Prepare resources directory with license
RESOURCES_DIR="$SEEKDB_BUILD/_pkg_resources"
rm -rf "$RESOURCES_DIR"
mkdir -p "$RESOURCES_DIR"
cp "$TOPDIR/LICENSE" "$RESOURCES_DIR/LICENSE"

productbuild \
  --distribution "$DIST_XML" \
  --resources "$RESOURCES_DIR" \
  --package-path "$(dirname "$COMPONENT_PKG")" \
  "$OUTPUT_PKG"

info "Package created: $OUTPUT_PKG"
ls -lh "$OUTPUT_PKG"

# Cleanup temp files
rm -f "$COMPONENT_PKG" "$DIST_XML" "$COMPONENT_PLIST"
rm -rf "$STAGING" "$RESOURCES_DIR"

info "Done."
