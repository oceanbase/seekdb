#!/bin/sh
# 自 farm-jenkins/scripts/opensource/scripts/farm_compile.sh 复制
set +x
export HOME=$_CONDOR_JOB_IWD
PRINT_ENV=1

MAKE=${MAKE:-make}
MAKE_ARGS=${MAKE_ARGS:--j32}

## use cmake
function run_new_oceanbase()
{
    cd $HOME/oceanbase

    TARGET=observer
    BINARY="src/observer/observer "

    if [[ "$PACKAGE_TYPE" == "release" ]]
    then
        BUILD_TARGET="release"
    fi

    BINARY_PATH=$HOME
    if [[ $NEED_CE_TEST_PARALLEL == "1" ]]
    then
        mkdir -p $BINARY_PATH
    elif [[ $NEED_SANITY_TEST_PARALLEL == "1" ]]
    then
        mkdir -p $BINARY_PATH
        BUILD_TARGET="sanity"
    fi

    if [[ -f $HOME/oceanbase/tools/ob_admin/CMakeLists.txt ]]
    then
        TARGET="$TARGET ob_admin"
    fi

    if [[ $NEED_CE_TEST_PARALLEL == "2" || $NEED_CE_TEST_PARALLEL == "1" ]]
    then
        BUILD_TARGET="$BUILD_TARGET -DOB_BUILD_OPENSOURCE=ON"
    fi

    if [ "$ENABLE_LIBOBLOG" == "1" ]
    then
        if [ -f $HOME/oceanbase/.ce ]
        then
            BUILD_TARGET="$BUILD_TARGET -DOB_BUILD_LIBOBLOG=ON"
        fi
        if [ -f $HOME/oceanbase/test/.obcdc_conf ]
        then
            code_path=`sed '/^code_path=/!d;s/.*=//' $HOME/oceanbase/test/.obcdc_conf`
            if [ -d $HOME/oceanbase/$code_path ]
            then
                target_path=`sed '/^output_path=/!d;s/.*=//' $HOME/oceanbase/test/.obcdc_conf`
                so_name=`sed '/^so_name=/!d;s/.*=//' $HOME/oceanbase/test/.obcdc_conf`
                tailf_name=`sed '/^tailf_name=/!d;s/.*=//' $HOME/oceanbase/test/.obcdc_conf`
                target_str=`sed '/^build_target=/!d;s/.*=//' $HOME/oceanbase/test/.obcdc_conf | sed 's/,/ /'`
                so_name_V=`sed '/^so_name_V=/!d;s/.*=//' $HOME/oceanbase/test/.obcdc_conf`
                so_name_v=`sed '/^so_name_v=/!d;s/.*=//' $HOME/oceanbase/test/.obcdc_conf`
                TARGET="$TARGET $target_str"
                BINARY="$BINARY $target_path""src/$so_name $target_path""src/$so_name_V $target_path""src/$so_name_v $target_path""tests/$tailf_name"
            else
                echo "读取.obcdc_conf配置文件, 获取路径$code_path, 对应目录不存在！"
                return 1
            fi
        fi
        if [ -d $HOME/oceanbase/tools/obcdc ]
        then
            TARGET="$TARGET obcdc obcdc_tailf"
            BINARY="$BINARY tools/obcdc/src/libobcdc.so.1.0.0 tools/obcdc/tests/obcdc_tailf"
        else
            touch $BINARY_PATH/libobcdc.so.1.0.0 $BINARY_PATH/obcdc_tailf
        fi
        if [ -d $HOME/oceanbase/src/liboblog ]
        then
            TARGET="$TARGET oblog oblog_tailf"
            BINARY="$BINARY src/liboblog/src/liboblog.so.1.0.0 src/liboblog/tests/oblog_tailf"
        else
            touch $HOME/liboblog.so.1.0.0 $HOME/oblog_tailf
        fi
    fi

    if [ "$CREATE_AGENTSERVER" == "1" ] && [ -f $HOME/oceanbase/tools/obtest/backup_restore_farm.sh ]
    then
       TARGET="$TARGET agentserver incbackupserver"
       BINARY="$BINARY tools/agentserver/agentserver tools/agentserver/incbackupserver"
    fi

    if [ "$CREATE_LIBOBSERVER_SO" == "1" ]
    then
        TARGET="$TARGET oceanbase"
        BINARY="$BINARY src/observer/liboceanbase.so*"
    fi

    if [ -d $HOME/oceanbase/tools/ob_error ]
    then
        TARGET="$TARGET oberror"
        BINARY="$BINARY tools/ob_error/src/liboberror.so"
    else
        touch $HOME/liboberror.so
    fi
    if [[ -f $HOME/oceanbase/test/compile/compile.conf ]]
    then
        EXTRA_ARGV=`sed 's/.*EXTRA_COMPILE_ARGS=\(.*\)/\1/' $HOME/oceanbase/test/compile/compile.conf`
        BUILD_TARGET="$BUILD_TARGET $EXTRA_ARGV"
    fi
    sed -i 's/mirrors.aliyun.com/mirrors.cloud.aliyuncs.com/g' $HOME/oceanbase/deps/init/oceanbase.el7.x86_64.deps 2>/dev/null || true
    sh -x build.sh $BUILD_TARGET --init || return
    cd build_* || return

    start=$(date +%s)
    time $MAKE $MAKE_ARGS $TARGET
    ret=$?

    mv $BINARY $BINARY_PATH

    if [ -f $HOME/oceanbase/test/.obcdc_conf ]
    then
        if [[ -f $BINARY_PATH/$so_name ]]
        then
            if [[ $so_name != "libobcdc.so" ]]
            then
                cp $BINARY_PATH/$so_name $BINARY_PATH/libobcdc.so
            fi
        elif [[ -f $BINARY_PATH/$so_name_v ]]
        then
            cp $BINARY_PATH/$so_name_v $BINARY_PATH/libobcdc.so
        elif [[ -f $BINARY_PATH/$so_name_V ]]
        then
            cp $BINARY_PATH/$so_name_V $BINARY_PATH/libobcdc.so
        else
            touch $BINARY_PATH/libobcdc.so
        fi
        if [[ -f $BINARY_PATH/$tailf_name ]]
        then
            if [[ $tailf_name != "obcdc_tailf" ]]
            then
                cp $BINARY_PATH/$tailf_name $BINARY_PATH/obcdc_tailf
            fi
        else
            touch $BINARY_PATH/obcdc_tailf
        fi
    fi

    if [[ ! -f $BINARY_PATH/libobcdc.so.1.0.0 ]]
    then
        touch $BINARY_PATH/libobcdc.so.1.0.0 $BINARY_PATH/obcdc_tailf
    fi
    if [[ ! -f $BINARY_PATH/libobcdc.so ]]
    then
        touch $BINARY_PATH/libobcdc.so
    fi
    if [[ ! -f $BINARY_PATH/obcdc_tailf ]]
    then
        touch $BINARY_PATH/obcdc_tailf
    fi

    if [[ -f $HOME/oceanbase/test/obproxy_test_config ]]
    then
        cp $HOME/oceanbase/test/obproxy_test_config $BINARY_PATH/obproxy_test_config
    fi
    return $ret
}

function run_old()
{
    cd $HOME/oceanbase &&
        ./build.sh init && sw &&
        ./build.sh $BUILD_TARGET --enable-dlink-observer=no || return 1

    if [ "$REPO" == "server" ]
    then
        cd $HOME/oceanbase/src && ${MAKE} ${MAKE_ARGS} observer/observer || return 2
        mv observer/observer $HOME/ || return 3
        if [ "$ENABLE_LIBOBLOG" == "1" ]
        then
            if [ -d $HOME/oceanbase/src/liboblog ]
            then
                (cd liboblog && ${MAKE} ${MAKE_ARGS}) || return 4
                mv liboblog/{src/liboblog.la,src/.libs/liboblog.so.1.0.0,tests/oblog_tailf} $HOME/
            fi
            if [ -d $HOME/oceanbase/tools/obcdc ]
            then
                (cd tools/obcdc && ${MAKE} ${MAKE_ARGS}) || true
                touch $HOME/libobcdc.so.1.0.0 $HOME/obcdc_tailf 2>/dev/null
            fi
        fi
        if [ "$CREATE_AGENTSERVER" == "1" ] && [ -f $HOME/oceanbase/tools/obtest/backup_restore_farm.sh ]
        then
            cd $HOME/oceanbase && cd tools/agentserver && ${MAKE} ${MAKE_ARGS} || return 3
            mv agentserver incbackupserver $HOME/ 2>/dev/null || true
        fi
        if [ "$CREATE_LIBOBSERVER_SO" == "1" ]
        then
            cd $HOME/oceanbase && rm -f config.status && ./build.sh $BUILD_TARGET --enable-dlink-observer=yes || return 5
            cd src && rm -f observer/libobserver.la && ${MAKE} ${MAKE_ARGS} observer/libobserver.la || return 6
            mv observer/.libs/libobserver.so.0.0.0 $HOME/ 2>/dev/null || true
        fi
        touch $HOME/libincbackup.la $HOME/libincbackup.a $HOME/libagentbackup.la $HOME/libagentbackup.a 2>/dev/null
        touch $HOME/libobcdc.so.1.0.0 $HOME/libobcdc.so $HOME/obcdc_tailf 2>/dev/null
    elif [ "$REPO" == "proxy" ]
    then
        cd $HOME/oceanbase/src && ${MAKE} ${MAKE_ARGS} || return 2
        mv obproxy/obproxy $HOME/
    else
        errcho "Repository hasn't been supported"
        return 2
    fi
}

function run()
{
    if [[ "$REPO" == "server" && -f $HOME/oceanbase/CMakeLists.txt ]]
    then
        run_new_oceanbase
    else
        run_old
    fi
}

source $HOME/scripts/frame.sh && main
