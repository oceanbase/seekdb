#!/usr/bin/env bash
# Sync stripped Android embed artifacts into a Gradle app's jniLibs/arm64-v8a,
# optionally build native targets and assemble APK.
#
# The built-in Android Gradle project is generated on first use under
# <repo>/build_android_app/ (no Gradle files committed to this repository).
#
# Prerequisites:
#   - Android NDK (ANDROID_NDK_HOME)
#   - curl or wget (to download gradle-wrapper.jar on first run)
# Note: build.sh clean + release --android --init are run automatically by --build.
#
# Usage:
#   ./package/apk/seekdb-apk-build.sh [options] PROJECT_NAME VERSION RELEASE
#
# Example:
#   ./package/apk/seekdb-apk-build.sh --build --apk seekdb 4.3.5 1
#   # produces rpm/seekdb-4.3.5-1.apk
#
# Env:
#   SEEKDB_BUILD      Default: <repo>/build_android_release
#   ANDROID_NDK_HOME  NDK root (default: $ANDROID_HOME/ndk/27.3.13750724)
#   ANDROID_HOME      Android SDK root (default: $HOME/Library/Android/sdk)

set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "$0")" && pwd)"
TOPDIR="$(cd "$SCRIPT_DIR/../.." && pwd)"
CURDIR="$SCRIPT_DIR"

SEEKDB_BUILD="${SEEKDB_BUILD:-$TOPDIR/build_android_release}"
EMBED_DIR="$SEEKDB_BUILD/src/observer/embed"
ANDROID_HOME="${ANDROID_HOME:-${ANDROID_SDK_ROOT:-$HOME/Library/Android/sdk}}"
ANDROID_NDK_HOME="${ANDROID_NDK_HOME:-$ANDROID_HOME/ndk/27.3.13750724}"

CLIENT_ROOT="$TOPDIR/build_android_app"

DO_MAKE=false
DO_APK=false
DO_INSTALL=false
USE_SYMLINK=false
WITH_JNI=false

usage() {
  cat <<'EOF'
Usage: package_embedded_apk.sh [options] PROJECT_NAME VERSION RELEASE

  PROJECT_NAME    Package name (e.g. seekdb)
  VERSION         Version string (e.g. 4.3.5)
  RELEASE         Release number (e.g. 1)
  -> output APK:  tools/android/<PROJECT_NAME>-<VERSION>-<RELEASE>.apk

Options:
  --build         Run make for embed targets in SEEKDB_BUILD (see --with-jni)
  --apk           Run ./gradlew assembleDebug and rename output APK
  --install       adb install -r the output APK (after --apk)
  --symlink       Use ln -sf into jniLibs instead of strip (for iteration)
  --with-jni      Also build/sync libseekdb_embed.so
  -h, --help      Show this help

Environment:
  SEEKDB_BUILD      Android CMake build dir (default: <repo>/build_android_release)
  ANDROID_HOME      Android SDK root (default: $HOME/Library/Android/sdk)
  ANDROID_NDK_HOME  NDK root (default: $ANDROID_HOME/ndk/27.3.13750724)
  JAVA_HOME         JDK 17+ for Gradle (auto-detected from Android Studio JBR if unset)

Typical workflow:
  cd <oceanbase-lite>
  ./package/apk/seekdb-apk-build.sh --build --apk seekdb 4.3.5 1
EOF
}

while [[ $# -gt 0 ]]; do
  case "$1" in
    --build)    DO_MAKE=true;    shift ;;
    --apk)      DO_APK=true;     shift ;;
    --install)  DO_INSTALL=true; shift ;;
    --symlink)  USE_SYMLINK=true; shift ;;
    --with-jni) WITH_JNI=true;   shift ;;
    -h|--help)  usage; exit 0 ;;
    -*)
      echo "Unknown option: $1" >&2
      usage >&2
      exit 1
      ;;
    *) break ;;
  esac
done

PROJECT_NAME="${1:-}"
VERSION="${2:-}"
RELEASE="${3:-}"

if [[ "$DO_APK" == true ]]; then
  if [[ -z "$PROJECT_NAME" || -z "$VERSION" || -z "$RELEASE" ]]; then
    echo "Error: PROJECT_NAME VERSION RELEASE are required when using --apk." >&2
    usage >&2
    exit 1
  fi
fi

echo "[package_embedded_apk] args: PROJECT_NAME=${PROJECT_NAME:-<unset>} VERSION=${VERSION:-<unset>} RELEASE=${RELEASE:-<unset>}"

# ---------------------------------------------------------------------------
# init_android_client – generate the built-in Gradle project under CLIENT_ROOT
# on first use (gradlew absent).
# ---------------------------------------------------------------------------
init_android_client() {
  local root="$1"
  echo "[package_embedded_apk] Initialising built-in Android client project at $root ..."

  mkdir -p "$root/gradle/wrapper"
  mkdir -p "$root/app/src/main/java/com/seekdb/sotest"
  mkdir -p "$root/app/src/main/jniLibs/arm64-v8a"

  # -- root build.gradle.kts ------------------------------------------------
  cat > "$root/build.gradle.kts" <<'GRADLE_EOF'
plugins {
    id("com.android.application") version "8.7.3" apply false
}
GRADLE_EOF

  # -- settings.gradle.kts --------------------------------------------------
  cat > "$root/settings.gradle.kts" <<'SETTINGS_EOF'
pluginManagement {
    repositories {
        google()
        mavenCentral()
        gradlePluginPortal()
    }
}

@Suppress("UnstableApiUsage")
dependencyResolutionManagement {
    repositoriesMode.set(RepositoriesMode.FAIL_ON_PROJECT_REPOS)
    repositories {
        google()
        mavenCentral()
    }
}

rootProject.name = "seekdb-android-embedded-client"
include(":app")
SETTINGS_EOF

  # -- gradle.properties ----------------------------------------------------
  cat > "$root/gradle.properties" <<'PROPS_EOF'
org.gradle.jvmargs=-Xmx4096m -Dfile.encoding=UTF-8
android.useAndroidX=true
android.nonTransitiveRClass=true
PROPS_EOF

  # -- gradle/wrapper/gradle-wrapper.properties -----------------------------
  cat > "$root/gradle/wrapper/gradle-wrapper.properties" <<'WRAPPER_PROPS_EOF'
distributionBase=GRADLE_USER_HOME
distributionPath=wrapper/dists
distributionUrl=https\://services.gradle.org/distributions/gradle-8.9-bin.zip
networkTimeout=10000
validateDistributionUrl=true
zipStoreBase=GRADLE_USER_HOME
zipStorePath=wrapper/dists
WRAPPER_PROPS_EOF

  # -- app/build.gradle.kts -------------------------------------------------
  cat > "$root/app/build.gradle.kts" <<'APP_GRADLE_EOF'
plugins {
    id("com.android.application")
}

android {
    namespace = "com.seekdb.sotest"
    compileSdk = 35

    defaultConfig {
        applicationId = "com.seekdb.sotest"
        minSdk = 28
        targetSdk = 30
        versionCode = 1
        versionName = "1.0"

        ndk {
            abiFilters += "arm64-v8a"
        }
    }

    buildTypes {
        release {
            isMinifyEnabled = false
        }
    }

    compileOptions {
        sourceCompatibility = JavaVersion.VERSION_17
        targetCompatibility = JavaVersion.VERSION_17
    }

    packaging {
        jniLibs {
            useLegacyPackaging = true
        }
    }
}

dependencies {
    implementation("androidx.core:core:1.12.0")
}
APP_GRADLE_EOF

  # -- AndroidManifest.xml --------------------------------------------------
  cat > "$root/app/src/main/AndroidManifest.xml" <<'MANIFEST_EOF'
<?xml version="1.0" encoding="utf-8"?>
<manifest xmlns:android="http://schemas.android.com/apk/res/android">

    <application
        android:allowBackup="false"
        android:label="SeekDB Embedded Client"
        android:largeHeap="true">

        <activity
            android:name=".MainActivity"
            android:exported="true">
            <intent-filter>
                <action android:name="android.intent.action.MAIN" />
                <category android:name="android.intent.category.LAUNCHER" />
            </intent-filter>
        </activity>
    </application>
</manifest>
MANIFEST_EOF

  # -- MainActivity.java ----------------------------------------------------
  cat > "$root/app/src/main/java/com/seekdb/sotest/MainActivity.java" <<'ACTIVITY_EOF'
package com.seekdb.sotest;

import android.app.Activity;
import android.os.Bundle;
import android.widget.TextView;

public class MainActivity extends Activity {
    @Override
    protected void onCreate(Bundle savedInstanceState) {
        super.onCreate(savedInstanceState);
        String nativeLibDir = getApplicationInfo().nativeLibraryDir;
        String dataDir = getApplicationInfo().dataDir;

        TextView tv = new TextView(this);
        tv.setPadding(32, 32, 32, 32);
        tv.setTextSize(14);
        tv.setText("SeekDB Embedded Client\n\n"
            + "Native libs: " + nativeLibDir + "\n\n"
            + "Data dir: " + dataDir + "\n\n"
            + "Run via adb shell:\n"
            + "  cd " + nativeLibDir + "\n"
            + "  LD_LIBRARY_PATH=. ./libembedded_client.so "
            + "--db-dir " + dataDir + "/seekdb_data\n");
        setContentView(tv);
    }
}
ACTIVITY_EOF

  # -- gradlew (Apache-2.0, generated by Gradle) ----------------------------
  cat > "$root/gradlew" <<'GRADLEW_EOF'
#!/bin/sh

#
# Copyright © 2015-2021 the original authors.
#
# Licensed under the Apache License, Version 2.0 (the "License");
# you may not use this file except in compliance with the License.
# You may obtain a copy of the License at
#
#      https://www.apache.org/licenses/LICENSE-2.0
#
# Unless required by applicable law or agreed to in writing, software
# distributed under the License is distributed on an "AS IS" BASIS,
# WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
# See the License for the specific language governing permissions and
# limitations under the License.
#
# SPDX-License-Identifier: Apache-2.0
#

##############################################################################
#
#   Gradle start up script for POSIX generated by Gradle.
#
##############################################################################

# Attempt to set APP_HOME

# Resolve links: $0 may be a link
app_path=$0

# Need this for daisy-chained symlinks.
while
    APP_HOME=${app_path%"${app_path##*/}"}  # leaves a trailing /; empty if no leading path
    [ -h "$app_path" ]
do
    ls=$( ls -ld "$app_path" )
    link=${ls#*' -> '}
    case $link in             #(
      /*)   app_path=$link ;; #(
      *)    app_path=$APP_HOME$link ;;
    esac
done

# This is normally unused
# shellcheck disable=SC2034
APP_BASE_NAME=${0##*/}
# Discard cd standard output in case $CDPATH is set (https://github.com/gradle/gradle/issues/25036)
APP_HOME=$( cd -P "${APP_HOME:-./}" > /dev/null && printf '%s\n' "$PWD" ) || exit

# Use the maximum available, or set MAX_FD != -1 to use that value.
MAX_FD=maximum

warn () {
    echo "$*"
} >&2

die () {
    echo
    echo "$*"
    echo
    exit 1
} >&2

# OS specific support (must be 'true' or 'false').
cygwin=false
msys=false
darwin=false
nonstop=false
case "$( uname )" in                #(
  CYGWIN* )         cygwin=true  ;; #(
  Darwin* )         darwin=true  ;; #(
  MSYS* | MINGW* )  msys=true    ;; #(
  NONSTOP* )        nonstop=true ;;
esac

CLASSPATH=$APP_HOME/gradle/wrapper/gradle-wrapper.jar


# Determine the Java command to use to start the JVM.
if [ -n "$JAVA_HOME" ] ; then
    if [ -x "$JAVA_HOME/jre/sh/java" ] ; then
        # IBM's JDK on AIX uses strange locations for the executables
        JAVACMD=$JAVA_HOME/jre/sh/java
    else
        JAVACMD=$JAVA_HOME/bin/java
    fi
    if [ ! -x "$JAVACMD" ] ; then
        die "ERROR: JAVA_HOME is set to an invalid directory: $JAVA_HOME

Please set the JAVA_HOME variable in your environment to match the
location of your Java installation."
    fi
else
    JAVACMD=java
    if ! command -v java >/dev/null 2>&1
    then
        die "ERROR: JAVA_HOME is not set and no 'java' command could be found in your PATH.

Please set the JAVA_HOME variable in your environment to match the
location of your Java installation."
    fi
fi

# Increase the maximum file descriptors if we can.
if ! "$cygwin" && ! "$darwin" && ! "$nonstop" ; then
    case $MAX_FD in #(
      max*)
        # shellcheck disable=SC2039,SC3045
        MAX_FD=$( ulimit -H -n ) ||
            warn "Could not query maximum file descriptor limit"
    esac
    case $MAX_FD in  #(
      '' | soft) :;; #(
      *)
        # shellcheck disable=SC2039,SC3045
        ulimit -n "$MAX_FD" ||
            warn "Could not set maximum file descriptor limit to $MAX_FD"
    esac
fi

# For Cygwin or MSYS, switch paths to Windows format before running java
if "$cygwin" || "$msys" ; then
    APP_HOME=$( cygpath --path --mixed "$APP_HOME" )
    CLASSPATH=$( cygpath --path --mixed "$CLASSPATH" )
    JAVACMD=$( cygpath --unix "$JAVACMD" )
    for arg do
        if
            case $arg in                                #(
              -*)   false ;;                            #(
              /?*)  t=${arg#/} t=/${t%%/*}
                    [ -e "$t" ] ;;                      #(
              *)    false ;;
            esac
        then
            arg=$( cygpath --path --ignore --mixed "$arg" )
        fi
        shift
        set -- "$@" "$arg"
    done
fi

DEFAULT_JVM_OPTS='-Dfile.encoding=UTF-8 "-Xmx64m" "-Xms64m"'

set -- \
        "-Dorg.gradle.appname=$APP_BASE_NAME" \
        -classpath "$CLASSPATH" \
        org.gradle.wrapper.GradleWrapperMain \
        "$@"

if ! command -v xargs >/dev/null 2>&1
then
    die "xargs is not available"
fi

eval "set -- $(
        printf '%s\n' "$DEFAULT_JVM_OPTS $JAVA_OPTS $GRADLE_OPTS" |
        xargs -n1 |
        sed ' s~[^-[:alnum:]+,./:=@_]~\\&~g; ' |
        tr '\n' ' '
    )" '"$@"'

exec "$JAVACMD" "$@"
GRADLEW_EOF
  chmod +x "$root/gradlew"

  # -- gradle-wrapper.jar (binary, must be downloaded) ----------------------
  local jar="$root/gradle/wrapper/gradle-wrapper.jar"
  local jar_sha256="498495120a03b9a6ab5d155f5de3c8f0d986a449153702fb80fc80e134484f17"
  local jar_url="https://raw.githubusercontent.com/gradle/gradle/v8.9.0/gradle/wrapper/gradle-wrapper.jar"

  echo "[package_embedded_apk] Downloading gradle-wrapper.jar ..."
  if command -v curl >/dev/null 2>&1; then
    curl -fsSL "$jar_url" -o "$jar" || true
  elif command -v wget >/dev/null 2>&1; then
    wget -q "$jar_url" -O "$jar" || true
  else
    echo "Error: curl or wget is required to download gradle-wrapper.jar." >&2
    exit 1
  fi

  if [[ ! -s "$jar" ]]; then
    echo "Error: failed to download gradle-wrapper.jar." >&2
    echo "" >&2
    echo "  Download URL (requires network access to GitHub):" >&2
    echo "    $jar_url" >&2
    echo "" >&2
    echo "  If the URL is inaccessible (e.g. behind a firewall), copy it manually:" >&2
    echo "    cp <seekdb-android-embedded-client>/gradle/wrapper/gradle-wrapper.jar \\" >&2
    echo "       $jar" >&2
    echo "" >&2
    echo "  Or download it via a browser / proxy and place it at:" >&2
    echo "    $jar" >&2
    exit 1
  fi

  local actual_sha256=""
  if command -v shasum >/dev/null 2>&1; then
    actual_sha256="$(shasum -a 256 "$jar" | awk '{print $1}')"
  elif command -v sha256sum >/dev/null 2>&1; then
    actual_sha256="$(sha256sum "$jar" | awk '{print $1}')"
  fi
  if [[ -n "$actual_sha256" && "$actual_sha256" != "$jar_sha256" ]]; then
    echo "Error: gradle-wrapper.jar checksum mismatch." >&2
    echo "  expected: $jar_sha256" >&2
    echo "  got:      $actual_sha256" >&2
    rm -f "$jar"
    exit 1
  fi

  echo "[package_embedded_apk] Built-in Android client project ready at $root"
}

JNILIBS="$CLIENT_ROOT/app/src/main/jniLibs/arm64-v8a"

if [[ ! -d "$ANDROID_NDK_HOME" ]]; then
  echo "Error: ANDROID_NDK_HOME not found: $ANDROID_NDK_HOME" >&2
  exit 1
fi

_prebuilt=""
for cand in darwin-arm64 darwin-x86_64 linux-x86_64; do
  if [[ -d "$ANDROID_NDK_HOME/toolchains/llvm/prebuilt/$cand" ]]; then
    _prebuilt="$cand"
    break
  fi
done
if [[ -z "$_prebuilt" ]]; then
  echo "Error: no NDK prebuilt under $ANDROID_NDK_HOME/toolchains/llvm/prebuilt/" >&2
  exit 1
fi
NDK_STRIP="$ANDROID_NDK_HOME/toolchains/llvm/prebuilt/$_prebuilt/bin/llvm-strip"

if [[ "$DO_MAKE" == true ]]; then
  cd "$TOPDIR"
  echo "[seekdb-apk-build] ./build.sh clean"
  ./build.sh clean
  echo "[seekdb-apk-build] ./build.sh release --android --init -DBUILD_EMBED_MODE=ON"
  ./build.sh release --android --init -DBUILD_EMBED_MODE=ON
  cd "$TOPDIR"
  _jobs="$(getconf _NPROCESSORS_ONLN 2>/dev/null || sysctl -n hw.ncpu 2>/dev/null || echo 4)"
  if [[ "$WITH_JNI" == true ]]; then
    echo "[seekdb-apk-build] make seekdb_embed_c embedded_client seekdb_embed ..."
    make -C "$SEEKDB_BUILD" -j"$_jobs" seekdb_embed_c embedded_client seekdb_embed
  else
    echo "[seekdb-apk-build] make seekdb_embed_c embedded_client ..."
    make -C "$SEEKDB_BUILD" -j"$_jobs" seekdb_embed_c embedded_client
  fi
fi

for f in "$EMBED_DIR/libseekdb_embed_c.so" "$EMBED_DIR/embedded_client"; do
  if [[ ! -e "$f" ]]; then
    echo "Error: missing $f (use --build or build embed targets first)" >&2
    exit 1
  fi
done
if [[ "$WITH_JNI" == true && ! -e "$EMBED_DIR/libseekdb_embed.so" ]]; then
  echo "Error: missing $EMBED_DIR/libseekdb_embed.so (use --build --with-jni)" >&2
  exit 1
fi

mkdir -p "$JNILIBS"
if [[ "$WITH_JNI" != true ]]; then
  rm -f "$JNILIBS/libseekdb_embed.so"
fi
echo "[package_embedded_apk] strip -> $JNILIBS"

install_one() {
  local src="$1" dest="$2"
  if [[ "$USE_SYMLINK" == true ]]; then
    ln -sf "$src" "$dest"
  else
    "$NDK_STRIP" -o "$dest" "$src"
  fi
}

install_one "$EMBED_DIR/libseekdb_embed_c.so" "$JNILIBS/libseekdb_embed_c.so"
install_one "$EMBED_DIR/embedded_client"       "$JNILIBS/libembedded_client.so"
if [[ "$WITH_JNI" == true ]]; then
  install_one "$EMBED_DIR/libseekdb_embed.so" "$JNILIBS/libseekdb_embed.so"
fi

# Gradle requires a real JDK.
ensure_java_for_gradle() {
  if [[ -n "${JAVA_HOME:-}" && -x "${JAVA_HOME}/bin/java" ]]; then
    return 0
  fi
  local -a candidates=(
    "/Applications/Android Studio.app/Contents/jbr/Contents/Home"
    "/Applications/Android Studio.app/Contents/jre/Contents/Home"
  )
  local h
  for h in "${candidates[@]}"; do
    if [[ -x "$h/bin/java" ]]; then
      export JAVA_HOME="$h"
      echo "[package_embedded_apk] JAVA_HOME=$JAVA_HOME (Android Studio)"
      return 0
    fi
  done
  shopt -s nullglob
  for h in /Library/Java/JavaVirtualMachines/*/Contents/Home; do
    if [[ -x "$h/bin/java" ]]; then
      export JAVA_HOME="$h"
      echo "[package_embedded_apk] JAVA_HOME=$JAVA_HOME"
      shopt -u nullglob
      return 0
    fi
  done
  shopt -u nullglob
  echo "Error: no JDK found for Gradle (need JDK 17+)." >&2
  echo "  export JAVA_HOME=\"/Applications/Android Studio.app/Contents/jbr/Contents/Home\"" >&2
  echo "  or: brew install --cask temurin@17" >&2
  exit 1
}

APK=""

if [[ "$DO_APK" == true ]]; then
  # Init the Gradle project after build.sh clean (which may have wiped CLIENT_ROOT).
  if [[ ! -f "$CLIENT_ROOT/gradlew" ]]; then
    init_android_client "$CLIENT_ROOT"
  fi
  echo "sdk.dir=$ANDROID_HOME" > "$CLIENT_ROOT/local.properties"
  ensure_java_for_gradle
  echo "[package_embedded_apk] ./gradlew assembleDebug"
  (cd "$CLIENT_ROOT" && ./gradlew assembleDebug)

  # Find the raw APK produced by Gradle
  _apk_dir="$CLIENT_ROOT/app/build/outputs/apk/debug"
  shopt -s nullglob
  _raw_apks=( "$_apk_dir"/*.apk )
  shopt -u nullglob
  if [[ ${#_raw_apks[@]} -eq 0 ]]; then
    echo "Error: no APK found under $_apk_dir" >&2
    exit 1
  fi
  _raw_apk="$(ls -t "${_raw_apks[@]}" | head -1)"

  # Rename and move to the caller's working directory, same as rpm/seekdb-build.sh
  APK="$CURDIR/$PROJECT_NAME-$VERSION-$RELEASE.apk"
  mv "$_raw_apk" "$APK"
  echo "[package_embedded_apk] APK: $APK ($(du -h "$APK" | cut -f1))"
fi

if [[ "$DO_INSTALL" == true ]]; then
  if [[ -z "$APK" || ! -f "$APK" ]]; then
    echo "Error: no APK available; run with --apk first" >&2
    exit 1
  fi
  echo "[package_embedded_apk] adb install -r $APK"
  adb install -r "$APK"
fi

echo "[package_embedded_apk] done"

