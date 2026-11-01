#!/bin/bash

ONLY_LOCAL_COPY=0
COMPILE_LOCAL_COPY=0
COMPILE_CONCURRENCY=100
CASE_NAME=
echo "Usage: ./copy.sh [-l] [oceanbase_dev_dir]"
echo "Usage: -l means only copy local bin, ship copy  8877 files"
echo "Usage: -n name means compile, copy and run case name, ship copy  8877 files"
echo "Usage: -j set compile concurrency"

while getopts "lc:n:" optname; do
  case "$optname" in
    l)
      echo "only copy local bins"
      ONLY_LOCAL_COPY=1
      ;;
    c)
      echo "compile and copy local bins"
      ONLY_LOCAL_COPY=1
      COMPILE_LOCAL_COPY=1
      COMPILE_CONCURRENCY="$OPTARG"
      ;;
    n)
      CASE_NAME="$OPTARG"
      ONLY_LOCAL_COPY=1
      COMPILE_LOCAL_COPY=1
      echo "run case $CASE_NAME"
      ;;
    *)
      # Should not occur
      echo "Unknown error while processing options"
      exit 1;
      ;;
  esac
done
shift $((OPTIND-1))

if [ $# -lt 1 ]
then
  OCEANBASE_DIR=`pwd`/../../
else
  OCEANBASE_DIR=$1
fi

FARM_BIN_DIR=$_CONDOR_JOB_IWD
BIN_DIR=`pwd`/bin${VER}
LIB_DIR=`pwd`/lib
ETC_DIR=`pwd`/etc
ADMIN_DIR=`pwd`/admin
TARGET_AGENT_DIR=`pwd`/agent
SOURCE_BACKUP_DIR=$OCEANBASE_DIR/tools/agentserver
SOURCE_RESTORE_DIR=$OCEANBASE_DIR/tools/ObRestore



if [ $# -lt 2 ]
then
  mkdir -p "$BIN_DIR" "$LIB_DIR" "$ADMIN_DIR" "$ETC_DIR" "$TARGET_AGENT_DIR" || exit $?
  #for system package
  cp "$OCEANBASE_DIR"/src/share/inner_table/sys_package/* "$ADMIN_DIR" || exit $?

  OBSERVER_BIN="${SEEKDB_BUILD_DIR:-}"
  if [[ -z "$OBSERVER_BIN" ]]; then
    # Honor the source directory argument and prefer maintained profiles over
    # an obsolete build_debug directory left by an earlier checkout.
    for profile in release sanity debug release_coverage errsim errsim_debug errsim_sanity; do
      if [[ -d "$OCEANBASE_DIR/build_$profile" ]]; then
        OBSERVER_BIN="$OCEANBASE_DIR/build_$profile"
        break
      fi
    done
  fi
  if [[ -z "$OBSERVER_BIN" || ! -d "$OBSERVER_BIN" ]]; then
    echo "No build directory found; set SEEKDB_BUILD_DIR or build release first" >&2
    exit 1
  fi

  if [ $COMPILE_LOCAL_COPY -eq 1 ]; then
    (cd "$OBSERVER_BIN" && ob-make -j "$COMPILE_CONCURRENCY" seekdb) || exit $?
  fi

  if [[ ! -x "$OBSERVER_BIN/src/observer/seekdb" ]]; then
    echo "Missing executable: $OBSERVER_BIN/src/observer/seekdb" >&2
    exit 1
  fi
  hipvs_build=false
  if [[ -f "$OBSERVER_BIN/CMakeCache.txt" ]]; then
    hipvs_option=$(sed -n 's/^OB_BUILD_HIPVS:[^=]*=//p' "$OBSERVER_BIN/CMakeCache.txt" | tr '[:lower:]' '[:upper:]')
    if [[ -z "$hipvs_option" ]]; then
      hipvs_option=$(sed -n 's/^OB_BUILD_CUVS:[^=]*=//p' "$OBSERVER_BIN/CMakeCache.txt" | tr '[:lower:]' '[:upper:]')
    fi
    case "$hipvs_option" in
      ""|0|OFF|NO|FALSE|N|IGNORE|NOTFOUND|*-NOTFOUND) ;;
      *) hipvs_build=true ;;
    esac
  elif [[ -f "$OBSERVER_BIN/src/observer/seekdb_hipvs_hnsw_worker" ]]; then
    hipvs_build=true
  fi
  if [[ "$hipvs_build" == true ]]; then
    if [[ ! -x "$OBSERVER_BIN/src/observer/seekdb_hipvs_hnsw_worker" ||
          ! -s "$OBSERVER_BIN/lib/libseekdb_hipvs_hnsw.so" ]]; then
      echo "Incomplete GPU build: worker or bridge missing in $OBSERVER_BIN" >&2
      exit 1
    fi
  fi
  libtool --mode=install cp "$OBSERVER_BIN/src/observer/seekdb" "$BIN_DIR/observer" || exit $?
  if [[ "$hipvs_build" == true ]]; then
    libtool --mode=install cp "$OBSERVER_BIN/src/observer/seekdb_hipvs_hnsw_worker" "$BIN_DIR/" || exit $?
    libtool --mode=install cp "$OBSERVER_BIN/lib/libseekdb_hipvs_hnsw.so" "$LIB_DIR/" || exit $?
  fi
fi

if [ $ONLY_LOCAL_COPY -eq 0 ]; then
  rm -rf bin/dict*
  wget -a dict.wget.log "http://11.166.86.153:8877/dict.tar.gz" -O bin/dict.tar.gz && chmod 755 bin/dict.tar.gz
  tar -zxf bin/dict.tar.gz -C bin/
fi

if [ ! -f $OCEANBASE_DIR/tools/obtest/etc/ob_admin ]; then
   echo "do not get ob_admin anymore"
   #echo "now try to wget ob_admin from hudson"
   #wget -a ob_admin.wget.log http://11.166.86.153:8877/ob_admin -O $OCEANBASE_DIR/tools/obtest/etc/ob_admin 2>&1 && chmod 755 $OCEANBASE_DIR/tools/obtest/etc/ob_admin
fi

#Copy all the necessary files to run the backup-restore related obtest cases
#echo "now try to wget libdrcmsg.so from hudson"
#wget -a libdrcmsg.so.wget.log http://11.166.86.153:8877/libdrcmsg.so -O ${TARGET_AGENT_DIR}/libdrcmsg.so 2>&1

if [ ! -z "$CASE_NAME" ]; then
  ./mytest $CASE_NAME
fi
