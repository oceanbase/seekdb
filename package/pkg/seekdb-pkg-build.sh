#!/usr/bin/env bash
# Assemble a macOS .pkg installer from pre-compiled build artifacts.
#
# Prerequisites:
#   - Existing macOS seekdb artifacts under SEEKDB_BUILD
#   - Xcode Command Line Tools (swiftc, pkgbuild, productbuild)
#
# Usage:
#   ./package/pkg/seekdb-pkg-build.sh [options] PROJECT_NAME VERSION RELEASE
#
# Example:
#   ./package/pkg/seekdb-pkg-build.sh --pkg seekdb 1.4.0 1
#   # produces: package/pkg/seekdb-1.4.0-1-macos15-arm64.pkg
#
# Env:
#   SEEKDB_BUILD   Build directory (default: <repo>/build_release)

set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "$0")" && pwd)"
TOPDIR="$(cd "$SCRIPT_DIR/../.." && pwd)"
MACPKG_DIR="$TOPDIR/tools/macpkg"

SEEKDB_BUILD="${SEEKDB_BUILD:-$TOPDIR/build_release}"

DO_BUILD=false
DO_PKG=false
DO_MENUBAR=true

usage() {
  cat <<'EOF'
Usage: seekdb-pkg-build.sh [options] PROJECT_NAME VERSION RELEASE

  PROJECT_NAME    Package name (e.g. seekdb)
  VERSION         Version string (e.g. 1.4.0)
  RELEASE         Release number (e.g. 1)

Options:
  --build         Unsupported; the Bazel release launcher is Linux-only
  --pkg           Assemble the .pkg installer
  --no-menubar    Skip building the menu bar app
  -h, --help      Show this help

Environment:
  SEEKDB_BUILD    Directory containing existing macOS artifacts
                  (default: <repo>/build_release)

Typical workflow:
  cd <oceanbase-lite>
  SEEKDB_BUILD=/path/to/macos-artifacts \
    ./package/pkg/seekdb-pkg-build.sh --pkg seekdb 1.4.0 1
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

if [[ "$DO_BUILD" == true ]]; then
  die "--build is not supported because the Bazel release launcher currently supports Linux only. Set SEEKDB_BUILD to existing macOS artifacts and omit --build."
fi

[[ "$DO_PKG" == true ]] || { info "No --pkg flag; done."; exit 0; }

MACOS_ARCH="$(uname -m)"
MACOS_VERSION_MAJOR="$(sw_vers -productVersion | cut -d. -f1)"

# ---------------------------------------------------------------------------
# Validate build artifacts
# ---------------------------------------------------------------------------
SEEKDB_BIN="$SEEKDB_BUILD/src/observer/seekdb"
[[ -x "$SEEKDB_BIN" ]] || die "seekdb binary not found: $SEEKDB_BIN (set SEEKDB_BUILD to existing macOS artifacts)"

# ---------------------------------------------------------------------------
# Step 1: Build menu bar app
# ---------------------------------------------------------------------------
MENUBAR_SRC="$MACPKG_DIR/seekdbctl/menubar"
MENUBAR_BIN="$SEEKDB_BUILD/seekdb-menubar"

HELPER_BIN="$SEEKDB_BUILD/seekdb-helper"
ICON_ASSETS_DIR="$MACPKG_DIR/assets"
SVG2PNG_BIN="$SEEKDB_BUILD/svg2png"
APP_ICONSET="$SEEKDB_BUILD/AppIcon.iconset"
APP_ICON_ICNS="$SEEKDB_BUILD/AppIcon.icns"

if [[ "$DO_MENUBAR" == true && -f "$MENUBAR_SRC/SeekDBMenuBar.swift" ]]; then
  info "Compiling seekdb menu bar app ..."
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
# Step 2: Assemble staging directory (the on-disk layout after install)
# ---------------------------------------------------------------------------
PKG_NAME="${PROJECT_NAME}-${VERSION}-${RELEASE}-macos${MACOS_VERSION_MAJOR}-${MACOS_ARCH}"
STAGING="$SEEKDB_BUILD/_pkg_staging"
APP_BUNDLE_NAME="seekdb Monitor.app"
APP_EXECUTABLE="seekdb-menubar"
rm -rf "$STAGING"

info "Assembling staging directory ..."

# --- binaries ---
install -d "$STAGING/opt/seekdb/bin"
install -d "$STAGING/opt/seekdb/lib/seekdb"
install -m 755 "$SEEKDB_BIN" "$STAGING/opt/seekdb/bin/seekdb"

# --- bundle non-system dylibs (recursive) ---
info "Bundling dynamic libraries ..."
DYLIB_DIR="$STAGING/opt/seekdb/lib/seekdb"

# An executable may only depend on system libraries, leaving this directory
# empty. Expand unmatched dylib globs to an empty list instead of a literal
# "*.dylib" path.
shopt -s nullglob

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

BUNDLED_COUNT=$(find "$DYLIB_DIR" -maxdepth 1 -type f -name '*.dylib' | wc -l | tr -d ' ')
info "  bundled $BUNDLED_COUNT dylibs"

# rewrite paths: seekdb binary
for dep in $(collect_non_system_deps "$STAGING/opt/seekdb/bin/seekdb"); do
  dep_name="$(basename "$dep")"
  install_name_tool -change "$dep" "@executable_path/../lib/seekdb/$dep_name" \
    "$STAGING/opt/seekdb/bin/seekdb" 2>/dev/null
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
shopt -u nullglob
codesign --force --sign - "$STAGING/opt/seekdb/bin/seekdb" 2>/dev/null || true
for script in seekdbctl seekdb_start seekdb_stop seekdb_status seekdb_config \
              seekdb_setup seekdb_paths seekdb_uninstall; do
  src="$MACPKG_DIR/seekdbctl/$script"
  if [[ -f "$src" ]]; then install -m 755 "$src" "$STAGING/opt/seekdb/bin/$script"; fi
done

# --- ob_admin / ob_error (optional, relink dylibs) ---
for tool_bin in "$SEEKDB_BUILD/tools/ob_admin/ob_admin" "$SEEKDB_BUILD/tools/ob_error/src/ob_error"; do
  if [[ -x "$tool_bin" ]]; then
    tool_name="$(basename "$tool_bin")"
    install -m 755 "$tool_bin" "$STAGING/opt/seekdb/bin/$tool_name"
    for dep in $(collect_non_system_deps "$tool_bin"); do
      dep_name="$(basename "$dep")"
      if [[ -f "$dep" && ! -f "$DYLIB_DIR/$dep_name" ]]; then
        cp "$dep" "$DYLIB_DIR/$dep_name"
        chmod 644 "$DYLIB_DIR/$dep_name"
      fi
      install_name_tool -change "$dep" "@executable_path/../lib/seekdb/$dep_name" \
        "$STAGING/opt/seekdb/bin/$tool_name" 2>/dev/null
    done
    codesign --force --sign - "$STAGING/opt/seekdb/bin/$tool_name" 2>/dev/null || true
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
install -d "$STAGING/opt/seekdb/libexec/seekdb/scripts"
install -m 755 "$MACPKG_DIR/launchd/profile/seekdb_launchd_start" \
  "$STAGING/opt/seekdb/libexec/seekdb/scripts/"
install -m 755 "$MACPKG_DIR/launchd/profile/seekdb_launchd_stop" \
  "$STAGING/opt/seekdb/libexec/seekdb/scripts/"
for py in import_time_zone_info.py import_srs_data.py seekdb_cli.py; do
  if [[ -f "$TOPDIR/tools/$py" ]]; then
    install -m 755 "$TOPDIR/tools/$py" "$STAGING/opt/seekdb/libexec/seekdb/"
  fi
done
if [[ -f "$TOPDIR/tools/seekdb-cli" ]]; then
  install -m 755 "$TOPDIR/tools/seekdb-cli" "$STAGING/opt/seekdb/libexec/seekdb/"
fi

# --- config ---
install -d "$STAGING/opt/seekdb/etc/seekdb"
install -m 644 "$MACPKG_DIR/launchd/profile/seekdb.cnf" "$STAGING/opt/seekdb/etc/seekdb/"
for f in default_parameter.json default_system_variable.json; do
  src="$TOPDIR/src/share/parameter/$f"
  if [[ ! -f "$src" ]]; then src="$TOPDIR/src/share/system_variable/$f"; fi
  if [[ -f "$src" ]]; then install -m 644 "$src" "$STAGING/opt/seekdb/etc/seekdb/"; fi
done
# ob_system_variable_init.json (generated at build time)
if [[ -f "$SEEKDB_BUILD/src/share/ob_system_variable_init.json" ]]; then
  install -m 644 "$SEEKDB_BUILD/src/share/ob_system_variable_init.json" "$STAGING/opt/seekdb/etc/seekdb/"
fi

# --- share: admin SQL ---
install -d "$STAGING/opt/seekdb/share/seekdb/admin"
SYS_PACK_DIR="$SEEKDB_BUILD/syspack_release"
if [[ -d "$SYS_PACK_DIR" ]]; then
  cp -R "$SYS_PACK_DIR/"* "$STAGING/opt/seekdb/share/seekdb/admin/" 2>/dev/null || true
fi

# --- share: timezone ---
install -d "$STAGING/opt/seekdb/share/seekdb/timezone"
for f in timezone_V1.log; do
  if [[ -f "$TOPDIR/tools/$f" ]]; then
    install -m 644 "$TOPDIR/tools/$f" "$STAGING/opt/seekdb/share/seekdb/timezone/"
  fi
done

# --- share: srs ---
install -d "$STAGING/opt/seekdb/share/seekdb/srs"
for f in default_srs_data_mysql.sql; do
  if [[ -f "$TOPDIR/tools/$f" ]]; then
    install -m 644 "$TOPDIR/tools/$f" "$STAGING/opt/seekdb/share/seekdb/srs/"
  fi
done

# --- seekdb Monitor .app bundle ---
if [[ "$DO_MENUBAR" == true && -x "$MENUBAR_BIN" ]]; then
  APP_BUNDLE="$STAGING/Applications/$APP_BUNDLE_NAME"
  APP_DIR="$APP_BUNDLE/Contents"
  install -d "$APP_DIR/MacOS" "$APP_DIR/Resources"
  install -m 755 "$MENUBAR_BIN" "$APP_DIR/MacOS/$APP_EXECUTABLE"

  [[ -f "$ICON_ASSETS_DIR/original.svg" ]] || die "app icon source not found: $ICON_ASSETS_DIR/original.svg"
  [[ -f "$MACPKG_DIR/svg2png.swift" ]] || die "SVG renderer source not found: $MACPKG_DIR/svg2png.swift"
  info "Generating app icon ..."
  swiftc \
    -o "$SVG2PNG_BIN" \
    -framework AppKit \
    -target arm64-apple-macosx13.0 \
    "$MACPKG_DIR/svg2png.swift"
  rm -rf "$APP_ICONSET" "$APP_ICON_ICNS"
  mkdir -p "$APP_ICONSET"
  "$SVG2PNG_BIN" "$ICON_ASSETS_DIR/original.svg" "$APP_ICONSET/icon_16x16.png" 16
  "$SVG2PNG_BIN" "$ICON_ASSETS_DIR/original.svg" "$APP_ICONSET/icon_16x16@2x.png" 32
  "$SVG2PNG_BIN" "$ICON_ASSETS_DIR/original.svg" "$APP_ICONSET/icon_32x32.png" 32
  "$SVG2PNG_BIN" "$ICON_ASSETS_DIR/original.svg" "$APP_ICONSET/icon_32x32@2x.png" 64
  "$SVG2PNG_BIN" "$ICON_ASSETS_DIR/original.svg" "$APP_ICONSET/icon_128x128.png" 128
  "$SVG2PNG_BIN" "$ICON_ASSETS_DIR/original.svg" "$APP_ICONSET/icon_128x128@2x.png" 256
  "$SVG2PNG_BIN" "$ICON_ASSETS_DIR/original.svg" "$APP_ICONSET/icon_256x256.png" 256
  "$SVG2PNG_BIN" "$ICON_ASSETS_DIR/original.svg" "$APP_ICONSET/icon_256x256@2x.png" 512
  "$SVG2PNG_BIN" "$ICON_ASSETS_DIR/original.svg" "$APP_ICONSET/icon_512x512.png" 512
  "$SVG2PNG_BIN" "$ICON_ASSETS_DIR/original.svg" "$APP_ICONSET/icon_512x512@2x.png" 1024
  iconutil -c icns -o "$APP_ICON_ICNS" "$APP_ICONSET"
  install -m 644 "$APP_ICON_ICNS" "$APP_DIR/Resources/AppIcon.icns"

  for icon in active loading stopped; do
    [[ -f "$ICON_ASSETS_DIR/$icon.svg" ]] || die "status icon source not found: $ICON_ASSETS_DIR/$icon.svg"
    install -m 644 "$ICON_ASSETS_DIR/$icon.svg" "$APP_DIR/Resources/$icon.svg"
  done

  # Generate Info.plist from template
  sed "s/@OceanBase_VERSION@/${VERSION}/g" "$MENUBAR_SRC/info.plist.in" > "$APP_DIR/Info.plist"
  codesign --force --sign - "$APP_DIR/MacOS/$APP_EXECUTABLE"
  codesign --force --sign - "$APP_BUNDLE"
  info "Menu bar app bundled"
fi

info "Staging complete: $(find "$STAGING" -type f | wc -l | tr -d ' ') files"

# ---------------------------------------------------------------------------
# Step 3: Build .pkg with pkgbuild + productbuild
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
    <string>Applications/seekdb Monitor.app</string>
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
    <title>seekdb ${VERSION}</title>
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
rm -f "$COMPONENT_PKG" "$DIST_XML" "$COMPONENT_PLIST" "$SVG2PNG_BIN" "$APP_ICON_ICNS"
rm -rf "$STAGING" "$RESOURCES_DIR" "$APP_ICONSET"

info "Done."
