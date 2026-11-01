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
#   ./package/pkg/seekdb-pkg-build.sh seekdb 1.4.0 1
#   # produces: package/pkg/seekdb-1.4.0-1-macos15-arm64.pkg
#
# Env:
#   SEEKDB_BUILD   Build directory (default: <repo>/build/bin)

set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "$0")" && pwd)"
TOPDIR="$(cd "$SCRIPT_DIR/../.." && pwd)"
MACPKG_DIR="$TOPDIR/package/pkg"

SEEKDB_BUILD="${SEEKDB_BUILD:-$TOPDIR/build/bin}"

DO_MENUBAR=true

usage() {
  cat <<'EOF'
Usage: seekdb-pkg-build.sh [options] PROJECT_NAME VERSION RELEASE

  PROJECT_NAME    Package name (e.g. seekdb)
  VERSION         Version string (e.g. 1.4.0)
  RELEASE         Release number (e.g. 1)

Options:
  --no-menubar    Skip building the menu bar app
  -h, --help      Show this help

Environment:
  SEEKDB_BUILD    Directory containing existing macOS artifacts
                  (default: <repo>/build/bin)

Typical workflow:
  cd <oceanbase-lite>
  SEEKDB_BUILD=/path/to/macos-artifacts \
    ./package/pkg/seekdb-pkg-build.sh seekdb 1.4.0 1
EOF
}

while [[ $# -gt 0 ]]; do
  case "$1" in
    --no-menubar) DO_MENUBAR=false; shift ;;
    -h|--help)    usage; exit 0 ;;
    -*)           echo "Unknown option: $1" >&2; usage >&2; exit 1 ;;
    *)            break ;;
  esac
done

PROJECT_NAME="${1:-}"
VERSION="${2:-}"
RELEASE="${3:-}"

if [[ $# != 3 ]]; then
  usage >&2
  exit 64
fi

info() { printf '[seekdb-pkg-build] %s\n' "$*"; }
die()  { printf '[seekdb-pkg-build][ERROR] %s\n' "$*" >&2; exit 1; }

[[ "$PROJECT_NAME" == seekdb ]] || die "only seekdb is packaged"
[[ "$(uname -s)" == Darwin ]] || die "PKG assembly requires macOS"
PKG_WORK="$(mktemp -d "${TMPDIR:-/tmp}/seekdb-pkg.XXXXXX")"
trap 'rm -rf "$PKG_WORK"' EXIT
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
MENUBAR_BIN="$PKG_WORK/seekdb-menubar"

HELPER_BIN="$PKG_WORK/seekdb-helper"
ICON_ASSETS_DIR="$MACPKG_DIR/assets"
SVG2PNG_BIN="$PKG_WORK/svg2png"
APP_ICONSET="$PKG_WORK/AppIcon.iconset"
APP_ICON_ICNS="$PKG_WORK/AppIcon.icns"

if [[ "$DO_MENUBAR" == true && -f "$MENUBAR_SRC/SeekDBMenuBar.swift" ]]; then
  info "Compiling seekdb menu bar app ..."
  swiftc \
    -o "$MENUBAR_BIN" \
    -framework AppKit \
    -framework Security \
    -target "${MACOS_ARCH}-apple-macosx13.0" \
    -O \
    "$MENUBAR_SRC/SeekDBMenuBar.swift"
  codesign --force --sign - "$MENUBAR_BIN"
  info "Menu bar app compiled: $MENUBAR_BIN"

  info "Compiling privileged helper ..."
  swiftc \
    -o "$HELPER_BIN" \
    -framework Foundation \
    -target "${MACOS_ARCH}-apple-macosx13.0" \
    -O \
    "$MENUBAR_SRC/SeekDBHelper.swift"
  codesign --force --sign - "$HELPER_BIN"
  info "Helper compiled: $HELPER_BIN"
fi

# ---------------------------------------------------------------------------
# Step 2: Assemble staging directory (the on-disk layout after install)
# ---------------------------------------------------------------------------
PKG_NAME="${PROJECT_NAME}-${VERSION}-${RELEASE}-macos${MACOS_VERSION_MAJOR}-${MACOS_ARCH}"
STAGING="$PKG_WORK/_pkg_staging"
APP_BUNDLE_NAME="seekdb Monitor.app"
APP_EXECUTABLE="seekdb-menubar"

info "Assembling staging directory ..."

python3 "$TOPDIR/package/package.py" stage --layout macos \
  --artifacts "$SEEKDB_BUILD" --version "$VERSION" --release "$RELEASE" --output "$STAGING"
python3 "$SCRIPT_DIR/bundle_dylibs.py" "$STAGING/opt/seekdb" "$SEEKDB_BIN"

for script in seekdbctl seekdb_start seekdb_stop seekdb_status seekdb_config \
              seekdb_setup seekdb_paths seekdb_uninstall; do
  src="$MACPKG_DIR/seekdbctl/$script"
  if [[ -f "$src" ]]; then install -m 755 "$src" "$STAGING/opt/seekdb/bin/$script"; fi
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

install -d "$STAGING/opt/seekdb/libexec/seekdb/scripts"
install -m 755 "$MACPKG_DIR/launchd/profile/seekdb_launchd_start" \
  "$MACPKG_DIR/launchd/profile/seekdb_launchd_stop" "$STAGING/opt/seekdb/libexec/seekdb/scripts/"

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
    -target "${MACOS_ARCH}-apple-macosx13.0" \
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
COMPONENT_PKG="$PKG_WORK/_pkg_component.pkg"
OUTPUT_PKG="$SCRIPT_DIR/${PKG_NAME}.pkg"

info "Building component package ..."
# Disable relocation for the .app bundle so it installs to /Applications
COMPONENT_PLIST="$PKG_WORK/_pkg_component.plist"
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

component_command=(pkgbuild --root "$STAGING" --identifier com.seekdb.server
                   --version "$VERSION" --scripts "$MACPKG_DIR/launchd/profile")
if [[ -d "$STAGING/Applications/$APP_BUNDLE_NAME" ]]; then
  component_command+=(--component-plist "$COMPONENT_PLIST")
fi
component_command+=("$COMPONENT_PKG")
"${component_command[@]}"

info "Building product archive ..."
# Create a minimal distribution.xml
DIST_XML="$PKG_WORK/_pkg_distribution.xml"
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
RESOURCES_DIR="$PKG_WORK/_pkg_resources"
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

info "Done."
