#!/bin/bash

httpserver=http://11.166.86.153:8877/
VERSION=master

# download mytest.jar
if [ ! -f jar/mytest.jar ]; then
  echo "begin to wget --no-check-certificate mytest10.jar ..."
  wget --no-check-certificate ${httpserver}/mytest10.jar -O jar/mytest.jar >/dev/null 2>&1 &
fi

# download bin (default)
if [ ! -n "$1" ];then
  echo "begin to wget --no-check-certificate observer ..."
  mkdir -p bin
  wget --no-check-certificate ${httpserver}/observer.$VERSION -O bin/observer &

fi

# download rpm
if [ "$1" == "rpm" ];then
  url=${httpserver}/oceanbase10-test.rpm
  debugurl=${httpserver}/oceanbase10-debuginfo-test.rpm

  echo "rpm url => " $url
  rpm2cpio $url | cpio -div

  cp -fr ./home/admin/oceanbase/bin/* bin/
  cp -fr ./home/admin/oceanbase/bin/* tools/
  cp -fr ./home/admin/oceanbase/lib/* lib/
  cp -fr ./home/admin/oceanbase/etc/* etc/

  echo "begin wget --no-check-certificate debuginfo ..."
  wget --no-check-certificate "$debugurl" -O debug.rpm > /dev/null 2>&1 &
fi

#download ob_admin
if [ ! -f etc/ob_admin ]; then
   echo "now try to wget ob_admin from hudson"
   wget --no-check-certificate ${httpserver}/ob_admin.$VERSION -O etc/ob_admin 2>&1 &
fi

wait

chmod +x etc/ob_admin
chmod +x bin/*

./bin/observer -V
