#!/bin/bash
set -e
set -x

SHEEL_DIR=$(cd "$(dirname "$0")" && pwd)
TOPDIR=$(realpath "$SHEEL_DIR/../..")

export PIP_INDEX_URL=https://pypi.tuna.tsinghua.edu.cn/simple
PACKAGE_VERSION=${PACKAGE_VERSION:-"0.0.1.dev3"}
export PACKAGE_VERSION

# setup build essentials
yum -y install git wget rpm* cpio make glibc-devel glibc-headers binutils m4 libtool libaio ccache
ccache -M 10G

# setup python
PYTHON_HOME=${PYTHON_HOME:-"/opt/python/cp38-cp38/"}
PYTHON=$PYTHON_HOME/bin/python
PYTHON_VERSION=`$PYTHON --version 2>&1 | awk '{print $2}' | cut -d. -f1,2`

#yum install -y python${PYTHON_VERSION}-devel
$PYTHON -m ensurepip
$PYTHON -m pip install build wheel setuptools pybind11 auditwheel

ARCH=`uname -m`
REBUILD=${REBUILD:-"1"}
BUILD_TYPE=release
LIBRARY_NAME=libseekdb_python
OBLITE_LIBRARY_PATH=$TOPDIR/build_$BUILD_TYPE/src/observer/embed/${LIBRARY_NAME}.so
PYTHON_EMBED_DIR=$TOPDIR/package/wheel
PYTHON_CORE_SRC_DIR=$PYTHON_EMBED_DIR/core/
PYTHON_LIB_SRC_DIR=$PYTHON_EMBED_DIR/lib/
BUILD_DIR=$PWD/build_python
WHEEL_HOUSE=$BUILD_DIR/wheelhouse
PYTHON_CORE_BUILD_DIR=$BUILD_DIR/core/
PYTHON_LIB_BUILD_DIR=$BUILD_DIR/lib/
mkdir -p $WHEEL_HOUSE

DUMMY_CODE="void dummy_function(){}"

# test environment
TEST_DIR=$BUILD_DIR/test
TEST_WHEEL_HOUSE=$TEST_DIR/wheelhouse
TEST_SOURCE=$PYTHON_EMBED_DIR/seekdb_test.py
# you shouldn't change this directory as it located by seekdb.__init__.py
LIB_CACHE_DIR=$HOME/.seekdb/
rm -rf $TEST_DIR
mkdir -p $TEST_DIR
rm -rf $LIB_CACHE_DIR

# compile
ccache -z
if [ $REBUILD -eq 0 ]; then
    echo "No need to rebuild"
else
    rm -rf $TOPDIR/build_$BUILD_TYPE
fi
$TOPDIR/build.sh $BUILD_TYPE --init -DOB_USE_CCACHE=ON -DBUILD_EMBED_MODE=ON -DPYTHON_VERSION=$PYTHON_VERSION -DCMAKE_PREFIX_PATH=$PYTHON_HOME --make
ccache -s

# prepare the library
ls -lh $OBLITE_LIBRARY_PATH
pushd $BUILD_DIR
libaio_path=`ldd $OBLITE_LIBRARY_PATH | grep libaio | awk '{print $3}'`
ls -l $libaio_path
strip $OBLITE_LIBRARY_PATH
ldd $OBLITE_LIBRARY_PATH
patchelf --print-rpath $OBLITE_LIBRARY_PATH
patchelf --set-rpath '$ORIGIN/' $OBLITE_LIBRARY_PATH
patchelf --print-rpath $OBLITE_LIBRARY_PATH
split --numeric-suffixes --suffix-length=1 --bytes=300M $OBLITE_LIBRARY_PATH ${LIBRARY_NAME}.so.
ls -lh ${LIBRARY_NAME}.so.0 ${LIBRARY_NAME}.so.1
# oblite.so.0 is a library and will be repaired by auditwheel, but it's invalid.
# so I compress it to make it a plain file.
gzip --force ${LIBRARY_NAME}.so.0 ${LIBRARY_NAME}.so.1
sha256sum $OBLITE_LIBRARY_PATH | awk '{print $1}' > ${LIBRARY_NAME}.so.sha
sha256sum ${LIBRARY_NAME}.so.0.gz | awk '{print $1}' > ${LIBRARY_NAME}.so.sha.0
sha256sum ${LIBRARY_NAME}.so.1.gz | awk '{print $1}' > ${LIBRARY_NAME}.so.sha.1

# prepare build directory
rm -rf $PYTHON_CORE_BUILD_DIR $PYTHON_LIB_BUILD_DIR
cp -rf $PYTHON_CORE_SRC_DIR $PYTHON_CORE_BUILD_DIR
cp -rf $PYTHON_LIB_SRC_DIR $PYTHON_LIB_BUILD_DIR

cp ${LIBRARY_NAME}.so.1.gz ${LIBRARY_NAME}.so.sha* $libaio_path $PYTHON_CORE_BUILD_DIR/
cp ${LIBRARY_NAME}.so.0.gz $PYTHON_LIB_BUILD_DIR/

cp $PYTHON_EMBED_DIR/README.md $PYTHON_CORE_BUILD_DIR/
cp $PYTHON_EMBED_DIR/README.md $PYTHON_LIB_BUILD_DIR/
echo "$DUMMY_CODE" > $PYTHON_CORE_BUILD_DIR/dummy.c
echo "$DUMMY_CODE" > $PYTHON_LIB_BUILD_DIR/dummy.c
popd

# build the wheel
pushd $PYTHON_CORE_BUILD_DIR
$PYTHON -m build --wheel && $PYTHON -m auditwheel repair --only-plat --plat manylinux_2_28_$ARCH dist/*.whl --wheel-dir $TEST_WHEEL_HOUSE/
popd
pushd $PYTHON_LIB_BUILD_DIR
$PYTHON -m build --wheel && $PYTHON -m auditwheel repair --only-plat --plat manylinux_2_28_$ARCH dist/*.whl --wheel-dir $TEST_WHEEL_HOUSE/
popd

ls -lh $TEST_WHEEL_HOUSE/*.whl

# test the wheels
pushd $TEST_DIR
$PYTHON -m venv py$PYTHON_VERSION
source py$PYTHON_VERSION/bin/activate
python -m pip install $TEST_WHEEL_HOUSE/*.whl
python $TEST_SOURCE
deactivate
popd

# move the wheels to destination
mv $TEST_WHEEL_HOUSE/*.whl $WHEEL_HOUSE

ls -lh $WHEEL_HOUSE
