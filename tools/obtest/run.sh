#!/bin/bash

workdir=/data/0/
clog_path=/data/0/
data_path=/data/1/
log_path=/data/1/
dev_name=eth0
backup_dir=/data/0/backup

test_module="backup"
OBSERVER_BIN=""
COMMIT_ID=""
BRANCH_NAME=""
ADMIN_PKG=""
job_dir=""
test_cases=""
deploy_mode="farm"
run_mode="x86"
MYTEST_DIR=""
OB_ADMIN_DIR=""
case_type=""
alarm_url=""
storage_file=""
function usage() {

    echo "Usage: $0 [options]"
    echo "Options:"
    echo " -h, --help                     Display this help message"
    echo " --ob_admin ob_admin包链接       支持提供一个ob_admin的远程链接下载地址"
    echo " --mytest mytest.jar包链接       支持提供一个mytest.jar的远程链接下载地址"
    echo " -r, --run                      选择运行模式，支持local和farm2种，默认是farm运行"
    echo " --alarm_url                    发送执行结果到指定钉钉群中, 需要提供指定群机器人的url"
    echo " -s, --cases                    设置执行的case集合， 若-s、-t2个参数都未指定,默认执行测试全集"
    echo " --observer <OBSERVER>          observer binary链接，可以是一个远端http链接，也可以是一个本地文件绝对路径"
    echo " --storage_file                 执行git仓库的case集合，提供git仓库中可执行的测试集合的文件目录，比如storage_farm/storage_ha_sn.list"
    echo " -d, --deploy                   选择部署机器的类型，支持 x86(部署在x86机器上)和arm(x86备>份、arm恢复模式) 2种，默认为x86"
    echo " --storage_mode                 设置observer 备份恢复存储介质模式，采用0、1、2、3分别代表oss、nfs(本地文件目录)、cos、obs，默认值是nfs"
    echo " --server-type，                是否选择errsim测试，可以跟----compile_type联用，也可以跟--observer 手动上传errsim binary联用,值为errsim时，执行errsim case，否则执行普通版本case "
    exit 1
}

function validate_parameters() {

    if [[ -z "$OBSERVER" ]]; then
        echo "Error: --observer is required."
        usage
    fi

}

function argument_parse() {

    # Parse command line options
    while [[ $# -gt 0 ]]; do
    case "$1" in
        -h|--help)
        usage
        ;;
        -f|--function)
        test_module="$2"
        shift 2
        ;;
        -c|--commit)
        COMMIT_ID="$2"
        shift 2
        ;;
        -b|--branch)
        BRANCH_NAME="$2"
        shift 2
        ;;
        -s|--cases)
        test_cases="$2"
        shift 2
        ;;
        -d|--deploy)
        run_mode="$2"
        shift 2
        ;;
        -r|--run)
        deploy_mode="$2"
        shift 2
        ;;
        --dev_name)
        dev_name="$2"
        shift 2
        ;;
        --observer)
            if [[ -n "$2" && ! "$2" =~ ^-- ]]; then
                OBSERVER_BIN="$2"
                shift 2
            else
                echo "Error: --observer requires a value."
                exit 1
            fi
        ;;
        --obproxy)
        OBPROXY_BIN="$2"
        shift 2
        ;;
        --admin)
        ADMIN_PKG="$2"
        shift 2
        ;;
        --ob_admin)
        OB_ADMIN_DIR="$2"
        shift 2
        ;;
        --server-type)
        case_type="$2"
        shift 2
        ;;
        --mytest)
        MYTEST_DIR="$2"
        shift 2
        ;;
        --alarm_url)
        alarm_url="$2"
        shift 2
        ;;
        --storage_file)
        storage_file="$2"
        shift 2
        ;;
        *)
        # 将未知选项添加到未知参数列表中
        unknown_args+=("$1")
        shift
        ;;
    esac
    done

    echo "parse end"

    job_dir=`pwd`
}

function software_precheck() {

    echo "检查测试依赖.. "

    # check Java
    if ! command -v java &> /dev/null; then
        echo "Java 没有安装，请运行以下命令安装:"
        echo "sudo yum install -y java"
        exit 1
    else
        echo "OK - Java 已安装。"
    fi


}

function disk_capacity_check() {

    disk_path=$1
    required_disksize=$2
    disk_type=$3

    disk_left_space=$(df -BG "$disk_path" | awk 'NR==2 {print $4}' | sed 's/G//')
    required_disksize=$(echo $required_disksize | sed 's/[gG]//')

    if [ $required_disksize -lt $disk_left_space ]; then
        echo "OK - $disk_type 空间充足, 磁盘位置: $disk_path, 需要空间: $required_disksize GB, 空闲空间: $disk_left_space GB"
    else
        echo "FAIL - $disk_type 空间不足, 磁盘位置: $disk_path, 需要空间: $required_disksize GB, 空闲空间: $disk_left_space GB"
        exit 1
    fi
}

function cpu_capacity_check() {

    required_cpu=$1
    available_cpu=$(nproc)

    if [ $available_cpu -ge $required_cpu ]; then
        echo "OK - cpu 资源充足, 需要cpu num: $required_cpu, 可用cpu num: $available_cpu"
    else
        oversold_percentage=$(echo "scale=2; ($required_cpu / $available_cpu) * 100" | bc)
        echo "WARN - 当前节点 cpu 资源不够, 存在超卖, 需要cpu num: $required_cpu, 可用cpu num: $available_cpu, 超卖程度: $oversold_percentage %"
    fi
}

function mem_capacity_check() {

    required_memory_gb=$1
    required_memory_gb=$(echo $required_memory_gb | sed 's/[gG]//')

    # 获取当前机器的可用内存（以GB为单位）
    available_memory_gb=$(free -g | awk '/^Mem:/ {print $7}')

    # 检查是否满足要求
    if [ $available_memory_gb -ge $required_memory_gb ]; then
        echo "OK - 内存资源充足, 需要内存: $required_memory_gb GB, 可用内存: $available_memory_gb GB"
    else
        echo "FAIL - 内存资源不足, 需要内存: $required_memory_gb GB, 可用内存: $available_memory_gb GB"
        exit 1
    fi
}

function hardware_precheck() {

    # check backup/restore dir existence
    if [ ! -d "$backup_dir" ]; then
        echo "FAIL - 备份介质目录 backup_dir: $backup_dir 不存在。请运行以下命令创建文件夹:"
        echo "  mkdir -p $backup_dir"
        exit 1  # 退出脚本，返回状态码1
    else
        echo "OK - 'backup_dir: $backup_dir 存在"
    fi

    # check backup_dir not equal to any other dir (testscript will cleanup this dir before test)
    if [[ "$backup_dir" -ef "$workdir" ]]   || \
       [[ "$backup_dir" -ef "$clog_path" ]] || \
       [[ "$backup_dir" -ef "$data_path" ]] || \
       [[ "$backup_dir" -ef "$log_path" ]]; then
        echo "FAIL - 备份介质目录 backup_dir: $backup_dir 不可以跟 observer部署/data/clog/workdir 相同, 测试脚本会清理backup_dir中所有数据，请调整backup_dir地址"
        exit 1
    fi
    echo "OK - '备份介质目录 backup_dir: $backup_dir 和observer工作目录不可以放在一起，检查通过"

    # check backup/restore dir empty
    if [ -z "$(ls -A $backup_dir)" ]; then
        echo "OK - 'backup_dir: $backup_dir 为空文件夹"
    else
        echo "WARN - 备份介质目录 backup_dir: $backup_dir 有数据存在，数据将被清空"
    fi

    # check sstable dir disk space enough，FIXME: 1080G是 执行脚本中写死的值。这里有优化的地方
    disk_capacity_check $data_path "80G" "sstable"

    # check clog dir disk space enough
    disk_capacity_check $clog_path "50G" "clog"

    # check backup dir disk space enough
    backup_device=$(df -P $backup_dir | awk 'NR==2{print $1}')
    clog_device=$(df -P $clog_path | awk 'NR==2{print $1}')
    data_device=$(df -P $data_path | awk 'NR==2{print $1}')

    if [ $backup_device == $clog_device ]; then
        # 备份目录和clog放在一个盘，需要空间 288G + (180*2)G = 648G
        disk_capacity_check $backup_dir "300G" "backup"
    elif [ $backup_device == $data_device ]; then
        # 备份目录和sstable放在一个盘，需要空间 1080G + (180*2)G = 1440G
        disk_capacity_check $backup_dir "300G" "backup"
    else
        # 备份目录单独放置
        disk_capacity_check $backup_dir "5G" "backup"
    fi

    # check cpu num enough
    cpu_capacity_check 36

    # check memory enough
    mem_capacity_check "144G"

}

function test_precheck() {

    software_precheck

}


function test_machine() {
    
    echo "test_machine, start top collect"
    top -b -d 5 > $OCEANBASE_DIR/tools/obtest/top_output.log &

}
function observer_io_check() {

    target_dir=$OCEANBASE_DIR/tools/obtest/collected_log
    echo "start observer_io_check"

    if [  -d "$target_dir" ]; then
        # 递归查找所有叶子目录（没有子目录的目录）
        find "$target_dir" -type d | while read dir; do
           # 检查是否是叶子目录（目录中没有子目录）
           if [ -z "$(find "$dir" -mindepth 1 -maxdepth 1 -type d 2>/dev/null)" ]; then
               # 进入叶子目录执行 grep，结果输出到该目录的 io_status_function.log和io_status_group.log
                cd "$dir" || continue
                grep -rnI "STATUS FUNCTION" *.log | sort | uniq > io_status_function.log
                grep -rnI "STATUS FUNCTION" *.log.* | sort | uniq >> io_status_function.log
                grep -rnI "STATUS GROUP" *.log | sort | uniq > io_status_group.log
                grep -rnI "STATUS GROUP" *.log.* | sort | uniq >> io_status_group.log
           fi
        done
        
    else 
        echo "collected_log 目录不存在,不进行observer io status日志收集"
    fi

    cd $OCEANBASE_DIR/tools/obtest

}

function update_binary() {

    echo "更新observer binary and mytest.jar and ob_admin..."
    cd $OCEANBASE_DIR/tools/obtest
    mkdir -p bin etc
    # 更新observer binary
    if [[ $OBSERVER_BIN =~ ^http:// ]] || [[ $OBSERVER_BIN =~ ^https:// ]]; then
        echo "downloading observer binary from $OBSERVER..."
        wget $OBSERVER_BIN -o ./wget_observer.log -O bin/observer && chmod +x bin/observer
        bin/observer -V 2>&1 | tee ./observer.version

    elif [[ -n $OBSERVER ]]; then
        echo "从本地copy observer binary.."
        cp -rf $OBSERVER bin/observer && chmod +x bin/observer
        bin/observer -V 2>&1 | tee ./observer.version
    else 
       echo "observer not exist,please use -c or --observer to specify it" 
    fi
    
   # 更新mytest.jar
   if [[ $MYTEST_DIR =~ ^http:// ]] || [[ $MYTEST_DIR =~ ^https:// ]]; then
        echo "downloading mytest.jar from $MYTEST_DIR..."
        wget $MYTEST_DIR -o ./wget_mytestjar.log -O jar/mytest.jar && chmod +x jar/mytest.jar
   else 
        echo "downloading mytest_latest.jar from pkg"
        #wget http://11.166.86.153:8877/mytest_latest.jar -o ./wget_mytestjar.log -O jar/mytest.jar && chmod +x jar/mytest.jar
        mkdir -p mytest_jar && cd mytest_jar
        curl "http://11.166.86.153:8877/pkg/downloader.sh" | bash -s -- --pkg_name mytest --pkg_type tool
        tar -xvzf mytest.tar.gz
        cp -r mytest/mytest.jar $OCEANBASE_DIR/tools/obtest/jar/mytest.jar
        cd $OCEANBASE_DIR/tools/obtest
        chmod +x jar/mytest.jar
   fi
   # 更新ob_admin
   if [[ $OB_ADMIN_DIR =~ ^http:// ]] || [[ $OB_ADMIN_DIR =~ ^https:// ]]; then
        echo "downloading ob_admin from $OB_ADMIN_DIR..."
        wget $OB_ADMIN_DIR -o ./wget_obadmin.log -O bin/ob_admin && chmod +x bin/ob_admin
        cp -r bin/ob_admin etc/ob_admin
   fi
  # farm 更新 admin
   admin_dir=$HOME/admin
   if [ "$(ls -A $admin_dir)" ]; then
        echo "get admin from $HOME/admin..."
        cp -r $HOME/admin $OCEANBASE_DIR/tools/obtest/
   fi
}

function update_test_config() {

    echo "更新obtest配置文件.."

    cp -rf conf/configure_temp.ini conf/configure.ini

    h=`hostname -i`
    
    #sed -i "s#^workdir.*#workdir=$workdir#g" conf/configure.ini
    #sed -i "s#^clog_path.*#clog_path=$clog_path#g" conf/configure.ini
    #sed -i "s#^data_path.*#data_path=$data_path#g" conf/configure.ini
    #sed -i "s#^log_path.*#log_path=$log_path#g" conf/configure.ini
    #sed -i "s#^dev.*#dev=$dev_name#g" conf/configure.ini
    #sed -i "s#^file_dir.*#file_dir=$backup_dir#g" conf/configure.ini
    
    #替换configure.ini配置
    sed -i "s#HOME_PATH#$HOME#g" conf/configure.ini
    sed -i "s#HOST#$HOST_IP#g" conf/configure.ini
    sed -i "s#PORT_BASE#$PORT_BASE#g" conf/configure.ini
    sed -i "s#devport#$ETHERNET#g" conf/configure.ini
    mkdir -p $HOME/obtest_data
    mkdir -p $HOME/obtest_data/workdir
    mkdir -p $HOME/obtest_data/datadir
    
    mkdir -p $HOME/obtest_data/obtest_backup
    mkdir -p $HOME/obtest_data/obtest_backup_backup
    
}

function test_init() {
    
    test_machine
    update_binary
    update_test_config

}

function test_run_backup() {
    
    host_ip=`hostname -i`
    
    bash ./copy.sh
    
    #cp -r $OBSERVER bin/observer
    #执行测试
    bash ./backAndDeleteLog.sh
    if [ ! -n "$test_cases" ]; then 
       if [ "$case_type" = "errsim" ]; then
          find t/errsim_storage_ha -name '*.test' >testset_temp.list
          find t/errsim_storage_ha_sn_only -name '*.test' >testset_storage.list
          cat testset_storage.list >> testset_temp.list
       else 
          find t/storage_ha -name '*.test' >testset_temp.list
          find t/storage_ha_sn_only -name '*.test' >testset_storage.list
          cat testset_storage.list >> testset_temp.list
       fi
    else
       echo $test_cases > testset_temp.list 
    fi
    if [  -e black_test.list ]; then
       sh exclude_test.sh testset_temp.list black_test.list > testset.list
    else
       cp -r testset_temp.list testset.list
    fi
    #判断是否生成了总的可执行的测试集合，不存在testset.list的话，说明前面的测试集合获取有问题
    if [ ! -s "testset.list" ]; then
      echo "error! there is no case to run"
      exit 1
    fi
    #判断是否使用--storage_file执行线上case集合，--storage_file指定测试case优先级高于 --cases（-s）或者 -t
    if [[ -f $storage_file ]]; then
      cat $storage_file > storage_temp.list
      if  grep -q ".test" storage_temp.list; then
         cat storage_temp.list > testset.list
      else
         echo "$storage_file文件格式不准确，将执行其他指定或者默认测试集"
      fi
    
    fi
    #调整测试集格式,将farm_test.list文件中的每一行末尾的换行符替换为逗号
    sed -i ':a;N;$!ba;s/\n/,/g' testset.list
    #进行case并发处理
    sh $HOME/scripts/split_cases.sh
    #判断子任务下是否存在可执行的测试集合,不存在则退出
    if ! grep -q ".test" farm_test.list; then 
      echo "info: there is no task to run"
      exit 0
    fi
    #调整测试集格式,将farm_test.list文件中的每一行末尾的换行符替换为逗号
    sed -i ':a;N;$!ba;s/\n/,/g' farm_test.list
    
    ./obtest -t farm_test.list|tee mytestlog_storage_ha

    #检查observer io
    observer_io_check
    
    #case执行失败时的日志回收处理
    if grep -qE "Fatal error|vimdiff" mytestlog_storage_ha; then
       cp -r collected_log $CASE_COLLECT_LOG_DIR/collected_log
       cp -r mytestlog_storage_ha $CASE_COLLECT_LOG_DIR/collected_log/
       cp -r log $CASE_COLLECT_LOG_DIR/collected_log/
       cp -r bin/observer $CASE_COLLECT_LOG_DIR/collected_log/
       cp -r farm_test.list $CASE_COLLECT_LOG_DIR/collected_log/
       cp -r bin/ob_admin $CASE_COLLECT_LOG_DIR/collected_log/  
       cp -r top_output.log $CASE_COLLECT_LOG_DIR/collected_log/  
       exit 5
    fi
    

}

function test_run_upgrade() {

    host_ip=`hostname -i`
    sed  -i  '/mytest_latest.jar/s/^/#/' copy_for_compat.sh
    observer_branch=`cat observer.version |grep BUILD_BRANCH |awk -F ' ' '{printf $2}'`
    if [[ "$observer_branch" == "4_3_5_release" ]]; then
      cp t/compat/test_temp.sh test.sh
      cp t/compat/for_sql_temp.sh for_sql.sh
      cp t/compat/for_sql_ss_temp.sh for_sql_ss.sh
    else
      touch test.sh for_sql.sh
    fi
    sh t/compat/prepare_for_packet.sh
    #clog盘设置过大，升级的主备库环境会reboot失败，需要调小
    #sed -i "s#log_disk_size=150G#log_disk_size=100G#g" conf/configure.ini
    bash ./copy.sh
    if [ ! -d "bin" ]; then
        mkdir bin
    fi
    cp -r $OBSERVER bin/observer
    bash ./backAndDeleteLog.sh

    if [ ! -n "$test_cases" ]; then
       find t/compat -name 'farm*_from_4_*.test' >testset_temp.list
    else
       echo $test_cases > testset_temp.list
    fi
	sh exclude_test.sh testset_temp.list black_test_upgrade.list > testset.list
    #判断是否生成了总的可执行的测试集合，不存在testset.list的话，说明前面的测试集合获取有问题
    if [ ! -s "testset.list" ]; then
      echo "error! there is no case to run"
      exit 1
    fi
    #调整测试集格式,将farm_test.list文件中的每一行末尾的换行符替换为逗号
    sed -i ':a;N;$!ba;s/\n/,/g' testset.list
    sh $HOME/scripts/split_cases.sh
    #判断子任务下是否存在可执行的测试集合,不存在则退出
    if ! grep -q ".test" farm_test.list; then 
      echo "info: there is no task to run"
      exit 0
    fi
    #调整测试集格式,将farm_test.list文件中的每一行末尾的换行符替换为逗号
    sed -i ':a;N;$!ba;s/\n/,/g' farm_test.list
    ./obtest -t farm_test.list|tee mytestlog_upgrade
    #收集obguard日志
    sh t/compat/obguard_collect/show_obguard.sh|tee obguard_log
    if grep -q "FAIL" mytestlog_upgrade; then
       cp -r collected_log $CASE_COLLECT_LOG_DIR/collected_log
       cp -r mytestlog_upgrade $CASE_COLLECT_LOG_DIR/collected_log/
       cp -r log $CASE_COLLECT_LOG_DIR/collected_log/
       cp -r bin/observer $CASE_COLLECT_LOG_DIR/collected_log/
       cp -r farm_test.list $CASE_COLLECT_LOG_DIR/collected_log/
       cp -r top_output.log $CASE_COLLECT_LOG_DIR/collected_log/  
       exit 5
    fi

}
function main() {

    argument_parse "$@"
   
    test_precheck
    if [ $deploy_mode == 'farm' ] ; then
        cd $OCEANBASE_DIR/tools/obtest
        echo "you are using farm mode..."
    elif [ $deploy_mode == 'local' ] ; then
        job_dir=`pwd`
        #$job_dir | grep -q '/oceanbase/tools/obtest$' && echo "you are using local mode..." || (echo "you are not in oceanbase/tools/obtest,please download the codebase and enter the exact directory" && exit 1)
        target_dir=$(echo $job_dir | rev | cut -d'/' -f1-3 | rev)
        if [ "$target_dir" != "oceanbase/tools/obtest" ]; then
           echo "you are not in oceanbase/tools/obtest,please download the codebase and enter the exact directory"
           exit 1
        fi
        echo "you are using local mode..."
        OCEANBASE_DIR=$(echo $job_dir | sed "s#$target_dir##")
    else 
        echo "please use -r to choose local or farm mode"
        exit 1            
    fi
    

    test_init
    
    if [ $test_module == "backup" ] ; then
        test_run_backup
    fi 
    if [ $test_module == "upgrade" ] ; then
        test_run_upgrade
    fi

    if [ $? -ne 0 ];then
      exit 1
    fi

}

main "$@"
