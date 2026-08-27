#!/bin/bash

basename=`pwd`

wget "http://11.166.86.153:8877/mytest10.jar" >/dev/null 2>&1 -O jar/mytest.jar

if [ ! -d "./bin" ]
then
    ln -s $basename/../deploy/bin bin
fi

mkdir -p lib
for f in $basename/../deploy/lib/*; do
  ln -s $f lib
done
