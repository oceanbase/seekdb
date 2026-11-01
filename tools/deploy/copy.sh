#!/bin/bash -x
SOURCE_DIR=$(readlink -f "$(dirname ${BASH_SOURCE[0]})/../..")

if [ $# -lt 1 ]
then
  BUILD_DIR=$(find $SOURCE_DIR -maxdepth 1 -name 'build_*' -type d | grep -v 'build_ccls'  | head -1)
  if [[ "$BUILD_DIR" == "" ]]
  then
    echo "Usage ./copy.sh [oceanbase_dev_dir]"
  else
    echo "Choose $BUILD_DIR as build directory of oceanbase."
  fi
else
  BUILD_DIR=$1
fi

BIN_DIR=`pwd`/bin${VER}
LIB_DIR=`pwd`/lib
TOOL_DIR=`pwd`/tools
ETC_DIR=`pwd`/etc
DEBUG_DIR=`pwd`/debug
ADMIN_DIR=`pwd`/admin

function do_install {
  quiet=false
  if [ $# -eq 3 ] && [[ "$3" == "true" ]]
  then
    quiet=true
  fi
  [[ "$quiet" == "false" ]] && echo -n "Installing $1 "
  sources=$(ls $1 2>/dev/null)
  if [[ "$sources" == "" ]]
  then
    [[ "$quiet" == "false" ]] && echo -e "\033[0;31mFAIL\033[0m\nNo such file: $1"
    if [ "$quiet" == "false" ]
    then
      return 1
    else
      return 0
    fi
  fi
  target=$2
  if [[ "$(uname -s)" == "Darwin" ]]; then
    err_msg=$(cp -f $sources $target 2>&1 >/dev/null)
  else
    err_msg=$(libtool --mode=install cp $sources $target 2>&1 >/dev/null)
  fi
  if [ $? -eq 0 ]
  then
    [[ "$quiet" == "false" ]] && echo -e "\033[0;32mOK\033[0m"
  else
    [[ "$quiet" == "false" ]] && echo -e "\033[0;31mFAIL\033[0m\n$err_msg"
    return 1
  fi
}

if [ $# -lt 2 ]
then
  mkdir -p $BIN_DIR
  mkdir -p $LIB_DIR
  mkdir -p $TOOL_DIR
  mkdir -p $ETC_DIR
  mkdir -p $DEBUG_DIR
  mkdir -p $ADMIN_DIR

  do_install $BUILD_DIR/src/observer/seekdb $BIN_DIR/seekdb || exit 1
  # A disabled reconfiguration can leave a worker from an earlier GPU build.
  # Prefer the current configuration, and require both GPU files when enabled.
  hipvs_build=false
  if [[ -f "$BUILD_DIR/CMakeCache.txt" ]]; then
    hipvs_option=$(sed -n 's/^OB_BUILD_HIPVS:[^=]*=//p' "$BUILD_DIR/CMakeCache.txt" | tr '[:lower:]' '[:upper:]')
    if [[ "$hipvs_option" == "" ]]; then
      hipvs_option=$(sed -n 's/^OB_BUILD_CUVS:[^=]*=//p' "$BUILD_DIR/CMakeCache.txt" | tr '[:lower:]' '[:upper:]')
    fi
    case "$hipvs_option" in
      ""|0|OFF|NO|FALSE|N|IGNORE|NOTFOUND|*-NOTFOUND) ;;
      *) hipvs_build=true ;;
    esac
  elif [[ -f "$BUILD_DIR/src/observer/seekdb_hipvs_hnsw_worker" ]]; then
    hipvs_build=true
  fi
  if [[ "$hipvs_build" == "true" ]]; then
    do_install "$BUILD_DIR/src/observer/seekdb_hipvs_hnsw_worker" "$BIN_DIR/" || exit 1
    do_install "$BUILD_DIR/lib/libseekdb_hipvs_hnsw.so" "$LIB_DIR/" || exit 1
  fi
  do_install "$BUILD_DIR/syspack_release/*" $ADMIN_DIR
  do_install $SOURCE_DIR/deps/3rd/usr/local/oceanbase/devtools/bin/llvm-symbolizer $TOOL_DIR/
  do_install $SOURCE_DIR/rpm/.dep_create/lib/libstdc++.so.6 $LIB_DIR true
  do_install "$SOURCE_DIR/tools/timezone_V1.log" $ETC_DIR
  do_install $SOURCE_DIR/deps/oblib/src/lib/profile/obperf $TOOL_DIR/ true
  do_install $SOURCE_DIR/deps/3rd/home/admin/oceanbase/bin/obshell $BIN_DIR/obshell true
  do_install "$SOURCE_DIR/tools/default_srs_data_mysql.sql" $ETC_DIR
fi
