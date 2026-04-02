#!/bin/sh
# 自 farm-jenkins/scripts/opensource/scripts/farm_post_compile.sh 复制
set +x
ret=$1
test "$ret" = "0" || exit $ret
function strip
{
    bin=$1
    if ! objdump -s -j .gnu_debuglink $bin 2>/dev/null | grep debuglink
    then
        objcopy --only-keep-debug $bin $bin.debug &&
            objcopy --add-gnu-debuglink=$bin.debug $bin &&
            objcopy -g $bin
    fi
}

function run
{
    if [ ! -e observer ]
    then
        echo "observer不存在"
        exit 1
    fi

    root_path=`pwd`

    if [[ -f $root_path/obproxy_test_config ]]
    then
        set +x
        cat >> /etc/hosts <<EOF 2>/dev/null || true
172.16.0.220 github.com
172.16.0.220 mirrors.aliyun.com
172.16.0.220 maven.aliyun.com
EOF
        obproxy_yum_version=`sed '/^obproxy_yum_version=/!d;s/.*=//' $root_path/obproxy_test_config`
        obproxy_yum_release=`sed '/^obproxy_yum_release=/!d;s/.*=//' $root_path/obproxy_test_config`
        obproxy_yum_filename="obproxy-ce-"$obproxy_yum_version"-"$obproxy_yum_release".x86_64.rpm"
        wget_url="http://mirrors.cloud.aliyuncs.com/oceanbase/community/stable/el/7/x86_64/"$obproxy_yum_filename
        retry_times=10
        for ((i=0; i<$retry_times; i++))
        do
            wget $wget_url -O $obproxy_yum_filename 2>/dev/null
            [[ -f $obproxy_yum_filename ]] && break
            echo "yum源网络异常, 10s后开始重试..."
            sleep 10
        done
        if [[ -f $obproxy_yum_filename ]]
        then
            rpm2cpio $obproxy_yum_filename | cpio -div
            if [ -f home/admin/obproxy-*/bin/obproxy ]
            then
                mv home/admin/obproxy-*/bin/obproxy $root_path
                chmod +x $root_path/obproxy
            fi
        fi
    fi

    if [ ! -e obproxy ]
    then
        echo "obproxy不存在"
        exit 1
    fi

    strip observer || return 2
    strip obproxy || return 3
    test -f libobserver.so.0.0.0 && (strip libobserver.so.0.0.0 || return 4)
    test -f liboblog.so.1.0.0 && (strip liboblog.so.1.0.0 || return 5)

    touch agentserver incbackupserver 2>/dev/null
    return 0
}
run &> post_compile.output 2>&1
