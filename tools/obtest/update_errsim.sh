#!/bin/bash

httpserver=http://11.166.86.153:8877/
VERSION=master

echo "The legacy test harness is not bundled or downloaded by update_errsim.sh."
echo "Set MYTEST_JAR to a compatible external harness when running ./mytest."

# download bin (default)
if [ ! -n "$1" ];then
  echo "begin to wget --no-check-certificate observer ..."
  mkdir -p bin
  wget --no-check-certificate ${httpserver}/observer.${VERSION}_errsim-7u -O bin/observer &

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
