#!/bin/sh
# 自 farm-jenkins/scripts/opensource/scripts/mysqltest_for_farm.sh 复制

set +x

export HOME=$_CONDOR_JOB_IWD
export PATH=/bin:/usr/bin
export USER=$(whoami)

HOST=`hostname -i 2>/dev/null || echo 127.0.0.1`
DOWNLOAD_DIR=$HOME/downloads
SLOT_ID=`echo $_CONDOR_SLOT | cut -c5-`

function prepare_config {
    if [[ "$MINI" == "1" ]] || ([[ "$MINI" == "-1" ]] && [[ -f $HOME/oceanbase/tools/deploy/enable_mini_mode ]])
    then
        [[ -f $HOME/oceanbase/tools/deploy/enable_mini_mode ]] && MINI_SIZE=$(cat $HOME/oceanbase/tools/deploy/enable_mini_mode)
        [[ $MINI_SIZE =~ ^[0-9]+G ]] || MINI_SIZE="8G"
        MINI_CONFIG_ITEM="ObCfg.init_config['memory_limit']='$MINI_SIZE'"
    fi

    cd $HOME/oceanbase/tools/deploy
    if [ "$WITH_PROXY" ] && [ "$WITH_PROXY" != "0" ]
    then
        if [ "$CLUSTER_SPEC" == '2x1' ]
        then
            SPEC="[$HOST,proxy@$HOST]@zone1 [$HOST]@zone2"
        else
            SPEC="[$HOST,proxy@$HOST]@zone1"
        fi
    else
        if [[ "$CLUSTER_SPEC" == '2x1' ]] || [[ "$SLAVE" == "1" ]]
        then
            SPEC="[$HOST]@zone1 [$HOST]@zone2"
        else
            SPEC="[$HOST]@zone1"
        fi
    fi
    cat >config7.py <<EOF
home='$HOME'
data_dir='$HOME/data'
port_gen = itertools.count(5000 + $SLOT_ID * 100)
$USER = OBI(
    server_spec='$SPEC',
    is_local=True,
    proxy_cfg_key='$USER_\${local_ip}_proxy_$SLOT_ID',
    cfg_key='$USER_\${local_ip}_rslist_$SLOT_ID')
$MINI_CONFIG_ITEM
EOF
}

function prepare_bin {
    mkdir -p $DOWNLOAD_DIR || return 1

    cd $HOME
    if [ "$WITH_PROXY" ] && [ "$WITH_PROXY" != "0" ]
    then
        # 先从$HOME目录获取
        if [[ -x $HOME/obproxy ]]
        then
            echo "从根目录读取obproxy..."
            mv obproxy $DOWNLOAD_DIR/ || return 3
        elif [[ -x $HOME/oceanbase/tools/obproxy/obproxy ]]
        then
            echo "从ob代码库获取obproxy..."
            cp $HOME/oceanbase/tools/obproxy/obproxy $DOWNLOAD_DIR/ || return 1
        fi
    fi

    if [ ! -x observer ]
    then
        return 1
    else
    cp observer $DOWNLOAD_DIR/ || return 2
    fi

    if [ "$WITH_DEPS" ] && [ "$WITH_DEPS" != "0" ]
    then
        sed -i 's/mirrors.aliyun.com/mirrors.cloud.aliyuncs.com/g' $HOME/oceanbase/deps/init/oceanbase.el7.x86_64.deps
        cd $HOME/oceanbase && ./build.sh init || return 3
    fi
    # ignore copy.sh failed
    cd $HOME/oceanbase/tools/deploy && [[ -f copy.sh ]] && (sh copy.sh || return 0)
}

function prepare_obs {
    cd $HOME
    if [ "$WITH_PROXY" ] && [ "$WITH_PROXY" != "0" ]
    then
       mkdir -p $USER.proxy0/bin &&
           ln -st $USER.proxy0/bin $DOWNLOAD_DIR/obproxy || return 1
    fi

    OBS_CNT=2
    for ((i=0; i<$OBS_CNT; i++))
    do
        mkdir -p $HOME/$USER.obs$i/bin &&
            ln -st $HOME/$USER.obs$i/bin $DOWNLOAD_DIR/observer || return 1
    done
}

function prepare_env {
    prepare_bin || return 1
    prepare_config || return 2
    prepare_obs || return 3
}

function run_mysqltest {
    slb=""
    [[ "$SLB" != "" ]] && slb="slb=$SLB,$EXECID"
    cd $HOME/oceanbase/tools/deploy
    mkfifo test.fifo
    if [ "$WITH_PROXY" ] && [ "$WITH_PROXY" != "0" ]
    then
        cat test.fifo | tee result.out & ./hap.py $USER.proxy0.mysqltest collect_all slices=$SLICES slice_idx=$SLICE_IDX $slb `echo $ARGV`  1> test.fifo 2>&1 
    elif [[ "$SLAVE" == "1" ]]
    then
	    cat test.fifo | tee result.out & ./hap.py $USER.obs1.mysqltest collect_all slices=$SLICES slice_idx=$SLICE_IDX $slb `echo $ARGV`  1> test.fifo 2>&1        
    else
        cat test.fifo | tee result.out & ./hap.py $USER.mysqltest collect_all slices=$SLICES slice_idx=$SLICE_IDX $slb `echo $ARGV`  1> test.fifo 2>&1 
    fi

    # check if there is error
    if [ "$?" == "0" ]
    then
       grep -E 'PASSED' $HOME/oceanbase/tools/deploy/result.out  > /dev/null && ! grep -E "FAIL LST" $HOME/oceanbase/tools/deploy/result.out  > /dev/null || return 1
    else
        return 1
    fi
}

function collect_log {
    cd $HOME/oceanbase/tools/deploy || return 1
    if [ -d "collected_log" ]
    then
        mv mysql_test/var/log/* collected_log
        mv $HOME/$USER.*/core[.-]* collected_log
        mv collected_log collected_log_$SLICE_IDX || return 1
        tar cvfz $HOME/collected_log_$SLICE_IDX.tar.gz collected_log_$SLICE_IDX
    fi
}

export -f run_mysqltest

function obd_prepare_obd {
    sed -i 's/mirrors.aliyun.com/mirrors.cloud.aliyuncs.com/g' $HOME/oceanbase/deps/init/oceanbase.el7.x86_64.deps
    if [[ -f $HOME/oceanbase/deps/init/dep_create.sh ]]
    then
        if [[ "$IS_CE" == "1" ]]
        then
          cd $HOME/oceanbase && ./build.sh init --ce || return 3
        else  
          cd $HOME/oceanbase && ./build.sh init || return 3
        fi
    else
        if grep 'dep_create.sh' $HOME/oceanbase/build.sh
        then
            cd $HOME/oceanbase/deps/3rd && bash dep_create.sh all || return 4
        else
            cd $HOME/oceanbase && ./build.sh init || return 3
        fi
    fi
    
    $obd devmode enable
    $obd env set OBD_DEPLOY_BASE_DIR $HOME/oceanbase/tools/deploy
    $obd --version
}

export -f obd_prepare_obd

function obd_prepare_global {
    export LANG=en_US.UTF-8
    HOST=`hostname -i`
    DOWNLOAD_DIR=$HOME/downloads
    SLOT_ID=`echo $_CONDOR_SLOT | cut -c5-`
    PORT_NUM=`expr 5000 + $SLOT_ID \* 100`

    # 根据传入的observer binary判断是否开源版
    if [[ `$HOME/observer -V 2>&1 | grep -E '(OceanBase CE|OceanBase_CE)'` ]]
    then
        COMPONENT="oceanbase-ce"
        OBRPOXY_COMPENT="obproxy-ce"
        export IS_CE=1
    else
        COMPONENT="oceanbase"
        OBRPOXY_COMPENT="obproxy"
        export IS_CE=0
    fi
    # 根据build.sh中的内容判断依赖安装路径
    if grep 'dep_create.sh' $HOME/oceanbase/build.sh
    then
        DEP_PATH=$HOME/oceanbase/deps/3rd
    else
        DEP_PATH=$HOME/oceanbase/rpm/.dep_create/var
    fi

    if [[ -f "$HOME/oceanbase/tools/deploy/mysqltest_config.yaml" ]]
    then
        export WITH_MYSQLTEST_CONFIG_YAML=1
    else
        export WITH_MYSQLTEST_CONFIG_YAML=0
    fi

    ob_name=obcluster$SLOT_ID
    app_name=$ob_name.$USER.$HOST

    export obd=$DEP_PATH/usr/bin/obd
    export OBD_HOME=$HOME
    export OBD_INSTALL_PRE=$DEP_PATH
    export DATA_PATH=$HOME/data

}

function obd_prepare_config {
    
    MINI_SIZE="10G"
    if [[ "$MINI" == "1" ]] || ([[ "$MINI" == "-1" ]] && [[ -f $HOME/oceanbase/tools/deploy/enable_mini_mode ]])
    then
        [[ -f $HOME/oceanbase/tools/deploy/enable_mini_mode ]] && MINI_SIZE=$(cat $HOME/oceanbase/tools/deploy/enable_mini_mode)
        [[ $MINI_SIZE =~ ^[0-9]+G ]] || MINI_SIZE="8G"
    fi
    if [[ "$CLUSTER_SPEC" == '2x1' ]] || [[ "$SLAVE" == "1" ]]
    then
        mysql_port=$PORT_NUM && PORT_NUM=`expr $PORT_NUM + 1`
        rpc_port1=$PORT_NUM && PORT_NUM=`expr $PORT_NUM + 1`
        obshell_port1=$PORT_NUM && PORT_NUM=`expr $PORT_NUM + 1`
        mysql_port2=$PORT_NUM && PORT_NUM=`expr $PORT_NUM + 1`
        rpc_port2=$PORT_NUM && PORT_NUM=`expr $PORT_NUM + 1`
        obshell_port2=$PORT_NUM && PORT_NUM=`expr $PORT_NUM + 1`
        SERVERS=$(cat <<-EOF
  servers:
    - name: server1
      ip: 127.0.0.1
    - name: server2
      ip: 127.0.0.1
  server1:
    mysql_port: $mysql_port
    rpc_port: $rpc_port1
    obshell_port: $obshell_port1
    home_path: $DATA_PATH/observer1
    zone: zone1
  server2:
    mysql_port: $mysql_port2
    rpc_port: $rpc_port2
    obshell_port: $obshell_port2
    home_path: $DATA_PATH/observer2
    zone: zone2
EOF
)
    else
        mysql_port=$PORT_NUM && PORT_NUM=`expr $PORT_NUM + 1`
        rpc_port=$PORT_NUM && PORT_NUM=`expr $PORT_NUM + 1`
        obshell_port=$PORT_NUM && PORT_NUM=`expr $PORT_NUM + 1`
        SERVERS=$(cat <<-EOF
  servers:
    - name: server1
      ip: 127.0.0.1
  server1:
    mysql_port: $mysql_port
    rpc_port: $rpc_port
    obshell_port: $obshell_port
    home_path: $DATA_PATH/observer1
    zone: zone1
EOF
)
    fi
    export MYSQL_PORT=$mysql_port
    proxy_conf=""
    if [ "$WITH_PROXY" ] && [ "$WITH_PROXY" != "0" ]
    then
        listen_port=$PORT_NUM && PORT_NUM=`expr $PORT_NUM + 1`
        prometheus_listen_port=$PORT_NUM && PORT_NUM=`expr $PORT_NUM + 1`
        
        proxy_conf=$(cat $HOME/oceanbase/tools/deploy/obd/obproxy.yaml.template)
        if [[ -f $HOME/oceanbase/test/obproxy_test_config ]]
        then
            obproxy_switch=`sed '/^obproxy_switch=/!d;s/.*=//' $HOME/oceanbase/test/obproxy_test_config`

            # 当且仅当 obproxy 指定模式为wget时，影响mysqltest obproxy配置，否则mysqltest依旧沿用原有obproxy获取逻辑
            mkdir $HOME/bin
            if [[ -f $HOME/obproxy ]]
            then
                cp $HOME/obproxy $HOME/bin
            else
                wget_url=`sed '/^wget_url=/!d;s/.*=//' $HOME/oceanbase/test/obproxy_test_config`
                wget $wget_url -O $HOME/bin/obproxy
                if [[ -f $HOME/bin/obproxy ]]
                then
                    echo "obproxy 文件下载成功"
                    chmod +x $HOME/bin/obproxy
                    
                else
                    # 下载失败，沿用原有逻辑
                    echo "obproxy 文件下载失败"
                    return 1
                fi
            fi
            
            echo "obrpxoy版本:"
            echo `$HOME/bin/obproxy -V 2>&1`

            obproxy_version=$("$HOME"/bin/obproxy --version  2>&1  | grep -E 'obproxy \(OceanBase [\.0-9]+ \S+\)' | grep -Eo '\s[.0-9]+\s')
            # obproxy 额外依赖了lib目录，然后需要额外创建
            mkdir -p $HOME/lib
            $obd mirror create -n $OBRPOXY_COMPENT -p $HOME -t "latest" -V $obproxy_version
            echo "执行结果: $?"

            proxy_conf=$(echo "$proxy_conf" | sed '/package_hash: [0-9a-z]*/d')
            proxy_conf="""$proxy_conf
  tag: latest
"""
        fi
        proxy_conf=${proxy_conf//'{{%% COMPONENT %%}}'/$COMPONENT}
        proxy_conf=${proxy_conf//'{{%% LISTEN_PORT %%}}'/$listen_port}
        proxy_conf=${proxy_conf//'{{%% PROMETHEUS_LISTEN_PORT %%}}'/$prometheus_listen_port}
        proxy_conf=${proxy_conf//'{{%% OBPORXY_HOME_PATH %%}}'/$DATA_PATH\/obproxy}
        proxy_conf=${proxy_conf//'{{%% OBPROXY_CONFIG_SERVER_URL %%}}'/$proxy_cfg_url}
    fi

    conf=$(cat $HOME/oceanbase/tools/deploy/obd/config.yaml.template)
    # cgroup support
    if [[ -f $HOME/oceanbase/tools/deploy/obd/.use_cgroup || $(echo " $ARGV " | grep " use-cgroup ") ]] 
    then
        CGROUP_CONFIG="cgroup_dir: /sys/fs/cgroup/cpu/$USER"
    fi
    # MYSQLRTEST_ARGS contains changes for special run
    proxy_conf="""$MYSQLRTEST_ARGS
    $CGROUP_CONFIG
$proxy_conf"""
    conf=${conf//'{{%% PROXY_CONF %%}}'/"$proxy_conf"}
    conf=${conf//'{{%% DEPLOY_PATH %%}}'/"$HOME/oceanbase/tools/deploy"}
    conf=${conf//'{{%% COMPONENT %%}}'/$COMPONENT}
    conf=${conf//'{{%% SERVERS %%}}'/$SERVERS}
    conf=${conf//'{{%% TAG %%}}'/'latest'}
    conf=${conf//'{{%% MINI_SIZE %%}}'/$MINI_SIZE}
    conf=${conf//'{{%% OBCONFIG_URL %%}}'/$cfg_url}
    conf=${conf//'{{%% APPNAME %%}}'/$app_name}
    conf=${conf//'{{%% EXTRA_PARAM %%}}'/$MYSQLRTEST_ARGS}
    if [[ "$IS_CE" == "1" ]]
    then
      conf=$(echo "$conf" | sed 's/oceanbase:/oceanbase\-ce:/g' | sed 's/\- oceanbase$/- oceanbase-ce/g')
    fi
    echo "$conf" > $HOME/oceanbase/tools/deploy/config.yaml
    cat $HOME/oceanbase/tools/deploy/config.yaml
}
function get_baseurl {
    repo_file=$OBD_HOME/.obd/mirror/remote/taobao.repo
    start=0
    end=$(cat $repo_file | wc -l )
    for line in $(grep -nE "^\[.*\]$" $repo_file)
    do
      num=$(echo "$line" | awk -F ":" '{print $1}')
      [ "$find_section" == "1" ] && end=$num
      is_target_section=$(echo $(echo "$line" | grep -E "\[\s*$repository\s*\]"))
      [ "$is_target_section" != "" ] && start=$num && find_section='1'
    done
    repo_section=$(awk "NR>=$start && NR<=$end" $repo_file)
    baseurl=$(echo "$repo_section" | grep baseurl | awk -F '=' '{print $2}')
}

function get_version_and_release {
    start=0
    end=$(cat $config_yaml | wc -l)
    for line in $(grep -nE '^\S+:' $config_yaml)
    do
      num=$(echo "$line" | awk -F ":" '{print $1}')
      [ "$find_obproxy" == "1" ] && end=$num
      [ "$(echo "$line" | grep obproxy)" != "" ] && start=$num && find_obproxy='1'
    done
    odp_conf=$(awk "NR>=$start && NR<=$end" $config_yaml)
    version=$(echo "$odp_conf" | grep version | awk '{print $2}')
    release=$(echo "$odp_conf" | grep release | awk '{print $2}')
    include_file=$DEPLOY_PATH/$(echo "$odp_conf"| grep 'include:'| awk '{print $2}')
    if [[ -f $include_file ]]
    then
      _repo=$(echo $(grep '__mirror_repository_section_name:'  $include_file | awk '{print $2}'))
      [ "$_repo" != "" ] && repository=$_repo 
      version=$(echo $(grep 'version:' $include_file | awk '{print $2}'))
      release=$(echo $(grep 'release:' $include_file | awk '{print $2}'))
    fi
}

function get_obproxy {
    repository="taobao"
    config_yaml=$HOME/oceanbase/tools/deploy/config.yaml
    DEPLOY_PATH=$HOME/oceanbase/tools/deploy
    obproxy_mirror_repository=$(echo $(grep '__mirror_repository_section_name' $config_yaml | awk -F':' '{print $2}'))
    [ "$obproxy_mirror_repository" != "" ] && repository=$obproxy_mirror_repository

    get_version_and_release
    get_baseurl
    $obd mirror disable remote
    OS_ARCH="$(uname -m)"
    if [[ "$baseurl" != "" && "$version" != "" && "$release" != "" ]]
    then
      if [[ $OS_ARCH == "aarch64" ]]
      then
          pkg_name="obproxy-$version-$release.aarch64.rpm"
      else
          pkg_name="obproxy-$version-$release.x86_64.rpm"
      fi
      if [ "$(find $OBD_HOME/.obd/mirror/local -name $pkg_name)" == "" ]
      then
        download_dir=$OBD_HOME/.obd_download
        mkdir -p $download_dir
        wget $baseurl/obproxy/$pkg_name -O "$download_dir/$pkg_name" -o $download_dir/obproxy.down
        $obd mirror clone "$download_dir/$pkg_name" && rm -rf $download_dir && return 0
      else
        return 0
      fi
    fi
    echo "Download pkg failed. Try to get obproxy from repository $repository"
    count=0
    interval=5
    retry_limit=100
    while (( $count < $retry_limit ))
    do
    $obd mirror disable remote
    $obd mirror enable $repository
    $obd mirror update && $obd mirror list $repository > /dev/null && return 0
    count=`expr $count + 1`
    (( $count <= $retry_limit )) && sleep $interval
    done 
    return 1
}

function obd_prepare_bin {
    cd $HOME
    mkdir -p $DOWNLOAD_DIR/{bin,etc,admin,lib} || return 1

    # remove the logic to get obproxy again
    # if [ "$WITH_PROXY" ] && [ "$WITH_PROXY" != "0" ]
    # then
    #     get_obproxy || (echo "failed to get obdproxy" || return 1)
    # fi

    if [ ! -x observer ]
    then
        return 1
    else
        cp observer $DOWNLOAD_DIR/bin/ || return 2
    fi
    
    if [ -f $DEP_PATH/home/admin/oceanbase/bin/obshell ]
    then
        cp $DEP_PATH/home/admin/oceanbase/bin/obshell $DOWNLOAD_DIR/bin/
        chmod a+x $DOWNLOAD_DIR/bin/obshell
    fi

    cd $HOME/oceanbase/tools/deploy
    cp $DEP_PATH/u01/obclient/bin/obclient ./obclient && chmod 777 obclient
    cp $DEP_PATH/u01/obclient/bin/mysqltest ./mysqltest && chmod 777 mysqltest
    
    if [ "$WITH_DEPS" ] && [ "$WITH_DEPS" != "0" ]
    then
        cd $HOME/oceanbase/tools/deploy && [[ -f copy.sh ]] && sh copy.sh
    fi
    if [[ -f "$HOME/oceanbase/tools/deploy/obd/.observer_obd_plugin_version" ]]
    then
        obs_version=$(cat $HOME/oceanbase/tools/deploy/obd/.observer_obd_plugin_version)
    else
        obs_version=`$DOWNLOAD_DIR/bin/observer -V 2>&1 | grep -E "observer \(OceanBase([ \_]CE)? ([.0-9]+)\)" | grep -Eo '([.0-9]+)'`
    fi

    mkdir $HOME/oceanbase/tools/deploy/admin
    if [[ -d $HOME/oceanbase/src/share/inner_table/sys_package ]]
    then 
        cp $HOME/oceanbase/src/share/inner_table/sys_package/*.sql $HOME/oceanbase/tools/deploy/admin/
    fi

    $obd mirror create -n $COMPONENT -p $DOWNLOAD_DIR -t latest -V $obs_version || return 2

}

function obd_init_cluster {
    retries=$reboot_retries
    while (( $retries > 0 ))
    do
    if [[ "$retries" == "$reboot_retries" ]]
    then
        $obd cluster deploy $ob_name -c $HOME/oceanbase/tools/deploy/config.yaml -f && $obd cluster start $ob_name -f && $obd cluster display $ob_name
    else
        $obd cluster redeploy $ob_name -f
    fi
    retries=`expr $retries - 1`
    ./obclient -h 127.1 -P $MYSQL_PORT -u root -A -e "alter system set_tp tp_no = 509, error_code = 4016, frequency = 1;"
    ret=$?
    if [[ $ret == 0 ]]
    then
        init_files=('init.sql' 'init_mini.sql' 'init_for_ce.sql')
        for init_file in ${init_files[*]}
        do
            if [[ -f $HOME/oceanbase/tools/deploy/$init_file ]]
            then
                if ! grep "alter system set_tp tp_no = 509, error_code = 4016, frequency = 1;" $HOME/oceanbase/tools/deploy/$init_file
                then
                    echo -e "\nalter system set_tp tp_no = 509, error_code = 4016, frequency = 1;" >> $HOME/oceanbase/tools/deploy/$init_file
                fi
            fi
        done
    fi
    $obd test mysqltest $ob_name $SERVER_ARGS --mysqltest-bin=./mysqltest --obclient-bin=./obclient --init-only --init-sql-dir=$HOME/oceanbase/tools/deploy $INIT_FLIES --log-dir=$HOME/init_case $EXTRA_ARGS -v > ./init_sql_log && init_seccess=1 && break
    done
}

function is_case_selector_arg()
{
    local arg
    arg=$1
    case_selector_patterns=('test-set=*' 'testset=*' 'suite=*' 'tag=*' 'regress_suite=*' 'all' 'psmall')
    for p in ${case_selector_patterns[@]}
    do
      [[ "$(echo $arg | grep -Eo $p)" != "" ]] && return 0
    done
    return 1
}

function obd_run_mysqltest {
    obd_prepare_global

    cd $HOME/oceanbase/tools/deploy
    if [[ "$SLB" != "" ]] && [[ "$EXECID" != "" ]]
    then
        SCHE_ARGS="--slb-host=$SLB --exec-id=$EXECID "
    else
        SCHE_ARGS="--slices=$SLICES --slice-idx=$SLICE_IDX "
    fi
    
    
    if [[ "$MINI" == "1" && -f core-test.init_mini.sql ]]
    then
        INIT_FLIES="--init-sql-files=core-test.init_mini.sql,init_user.sql|root@mysql|test"
        [ -f init_user_oracle.sql ] && INIT_FLIES="${INIT_FLIES},init_user_oracle.sql|SYS@oracle|SYS"
    else
        if [[ "$IS_CE" == "1" && -f $HOME/oceanbase/.ce ]]
        then
            INIT_FLIES="--init-sql-files=init_mini.sql,init_user.sql|root@mysql|test "
        fi
    fi
    CLUSTER_MODE="c"
    if [[ "$SLAVE" == "1" ]]
    then
    SERVER_ARGS="--test-server=server2"
    CLUSTER_MODE="slave"
    else
    SERVER_ARGS=""
    fi
    if [ "$WITH_PROXY" ] && [ "$WITH_PROXY" != "0" ]
    then
    CLUSTER_MODE="proxy"
    fi
    reboot_retries=2
    if [[ "$WITH_MYSQLTEST_CONFIG_YAML" == "1" ]]
    then
        EXTRA_ARGS="--test-mode=$CLUSTER_MODE "
    else
        EXTRA_ARGS="--cluster-mode=$CLUSTER_MODE "
    fi
    if [[  -f $HOME/oceanbase/tools/deploy/obd/.fast-reboot ]]
    then
        EXTRA_ARGS="${EXTRA_ARGS}--fast-reboot "
    fi 

    if [[ "$IS_CE" == "1" ]]
    then
        EXTRA_ARGS="${EXTRA_ARGS}--mode=mysql "
    fi

    EXTRA_ARGS_WITHOUT_CASE=$EXTRA_ARGS
    
    if [[ "$ARGV" != "" ]]
    then
        for arg in $ARGV
        do  
            [[ "${arg%%=*}" == "testset" ]] && arg="${arg/testset=/test-set=}"
            [[ "${arg%%=*}" == "reboot-timeout" ]] && has_reboot_timeout=1
            [[ "${arg%%=*}" == "reboot-retries" ]] && reboot_retries=${arg#*=}
            [[ "${arg%%=*}" == "disable-collect" ]] && disable_collect=1 && continue
            [[ "${arg%%=*}" == "use-cgroup" ]] && continue
            EXTRA_ARGS="${EXTRA_ARGS}--${arg} "
            if ! is_case_selector_arg "${arg}"
            then
              EXTRA_ARGS_WITHOUT_CASE="${EXTRA_ARGS_WITHOUT_CASE}--${arg} "
            fi
        done
    fi

    if [[ "$has_reboot_timeout" != "1" ]]
    then
        REBOOT_TIMEOUT="--reboot-timeout=600"
    fi
    if [[ "$disable_collect" != "1" ]]
    then
        COLLECT_ARG="--collect-all"
        if [[ "$WITH_MYSQLTEST_CONFIG_YAML" == "0" ]] && [ "$WITH_PROXY" ] && [ "$WITH_PROXY" != "0" ] && [[ -f "$HOME/oceanbase/tools/deploy/obd/.collect_proxy_log" ]]
        then
            COLLECT_ARG="$COLLECT_ARG --collect-components=$COMPONENT,obproxy"
        fi
    fi
    obd_init_cluster
    if [[ "$init_seccess" != "1" ]]
    then
        cat ./init_sql_log
        echo "Failed to init sql, see more log at ./collected_log/_test_init"
        mkdir -p $HOME/collected_log/_test_init/
        [[ -d "$DATA_PATH/observer1/log" ]] && mkdir -p $HOME/collected_log/_test_init/observer1 && mv $DATA_PATH/observer1/log/* $HOME/collected_log/_test_init/observer1
        [[ -d "$DATA_PATH/observer2/log" ]] && mkdir -p $HOME/collected_log/_test_init/observer2 && mv $DATA_PATH/observer2/log/* $HOME/collected_log/_test_init/observer2
        [[ -d "$DATA_PATH/obproxy/log" ]] && mkdir -p $HOME/collected_log/_test_init/obproxy && mv $DATA_PATH/obproxy/log/* $HOME/collected_log/_test_init/obproxy
        exit 1
    fi 
    mysqltest_cmd="$obd test mysqltest $ob_name $SERVER_ARGS --mysqltest-bin=./mysqltest --obclient-bin=./obclient $COLLECT_ARG --init-sql-dir=$HOME/oceanbase/tools/deploy --log-dir=./var/log $REBOOT_TIMEOUT $VERBOSE_ARG $EXTRA_ARGS"
    $mysqltest_cmd $RUN_EXTRA_CHECK_CMD $INIT_FLIES $SCHE_ARGS 2>&1 | tee compare.out && ( exit ${PIPESTATUS[0]})
    ret=$?
    if [[ $JOBNAME == 'mysqltest_opensource' ]]
    then
        submarker="_opensource"
    else
        submarker=""
    fi
    mv compare.out $HOME/mysqltest${submarker}_compare_output.$SLICE_IDX
    echo "finish!"
    return $ret
}

function obd_prepare_env {
    obd_prepare_global
    obd_prepare_obd
    obd_prepare_config
    obd_prepare_bin
}

function obd_collect_log {
    
    echo "collect log"
    cd $HOME/
    mkdir -p collected_log
    mkdir -p collected_log/obd_log
    mkdir -p collected_log/mysqltest_log
    mkdir -p collected_log/mysqltest_rec_log
    find $DATA_PATH -name 'core[.-]*' | xargs -i cp {} collected_log
    [[ "$OBD_HOME" != "" ]] && mv $OBD_HOME/.obd/log/*  collected_log/obd_log/
    mv oceanbase/tools/deploy/var/log/* collected_log/mysqltest_log/
    mv oceanbase/tools/deploy/var/rec_log/* collected_log/mysqltest_rec_log/
    mv collected_log collected_log_$SLICE_IDX
    tar zcvf collected_log_$SLICE_IDX.tar.gz collected_log_$SLICE_IDX
}

function collect_obd_case_log {
    
    echo "collect obd case log"
    cd $HOME/
    if [[ $JOBNAME == 'mysqltest_opensource' ]]
    then
        submarker="_opensource"
    else
        submarker=""
    fi
    [[ "$OBD_HOME" != "" ]] && cat $OBD_HOME/.obd/log/* | grep '\[INFO\]' > mysqltest_cases_log${submarker}_$SLICE_IDX.output

}


export -f obd_prepare_global
export -f obd_prepare_config
export -f obd_run_mysqltest
export -f obd_init_cluster
export -f is_case_selector_arg

function run {
    set +x
    if ([[ -f $HOME/oceanbase/rpm/oceanbase.deps ]] && [[ "$(grep 'ob-deploy' $HOME/oceanbase/rpm/oceanbase.deps )" != "" ]]) || 
      ([[ -f $HOME/oceanbase/deps/3rd/oceanbase.el7.x86_64.deps ]] && [[ "$(grep 'ob-deploy' $HOME/oceanbase/deps/3rd/oceanbase.el7.x86_64.deps )" != "" ]]) ||
      ([[ -f $HOME/oceanbase/deps/init/oceanbase.el7.x86_64.deps ]] && [[ "$(grep 'ob-deploy' $HOME/oceanbase/deps/init/oceanbase.el7.x86_64.deps )" != "" ]])
    then
        timeout=18000
        [[ "$SPECIAL_RUN" == "1" ]] && timeout=72000
        obd_prepare_env
        prepare_result=$?
        if [[ $prepare_result -ne 0 ]]
        then
            obd_collect_log
            return $prepare_result
        fi
        timeout $timeout bash -c "obd_run_mysqltest" 
        test_ret=$?
        error_log_ret=0
        if [[ -f $HOME/oceanbase/tools/deploy/error_log_filter.json && ( $BRANCH == 'master' || $BRANCH == "4_2_x_release" ) ]]
        then
            # notice the core
            # python $HOME/common/analyse_the_observer_core.py collect -p ${DATA_PATH} -j ${JOBNAME}.${SLICE_IDX} -o $HOME
            error_log_ret=0
            # collect_obd_case_log
        fi
        if [[ $test_ret -ne 0 || $error_log_ret -ne 0  ]]
        then
            obd_collect_log
        fi
        return `[[ $test_ret = 0 && $error_log_ret = 0 ]]` 
    else
        prepare_env &&
        timeout 18000 bash -c "run_mysqltest"   
        test_ret=$?      
        error_log_ret=0
        if [[ -f $HOME/oceanbase/tools/deploy/error_log_filter.json && ( $BRANCH == 'master' || $BRANCH == "4_2_x_release" ) ]]
        then
            python $HOME/common/analyse_the_observer_log.py collect -p ${DATA_PATH}/observer1/log  -p ${DATA_PATH}/observer2/log -i $GID -j ${JOBNAME}.${SLICE_IDX} -o $HOME -F $HOME/oceanbase/tools/deploy/error_log_filter.json
            error_log_ret=0
        fi
        if [[ $test_ret -ne 0 || $error_log_ret -ne 0  ]]
        then
            collect_log
        fi
        return `[[ $test_ret = 0 && $error_log_ret = 0 ]]`
    fi
    
}

source $HOME/scripts/frame.sh && main