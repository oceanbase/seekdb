#!/bin/bash

#clear env
unalias -a

PWD="$(cd $(dirname $0); pwd)"

OS_ARCH="$(uname -m)" || exit 1
OS_RELEASE="0"
AL3_RELEASE="0"
OS_TYPE="$(uname -s)" || exit 1

# Use a more reliable method to detect architecture on macOS
# because uname -m incorrectly returns x86_64 when running under Rosetta 2
if [[ "${OS_TYPE}" == "Darwin" ]]; then
  # Use sysctl to detect actual hardware architecture, unaffected by Rosetta
  if sysctl -n hw.optional.arm64 2>/dev/null | grep -q '1'; then
    OS_ARCH="arm64"
  else
    OS_ARCH="x86_64"
  fi
fi

# macOS detection
if [[ "${OS_TYPE}" == "Darwin" ]]; then
  # macOS does not need /etc/os-release
  OS_RELEASE="macos"
  PNAME="macOS $(sw_vers -productVersion) (${OS_ARCH})"
else
  if [[ ! -f /etc/os-release ]]; then
    echo "[ERROR] os release info not found" 1>&2 && exit 1
  fi
  source /etc/os-release || exit 1
  PNAME=${PRETTY_NAME:-"${NAME} ${VERSION}"}
  PNAME="${PNAME} (${OS_ARCH})"
fi

function compat_centos9() {
  echo_log "[NOTICE] '$PNAME' is compatible with CentOS 9, use el9 dependencies list"
  OS_RELEASE=9
}

function compat_centos8() {
  echo_log "[NOTICE] '$PNAME' is compatible with CentOS 8, use el8 dependencies list"
  OS_RELEASE=8
}

function compat_centos7() {
  echo_log "[NOTICE] '$PNAME' is compatible with CentOS 7, use el7 dependencies list"
  OS_RELEASE=7
}

function compat_alinux3() {
  echo_log "[NOTICE] '$PNAME' is compatible with Alinux3, use al8 dependencies list"
  AL3_RELEASE="1"
  OS_RELEASE=8
}

function not_supported() {
  echo_log "[ERROR] '$PNAME' is not supported yet."
}

function version_ge() {
  test "$(awk -v v1=$VERSION_ID -v v2=$1 'BEGIN{print(v1>=v2)?"1":"0"}' 2>/dev/null)" == "1"
}

function echo_log() {
  echo -e "[dep_create.sh] $@"
}

function echo_err() {
  echo -e "[dep_create.sh][ERROR] $@" 1>&2
}

function get_os_release() {
  if [[ "${OS_TYPE}" == "Darwin" ]]; then
    if [[ "${OS_ARCH}x" == "x86_64x" || "${OS_ARCH}x" == "arm64x" ]]; then
      OS_RELEASE="macos"
      echo_log "[NOTICE] 'macOS (${OS_ARCH})' detected, use macos dependencies list"
      return 0
    fi
  fi

  if [[ "${OS_ARCH}x" == "x86_64x" ]]; then
    case "$ID" in
      rhel)
        version_ge "9.0" && compat_centos9 && return
        version_ge "8.0" && compat_centos8 && return
        version_ge "7.0" && compat_centos7 && return
        ;;
      alinux)
        version_ge "3.0" && compat_alinux3 && return
        version_ge "2.1903" && compat_centos7 && return
        ;;
      alios)
        version_ge "8.0" && compat_centos8 && return
        version_ge "7.2" && compat_centos7 && return
        ;;
      anolis)
        version_ge "23.0" && compat_centos9 && return
        version_ge "8.0" && compat_centos8 && return
        version_ge "7.0" && compat_centos7 && return
        ;;
      ubuntu)
        version_ge "22.04" && compat_centos9 && return
        version_ge "16.04" && compat_centos7 && return
        ;;
      centos)
        version_ge "9.0" && OS_RELEASE=9 && return
        version_ge "8.0" && OS_RELEASE=8 && return
        version_ge "7.0" && OS_RELEASE=7 && return
        ;;
      almalinux)
        version_ge "9.0" && compat_centos9 && return
        version_ge "8.0" && compat_centos8 && return
        ;;
      debian)
        version_ge "12" && compat_centos9 && return
        version_ge "9" && compat_centos7 && return
        ;;
      fedora)
        version_ge "33" && compat_centos7 && return
        ;;
      kylin)
        version_ge "V10" && compat_centos8 && return
        ;;
      openEuler)
        version_ge "22" && compat_centos9 && return
        ;;
      opensuse-leap)
        version_ge "15" && compat_centos7 && return
        ;;
      #suse
      sles)
        version_ge "15" && compat_centos7 && return
        ;;
      uos)
        version_ge "20" && compat_centos7 && return
        ;;
      arch | garuda)
        compat_centos8 && return
        ;;
      rocky)
        version_ge "9.0" && compat_centos9 && return
        version_ge "8.0" && compat_centos8 && return
        ;;
      tencentos)
        version_ge "3.1" && compat_centos8 && return
        ;;
    esac
  elif [[ "${OS_ARCH}x" == "aarch64x" ]]; then
    case "$ID" in
      rhel)
        version_ge "9.0" && compat_centos9 && return
        version_ge "8.0" && compat_centos8 && return
        version_ge "7.0" && compat_centos7 && return
        ;;
      alios)
        version_ge "8.0" && compat_centos8 && return
        version_ge "7.0" && compat_centos7 && return
        ;;
      anolis)
        version_ge "23.0" && compat_centos9 && return
        version_ge "8.0" && compat_centos8 && return
        version_ge "7.0" && compat_centos7 && return
        ;;
      centos)
        version_ge "9.0" && OS_RELEASE=9 && return
        version_ge "8.0" && OS_RELEASE=8 && return
        version_ge "7.0" && OS_RELEASE=7 && return
        ;;
      almalinux)
        version_ge "9.0" && compat_centos9 && return
        version_ge "8.0" && compat_centos8 && return
        ;;
      debian)
        version_ge "12" && compat_centos9 && return
        version_ge "9" && compat_centos7 && return
        ;;
      kylin)
        version_ge "V10" && compat_centos8 && return
        ;;
      openEuler)
        version_ge "22" && compat_centos9 && return
        ;;
      ubuntu)
        version_ge "22.04" && compat_centos9 && return
        version_ge "16.04" && compat_centos7 && return
        ;;
      alinux)
        version_ge "3.0" && compat_alinux3 && return
        ;;
      rocky)
        version_ge "9.0" && compat_centos9 && return
        version_ge "8.0" && compat_centos8 && return
        ;;
    esac
  elif [[ "${OS_ARCH}x" == "sw_64x" ]]; then
    case "$ID" in
      UOS)
	version_ge "20" && OS_RELEASE=20 && return
      ;;
    esac
  fi
  not_supported && return 1
}

get_os_release || exit 1

if [[ "${OS_RELEASE}x" == "macosx" ]]; then
    MACOS_VERSION=$(sw_vers -productVersion | awk -F. '{print $1}')
    if [ $MACOS_VERSION -lt 13 ]; then
      not_supported && exit 1
    elif [ $MACOS_VERSION -lt 15 ]; then
      OS_TAG="macos13.$OS_ARCH"
    else
      OS_TAG="macos15.$OS_ARCH"
    fi
elif [[ "${AL3_RELEASE}x" == "1x" ]]; then
    OS_TAG="al$OS_RELEASE.$OS_ARCH"
else
    OS_TAG="el$OS_RELEASE.$OS_ARCH"
fi

DEP_FILE="oceanbase.${OS_TAG}.deps"
# Compatible with MD5 checksum for macOS and Linux
if command -v md5sum >/dev/null 2>&1; then
    MD5=`md5sum ${DEP_FILE} | cut -d" " -f1`
else
    MD5=`md5 -r ${DEP_FILE} | cut -d" " -f1`
fi

# Whether to use shared dependency cache, default is ON, will be OFF under certain conditions
NEED_SHARE_CACHE=ON

WORKSACPE_DEPS_DIR="$(cd $(dirname $0); cd ..; pwd)"
WORKSPACE_DEPS_3RD=${WORKSACPE_DEPS_DIR}/3rd
WORKSAPCE_DEPS_3RD_DONE=${WORKSPACE_DEPS_3RD}/DONE
WORKSAPCE_DEPS_3RD_MD5=${WORKSPACE_DEPS_3RD}/${MD5}

# Check if local dependency directory exists
if [ -f ${WORKSAPCE_DEPS_3RD_MD5} ]; then
    if [ -f "${WORKSAPCE_DEPS_3RD_DONE}" ]; then
        echo_log "${DEP_FILE} has been initialized due to ${WORKSAPCE_DEPS_3RD_MD5}, ${WORKSAPCE_DEPS_3RD_DONE} exists"
        exit 0
    else
        echo_log "${DEP_FILE} has been not initialized, due to ${WORKSAPCE_DEPS_3RD_DONE} not exists"
    fi
else
    echo_log "${DEP_FILE} has been not initialized, due to ${WORKSAPCE_DEPS_3RD_MD5} not exists"
fi

# Dependency directory does not exist, disable cache
if [ "x${DEP_CACHE_DIR}" == "x" ]; then
    NEED_SHARE_CACHE=OFF
    echo_log "disable share dep cache due to env DEP_CACHE_DIR not set"
else
  if [ -d ${DEP_CACHE_DIR} ]; then
    echo_log "FOUND env DEP_CACHE_DIR: ${DEP_CACHE_DIR}"
  else
      NEED_SHARE_CACHE=OFF
      echo_log "disable share dep cache due to not exist env FOUND DEP_CACHE_DIR(${DEP_CACHE_DIR})"
  fi
fi

# Determine shared dependency cache directory
CACHE_DEPS_DIR=${DEP_CACHE_DIR}/${MD5}
CACHE_DEPS_DIR_3RD=${DEP_CACHE_DIR}/${MD5}/3rd
CACHE_DEPS_DIR_3RD_DONE=${DEP_CACHE_DIR}/${MD5}/3rd/DONE
CACHE_DEPS_LOCKFILE=${DEP_CACHE_DIR}/${MD5}.lockfile

# Determine temporary directory path; if cache is disabled, this will also be the actual directory path
# UUID generation compatible with both macOS and Linux
if [[ "${OS_TYPE}" == "Darwin" ]]; then
  UUID=`uuidgen`
else
  UUID=`cat /proc/sys/kernel/random/uuid`
fi
TARGET_DIR=${DEP_CACHE_DIR}/${MD5}.${UUID}
TARGET_DIR_3RD=${DEP_CACHE_DIR}/${MD5}.${UUID}/3rd

# Environment variable to disable shared dependency cache
if [ "x${DISABLE_SHARE_DEP_CACHE}" == "x1" ]; then
    NEED_SHARE_CACHE=OFF
    echo_log "disable share deps cache due to env DISABLE_SHARE_DEP_CACHE=1"
fi

if [ $NEED_SHARE_CACHE == "OFF" ]; then
    # Not sharing cache, download directly to current workspace directory
    TARGET_DIR_3RD=${WORKSPACE_DEPS_3RD}
fi

# Remove local dependency files
rm -rf ${WORKSPACE_DEPS_3RD}

if [ ${NEED_SHARE_CACHE} == "ON" ]; then
    # Check if shared directory exists
    if [ -f ${CACHE_DEPS_DIR_3RD_DONE} ]; then
        echo_log "use cache deps ${WORKSPACE_DEPS_3RD} -> ${CACHE_DEPS_DIR_3RD}"
        ln -sf ${CACHE_DEPS_DIR_3RD} ${WORKSPACE_DEPS_3RD}
        exit $?
    else
        echo_log "cache deps ${CACHE_DEPS_DIR_3RD_DONE} not exist"
    fi
fi

if [[ ! -f "${DEP_FILE}" ]]; then
    echo_err "check dependencies profile for ${DEP_FILE}... NOT FOUND"
    exit 2
else
    echo_log "check dependencies profile for ${DEP_FILE}... FOUND"
fi

# Use regular arrays for compatibility with bash 3.x (macOS default version)
# targets use two regular arrays: target name and repo URL
targets_sections=()
targets_repos=()

# packages use two regular arrays: section name and content
package_sections=()
package_contents=()

section="default"
content=""

function save_content {
    if [[ "$content" != "" ]]
    then
        if [[ $(echo "$section" | grep -E "^target\-") != "" ]]
        then
            target_name=$(echo $section | sed 's|^target\-\(.*\)$|\1|g')
            repo=$(echo "${content}" | grep -Eo "repo=.*" | awk -F '=' '{ print $2 }')
            targets_sections+=("$target_name")
            targets_repos+=("$repo")
            echo_log "target: $target_name, repo: $repo"
        else
            package_sections+=("$section")
            package_contents+=("$content")
        fi
    fi
}
echo_log "check repository address in profile..."

while read -r line
do
    if [[ $(echo "$line" | grep -E "\[.*\]") != "" ]]
    then
        save_content
        content=""
        # section=${line//\[\(.*\)\]/\1}
        section=$(echo $line | sed 's|.*\[\(.*\)\].*|\1|g')
    else
        [[ "$line" != "" ]] && [[ "$line" != '#'* ]] && content+=$'\n'"$line"
    fi
done < $DEP_FILE
save_content

# Start downloading dependencies
echo_log "start to download dependencies..."
if [[ ${#package_sections[@]} -eq 0 ]]; then
    echo_err "ERROR: packages array is empty! Cannot proceed."
    exit 1
fi
mkdir -p "${TARGET_DIR_3RD}/pkg"

# Iterate through all sections
for i in $(seq 0 $((${#package_sections[@]} - 1)))
do
    sect="${package_sections[$i]}"
    sect_content="${package_contents[$i]}"
    while read -r line
    do
        [[ "$line" == "" ]] && continue
        pkg=${line%%\ *}
        target_name="default"
        temp=$(echo "$line" | grep -Eo "target=(\S*)")

        [[ "$temp" != "" ]] && target_name=${temp#*=}
       
	# Adapt to OB artifact repository download URL
        pkg_path=${pkg}
        # 在 targets_sections 中查找对应的 repo
        repo=""
        for j in $(seq 0 $((${#targets_sections[@]} - 1))); do
            if [[ "${targets_sections[$j]}" == "$target_name" ]]; then
                repo="${targets_repos[$j]}"
                break
            fi
        done
        # If not found, use default value
        if [[ -z "$repo" ]]; then
            # Try to get from target-default
            for j in $(seq 0 $((${#targets_sections[@]} - 1))); do
                if [[ "${targets_sections[$j]}" == "default" ]]; then
                    repo="${targets_repos[$j]}"
                    break
                fi
            done
        fi
        if [[ "${AL3_RELEASE}" == "1" && ("${target_name}" == "default" || "${target_name}" == "test") && "$repo" != *"mirror"* ]]; then
            pkg_version=${pkg%.al8.${OS_ARCH}.rpm}
            pkg_name=$(echo "$pkg_version" | sed 's/\(.*\)-.*-.*/\1/')
            pkg_path=${pkg_name}/${pkg}
        fi

	      if [[ -f "${TARGET_DIR_3RD}/pkg/${pkg}" ]]; then
            echo_log "find package <${pkg}> in cache"
        else
            echo_log "downloading package <${pkg}>"
            # macOS uses different temp file creation method and download command
            if [[ "${OS_RELEASE}x" == "macosx" ]]; then
              TEMP=$(mktemp "${TARGET_DIR_3RD}/pkg/.${pkg}.XXXX")
              if command -v curl >/dev/null 2>&1; then
                curl -L -f -s "$repo/${pkg_path}" -o "${TEMP}" > ${TARGET_DIR_3RD}/pkg/error.log 2>&1
              else
                echo_err "curl command not found, please install curl"
                exit 4
              fi
            else
              TEMP=$(mktemp -p "/" -u ".${pkg}.XXXX")
              wget "$repo/${pkg_path}" -O "${TARGET_DIR_3RD}/pkg/${TEMP}" &> ${TARGET_DIR_3RD}/pkg/error.log
            fi
            if (( $? == 0 )); then
                if [[ "${OS_RELEASE}x" == "macosx" ]]; then
                  mv -f "${TEMP}" "${TARGET_DIR_3RD}/pkg/${pkg}"
                else
                  mv -f "${TARGET_DIR_3RD}/pkg/$TEMP" "${TARGET_DIR_3RD}/pkg/${pkg}"
                fi
                rm -rf ${TARGET_DIR_3RD}/pkg/error.log
            else
                cat ${TARGET_DIR_3RD}/pkg/error.log
                if [[ "${OS_RELEASE}x" == "macosx" ]]; then
                  rm -rf "${TEMP}"
                  echo_err "curl $repo/${pkg_path}"
                  echo_err "Failed to init macos deps"
                else
                  rm -rf "${TARGET_DIR_3RD}/pkg/$TEMP"
                  echo_err "wget $repo/${pkg_path}"
                  echo_err "Failed to init rpm deps"
                fi
                exit 4
            fi
        fi
        echo_log "unpack package <${pkg}>... \c"
        # macOS uses tar.gz extraction
        if [[ "${OS_RELEASE}x" == "macosx" ]]; then
          (cd ${TARGET_DIR_3RD} && tar -xzf "${TARGET_DIR_3RD}/pkg/${pkg}" --strip-components=1)
        elif [[ "$ID" = "arch" || "$ID" = "garuda" ]]; then
          (cd ${TARGET_DIR_3RD} && rpmextract.sh "${TARGET_DIR_3RD}/pkg/${pkg}")
        else
          (cd ${TARGET_DIR_3RD} && rpm2cpio "${TARGET_DIR_3RD}/pkg/${pkg}" | cpio -di -u --quiet)
        fi
        if [[ $? -eq 0 ]]; then
          echo "SUCCESS"
        else
          echo "FAILED" 1>&2
          if [[ "${OS_RELEASE}x" == "macosx" ]]; then
            echo_log "[ERROR] Failed to init macos deps"
          else
            echo_log "[ERROR] Failed to init rpm deps"
          fi
          exit 5
        fi
    done <<< "$sect_content"
done

# Skip caching and exit directly
if [ ${NEED_SHARE_CACHE} == "OFF" ]; then
    touch ${WORKSAPCE_DEPS_3RD_MD5}
    touch ${WORKSAPCE_DEPS_3RD_DONE}
    exit $?
fi

# Link to cache directory
LINK_CHACE_DIRECT=OFF
# Link to current target directory
LINK_TARGET_DIRECT=OFF

# After download, if target already exists, create symlink directly
if [ -d ${CACHE_DEPS_DIR} ]; then
    echo_log "found ${CACHE_DEPS_DIR} exists"
    if [ -f ${CACHE_DEPS_DIR_3RD_DONE} ]; then
        echo_log "found ${CACHE_DEPS_DIR_3RD_DONE} exists"
        LINK_CHACE_DIRECT=ON
    else
        echo_log "not found ${CACHE_DEPS_DIR_3RD_DONE} exists"
        LINK_TARGET_DIRECT=ON
    fi
fi

if [ -f ${CACHE_DEPS_LOCKFILE} ];then
    # Lock files older than one minute are considered stale, cache will be reinitialized
    echo_log "found lock file ${CACHE_DEPS_LOCKFILE}"
    LINK_TARGET_DIRECT=ON
    if test `find "${CACHE_DEPS_LOCKFILE}" -mmin +1`; then
        echo_log "lock file ${CACHE_DEPS_LOCKFILE} escape 1 mins, and try to unlock"
        rm -rf ${CACHE_DEPS_LOCKFILE}
        if [ $? -eq 0 ]; then
            LINK_TARGET_DIRECT=OFF
            echo_log "unlock success"
        else
            echo_log "failed to unlock"
        fi
    else
        echo_log "lock file ${CACHE_DEPS_LOCKFILE} in 1 mins"
    fi
fi

if [ ${LINK_CHACE_DIRECT}  == "ON" ]; then
    echo_log "give up current file and link direct, ${WORKSPACE_DEPS_3RD} -> ${CACHE_DEPS_DIR_3RD}"
    rm -rf ${TARGET_DIR}
    echo_log "link deps  ${WORKSPACE_DEPS_3RD} -> ${CACHE_DEPS_DIR_3RD}"
    ln -sf ${CACHE_DEPS_DIR_3RD} ${WORKSPACE_DEPS_3RD}
    exit $?
fi

if [ ${LINK_TARGET_DIRECT} == "ON" ]; then
    # Skip caching, create symlink directly to this directory
    echo_log "give up mv and link dirct, ${WORKSPACE_DEPS_3RD} -> ${TARGET_DIR_3RD}"
    ln -sf ${TARGET_DIR_3RD} ${WORKSPACE_DEPS_3RD}
    if [ $? -ne 0 ]; then
        echo_err "Failed to link ${WORKSPACE_DEPS_3RD} to ${TARGET_DIR_3RD}"
        exit 1
    fi
    touch ${WORKSAPCE_DEPS_3RD_MD5}
    touch ${WORKSAPCE_DEPS_3RD_DONE}
    exit $?
fi

touch ${CACHE_DEPS_LOCKFILE}
chmod 777 ${CACHE_DEPS_LOCKFILE}
echo_log "generate lock file ${CACHE_DEPS_LOCKFILE}"

mv ${TARGET_DIR} ${CACHE_DEPS_DIR}
if [ $? -ne 0 ]; then
    echo_err "Failed to mv ${TARGET_DIR} to ${CACHE_DEPS_DIR}"
    exit 1
fi

ln -sf ${CACHE_DEPS_DIR_3RD} ${WORKSPACE_DEPS_3RD}
if [ $? -ne 0 ]; then
    echo_err "Failed to link ${CACHE_DEPS_DIR_3RD} to ${WORKSPACE_DEPS_3RD}"
    exit 1
fi

rm -rf ${CACHE_DEPS_LOCKFILE}
echo_log "unlock lock file ${CACHE_DEPS_LOCKFILE}"

echo_log "link deps ${WORKSPACE_DEPS_3RD} -> ${CACHE_DEPS_DIR_3RD}"

# Mark md5 and done files
touch ${WORKSAPCE_DEPS_3RD_MD5}
touch ${WORKSAPCE_DEPS_3RD_DONE}
