#!/bin/bash

basename=`pwd`

echo "The legacy test harness is not bundled or downloaded by update_local_bin.sh."
echo "Set MYTEST_JAR to a compatible external harness when running ./mytest."

if [ ! -d "./bin" ]
then
    ln -s $basename/../deploy/bin bin
fi

mkdir -p lib
for f in $basename/../deploy/lib/*; do
  ln -s $f lib
done
