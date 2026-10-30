#!/bin/bash
if [ $# -lt 1 ]
then
  echo "Usage ./quick_copy.sh [oceanbase_dev_dir]"
  OCEANBASE_DIR=`pwd`/../../
else
  OCEANBASE_DIR=$1
fi

BIN_DIR=`pwd`/bin
LIB_DIR=`pwd`/lib
ETC_DIR=`pwd`/etc

if [ $# -lt 2 ]
then
  mkdir -p $BIN_DIR
  mkdir -p $LIB_DIR
  mkdir -p $ETC_DIR
  libtool --mode=install cp $OCEANBASE_DIR/src/observer/libobserver.la $LIB_DIR
  libtool --mode=install cp $OCEANBASE_DIR/src/lib/compress/libzstd_1.3.8.la $LIB_DIR
  libtool --mode=install cp $OCEANBASE_DIR/src/lib/compress/liblzo_1.0.la $LIB_DIR
  libtool --mode=install cp $OCEANBASE_DIR/src/lib/compress/libnone.la $LIB_DIR
  libtool --mode=install cp $OCEANBASE_DIR/src/lib/compress/libzstd_1.3.8.la $LIB_DIR
  libtool --mode=install cp $OCEANBASE_DIR/src/lib/compress/libzstd_1.3.8.la $LIB_DIR
  libtool --mode=install cp $OCEANBASE_DIR/src/observer/observer $BIN_DIR/
fi
echo "The legacy test harness is not bundled or downloaded by quick_copy.sh."
echo "Set MYTEST_JAR to a compatible external harness when running ./mytest."
