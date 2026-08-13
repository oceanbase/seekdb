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
  mkdir -p $BIN_DIR
  mkdir -p $LIB_DIR
  mkdir -p $ADMIN_DIR
  mkdir -p $ETC_DIR
  mkdir -p $TARGET_AGENT_DIR
  #for system package
  cp $OCEANBASE_DIR/src/share/inner_table/sys_package/* $ADMIN_DIR

  if [ -d "../../build_debug" ]
  then
    OBSERVER_BIN=`pwd`/../../build_debug
  elif [ -d "../../build_release" ]
  then
    OBSERVER_BIN=`pwd`/../../build_release
  elif [ -d "../../build_release_coverage" ]
  then
    OBSERVER_BIN=`pwd`/../../build_release_coverage
  elif [ -d "../../build_errsim" ]
  then
    OBSERVER_BIN=`pwd`/../../build_errsim
  elif [ -d "../../build_errsim_debug" ]
  then
    OBSERVER_BIN=`pwd`/../../build_errsim_debug
  elif [ -d "../../build_errsim_sanity" ]
  then 
    OBSERVER_BIN=`pwd`/../../build_errsim_sanity
  fi

  if [ $COMPILE_LOCAL_COPY -eq 1 ]; then
    (pushd $OBSERVER_BIN && ob-make -j $COMPILE_CONCURRENCY seekdb && popd) || exit $?
  fi

  libtool --mode=install cp $OBSERVER_BIN/src/observer/seekdb $BIN_DIR/observer
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
