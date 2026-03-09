#!/bin/bash
# Download and install pre-built Android NDK dependencies and host devtools.
# Follows the same download/extract pattern as dep_create.sh:
#   - Downloads to deps/3rd/pkg/ (cached)
#   - Extracts with tar -xzf --strip-components=1 into deps/3rd/

SCRIPT_DIR="$(cd "$(dirname "$0")" && pwd)"
DEP_FILE="$SCRIPT_DIR/oceanbase.android.arm64.deps"
TARGET_DIR="$SCRIPT_DIR/../3rd"
mkdir -p "$TARGET_DIR"
TARGET_DIR="$(cd "$TARGET_DIR" && pwd)"
OS_TYPE="$(uname -s)"

function echo_log() {
    echo -e "[dep_create.sh] $@"
}

function echo_err() {
    echo -e "[dep_create.sh][ERROR] $@" 1>&2
}

if [[ ! -f "$DEP_FILE" ]]; then
    echo_err "check dependencies profile for ${DEP_FILE}... NOT FOUND"
    exit 2
fi
echo_log "check dependencies profile for ${DEP_FILE}... FOUND"

# --- Check if already initialized (same mechanism as dep_create.sh) ---
if command -v md5sum >/dev/null 2>&1; then
    MD5=$(md5sum "$DEP_FILE" | cut -d" " -f1)
else
    MD5=$(md5 -r "$DEP_FILE" | cut -d" " -f1)
fi

if [[ -f "${TARGET_DIR}/${MD5}" && -f "${TARGET_DIR}/DONE" ]]; then
    echo_log "dependencies already initialized (${MD5}), skipping"
    exit 0
fi

# --- Parse deps file ---
REPO_URL=""
DEPS_PKGS=()
TOOLS_PKGS=()
current_section=""

while IFS= read -r line; do
    [[ -z "$line" || "$line" == \#* ]] && continue
    if [[ "$line" =~ ^\[(.+)\]$ ]]; then
        current_section="${BASH_REMATCH[1]}"
        continue
    fi
    if [[ "$current_section" == "target-default" && "$line" =~ ^repo= ]]; then
        REPO_URL="${line#repo=}"
        continue
    fi
    if [[ "$current_section" == "deps" ]]; then
        DEPS_PKGS+=("$line")
    elif [[ "$current_section" == "tools" ]]; then
        TOOLS_PKGS+=("$line")
    fi
done < "$DEP_FILE"

if [[ -z "$REPO_URL" ]]; then
    echo_err "no repo URL found in $DEP_FILE"
    exit 1
fi

echo_log "check repository address in profile... $REPO_URL"

# --- Helper: download a package to cache ---
mkdir -p "${TARGET_DIR}/pkg"

function download_pkg() {
    local pkg="$1"
    if [[ -f "${TARGET_DIR}/pkg/${pkg}" ]]; then
        echo_log "find package <${pkg}> in cache"
        return 0
    fi
    echo_log "downloading package <${pkg}>"
    if [[ "$OS_TYPE" == "Darwin" ]]; then
        TEMP=$(mktemp "${TARGET_DIR}/pkg/.${pkg}.XXXX")
        curl -L -f -s "$REPO_URL/${pkg}" -o "${TEMP}" > ${TARGET_DIR}/pkg/error.log 2>&1
    else
        TEMP=$(mktemp -p "/" -u ".${pkg}.XXXX")
        wget "$REPO_URL/${pkg}" -O "${TARGET_DIR}/pkg/${TEMP}" &> ${TARGET_DIR}/pkg/error.log
    fi
    if (( $? == 0 )); then
        if [[ "$OS_TYPE" == "Darwin" ]]; then
            mv -f "${TEMP}" "${TARGET_DIR}/pkg/${pkg}"
        else
            mv -f "${TARGET_DIR}/pkg/$TEMP" "${TARGET_DIR}/pkg/${pkg}"
        fi
        rm -rf ${TARGET_DIR}/pkg/error.log
    else
        cat ${TARGET_DIR}/pkg/error.log
        if [[ "$OS_TYPE" == "Darwin" ]]; then
            rm -rf "${TEMP}"
        else
            rm -rf "${TARGET_DIR}/pkg/$TEMP"
        fi
        echo_err "failed to download $REPO_URL/${pkg}"
        exit 4
    fi
}

# --- Install NDK dependencies ---
echo_log "start to download dependencies..."

for pkg in "${DEPS_PKGS[@]}"; do
    download_pkg "$pkg"
    echo_log "unpack package <${pkg}>... \c"
    (cd ${TARGET_DIR} && tar -xzf "${TARGET_DIR}/pkg/${pkg}" --strip-components=1)
    if [[ $? -eq 0 ]]; then
        echo "SUCCESS"
    else
        echo "FAILED" 1>&2
        echo_err "failed to extract ${pkg}"
        exit 5
    fi
done

echo_log "installed ${#DEPS_PKGS[@]} dependency packages"

# --- Install host devtools (bison, flex) ---
if [[ ${#TOOLS_PKGS[@]} -gt 0 ]]; then
    DEVTOOLS_DIR="${TARGET_DIR}/usr/local/oceanbase/devtools"
    mkdir -p "$DEVTOOLS_DIR"
    echo_log "installing ${#TOOLS_PKGS[@]} host devtools..."

    for pkg in "${TOOLS_PKGS[@]}"; do
        download_pkg "$pkg"
        echo_log "unpack package <${pkg}>... \c"
        tar -xzf "${TARGET_DIR}/pkg/${pkg}" --strip-components=1 -C "$TARGET_DIR"
        if [[ $? -eq 0 ]]; then
            echo "SUCCESS"
        else
            echo "FAILED" 1>&2
            echo_err "failed to extract ${pkg}"
            exit 5
        fi
    done

    echo_log "installed ${#TOOLS_PKGS[@]} host devtools to $DEVTOOLS_DIR"
fi

# --- Mark as done ---
touch "${TARGET_DIR}/${MD5}"
touch "${TARGET_DIR}/DONE"
echo_log "initialization complete"
