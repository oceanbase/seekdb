#!/bin/bash

PROJECT_DIR=$1
PROJECT_NAME=$2
VERSION=$3
RELEASE=$4

CURDIR=$PWD
TOP_DIR=`pwd`/../

# check os type
PACKAGE_TYPE="rpm"
PACKAGE_EXTENSION="rpm"
OS_TYPE="$(uname -s)" || exit 1
if [[ "${OS_TYPE}" == "Darwin" ]]; then
  PACKAGE_TYPE="tgz"
  PACKAGE_EXTENSION="tar.gz"
fi

OB_DISABLE_LSE_OPTION=""
[[ $OB_DISABLE_LSE == "1" ]] && OB_DISABLE_LSE_OPTION="-DOB_DISABLE_LSE=ON"

echo "[BUILD] args: TOP_DIR=${TOP_DIR} PROJECT_NAME=${PROJECT_NAME} VERSION=${VERSION} RELEASE=${RELEASE} ${OB_DISABLE_LSE_OPTION}"

cd ${TOP_DIR}
./build.sh clean
./build.sh                    \
<<<<<<< HEAD
    ${PACKAGE_TYPE}                       \
=======
    ${PACKAGE_TYPE}           \
>>>>>>> master
    -DOB_RELEASEID=$RELEASE   \
    -DBUILD_NUMBER=$RELEASE   \
    -DUSE_LTO_CACHE=ON	      \
    ${OB_DISABLE_LSE_OPTION}  \
    --init                    \
    --make ${PACKAGE_TYPE} || exit 1
<<<<<<< HEAD

cd ${TOP_DIR}/build_${PACKAGE_TYPE}
mv *.${PACKAGE_EXTENSION} $CURDIR || exit 2
=======

cd ${TOP_DIR}/build_${PACKAGE_TYPE}
mv *.${PACKAGE_EXTENSION} $CURDIR || exit 2

>>>>>>> master
