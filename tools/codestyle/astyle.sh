#!/bin/sh

astyle_require_version="Artistic Style Version 2.04 (OB-Modified_v0.4)"

script=$(readlink -f "$0")
scriptpath=$(dirname "$script")

if [ -t 1 ]
then
  if [ -z "$*" ]
  then
    echo "Usage  :  astyle.sh [options] Source1.cpp Source2.cpp  [...]"
    echo "          astyle.sh [options] < Original > Beautified"
    exit -1;
  fi
fi

if [ ! -e $scriptpath/astyle/build/gcc/bin/astyle ]
then
  cd $scriptpath/astyle/build/gcc
  make -j
  cd -L -
fi

binary_version=`$scriptpath/astyle/build/gcc/bin/astyle --version 2>&1 | head -n1`
if [[ "$binary_version" != "$astyle_require_version" ]]
then
  cd $scriptpath/astyle/build/gcc
  make -j
  cd -L -
  binary_version=`$scriptpath/astyle/build/gcc/bin/astyle --version 2>&1 | head -n1`
  if [[ "$binary_version" != "$astyle_require_version" ]]
  then
    echo "WARN: Astyle version not match, binary version [$binary_version], require version [$astyle_require_version]"
  fi
fi

$scriptpath/astyle/build/gcc/bin/astyle --options=$scriptpath/obastylerc $*
