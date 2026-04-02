### predefined variables
# HOME    - job home directory
# COMMIT  - commit our operations are based on
# BRANCH  - branch out operations are based on
# PATCH   - file name of patch need to patch
# REPO    - repository that the job is executing in
# JOBNAME - job name
# 自 farm-jenkins/scripts/opensource/scripts/frame.sh 复制并改造：/etc/hosts 与 dep_cache 在无权限时跳过
set +x

function __execute_job__(){
    init && prepare && run && clean
}

function main()
{
    __execute_job__
    ret=$?
    return $ret
}

function clean_dir(){
    for file in `ls $1`
    do
        if [ -d $1"/"$file ]
        then
            clean_dir $1"/"$file
        fi
    done
    rmdir $1
}


function init()
{
    ulimit -s 10240
    ulimit -c unlimited
    ulimit -n 655350
    user=$(whoami)
    if [ -f /var/clone/clone_info ]; then
        sn=`cat /var/clone/clone_info | grep -Po '"sn": "(.*)",' | awk -F': "' '{print $2}'`
        sn=${sn%%\",}
    else
        sn=$(dmidecode -t1 2>/dev/null | egrep "Serial Number" | awk -F':' '{print $2}' | sed 's/ //g')
    fi
    if [ "X$sn" == "X" ]; then
        ip=$(hostname -i 2>/dev/null || echo "127.0.0.1")
        OB_SM_CW_TAG="$ip@$user"
    else
        OB_SM_CW_TAG="$sn@$user"
    fi
    [[ "$user" == cwork* ]] && { killall -u "$user" -9 observer 2>/dev/null; killall -u "$user" -9 obproxy 2>/dev/null; killall -u "$user" -9 ofsserver 2>/dev/null; true; }
    [[ "$user" == cwork* ]] && [[ -d /sys/fs/cgroup/cpu/$user/oceanbase ]] && clean_dir /sys/fs/cgroup/cpu/$user/oceanbase 2>/dev/null || true
    export DEP_CACHE_DIR="${DEP_CACHE_DIR:-$HOME/../../dep_cache}"
    return 0
}


function prepare()
{
    set +x
    # 仅在有 FORWARDING_HOST 或可写 /etc/hosts 时追加（native runner 可能无权限）
    if [[ -n "${FORWARDING_HOST:-}" ]] && [[ -w /etc/hosts ]]; then
        cat >> /etc/hosts <<EOF
${FORWARDING_HOST} github.com
${FORWARDING_HOST} mirrors.aliyun.com
${FORWARDING_HOST} maven.aliyun.com
EOF
    elif [[ -w /etc/hosts ]]; then
        cat >> /etc/hosts <<EOF 2>/dev/null || true
172.16.0.220 github.com
172.16.0.220 mirrors.aliyun.com
172.16.0.220 maven.aliyun.com
EOF
    fi
    export PATH=/var/lib/condor/bin:/bin:/usr/local/bin:/usr/bin:/sbin
    [[ -f /etc/profile.d/dep_create.sh ]] && {
        source /etc/profile.d/dep_create.sh
        [[ -f $(dirname ${BASH_SOURCE[0]})/dep_cache.sh ]] && source $(dirname ${BASH_SOURCE[0]})/dep_cache.sh
        export DEP_CACHE_DIR=$(readlink -f $HOME/../../dep_cache 2>/dev/null || echo "$DEP_CACHE_DIR")
        mkdir -p $DEP_CACHE_DIR || return 1
    }

    if [ "$REPO" == "server" ]
    then
        REPONAME="oceanbase"
    fi

    git config --global gc.auto 0
    git config --global user.email "$(whoami)@$HOSTNAME"
    git config --global user.name "$(whoami)"

    # prepare oceanbase repository: HOME/../.. 下 clone 或更新 oceanbase
    cd $HOME
    (
        cd ../..
        if [ -d $REPONAME ]
        then
            cd $REPONAME &&
                rm -f .git/index.lock .git/packed-refs.lock .git/ORIG_HEAD.lock .git/refs/heads/master.lock .git/refs/heads/test.lock .git/refs/heads/test_cross_validatition.lock .git/refs/remotes/origin/*.lock .git/refs/remotes/origin/{issue,task,req}/*.lock 2>/dev/null
                git reset --hard &&
                git clean -dxff || return 1
                git submodule deinit -f .
                git submodule foreach rm -f .git/index.lock .git/ORIG_HEAD.lock .git/refs/heads/master.lock 2>/dev/null
                git submodule foreach 'ls .git && git reset --hard || echo'
                git submodule foreach 'ls .git && git clean -dxff || echo'
                git checkout master &&
                git reset --hard origin/master &&
                git clean -dxff &&
                ( git pull --all || git pull --all || git pull --all || return 8 )
                git remote prune origin
        else
            current_branch_name=${CURRENT_BRANCH:-master}
            max_retries=5
            retry=0
            while [ $retry -lt $max_retries ]; do
                git clone --depth 3 $CODE_URL --branch $current_branch_name -v && break
                ((retry++))
                echo "Clone failed. Retrying..."
            done
            if [ $retry -eq $max_retries ]; then
                echo "Max retries reached. Clone failed."
                return 2
            fi
            current_reponame=$(basename "$CODE_URL" .git)
            if [[ ! $current_reponame == "oceanbase" ]]
            then
                mv $current_reponame $REPONAME
            fi
        fi
    ) && ln -sfn ../../$REPONAME $HOME/oceanbase || return 1

    # prepare oceanbase environment
    cd $HOME/oceanbase
    if [ "$MRID" != "" ]
    then
        git fetch origin merge-requests/$MRID/head:test && git checkout test
    elif [[ "$TARGET_HEAD" != "" ]]
    then
        git checkout $TARGET_BRANCH &&
        git reset --hard origin/$TARGET_BRANCH &&
        test $(git rev-parse HEAD) == "$TARGET_HEAD" || return 3
        git merge origin/$SOURCE_BRANCH --no-commit || (echo "[ABORT] 在执行合并时发生意料外的错误，任务终止"; git status -s; return 1) || return 3
    elif [ "$COMMIT" != "" ]
    then
        git branch -f test_branch $COMMIT && git checkout test_branch || return 2
    else
        if [ "$BRANCH" = "" ] || [ "$BRANCH" = "master" ]
        then
            BRANCH=origin/master
        fi
        if [[ "$BRANCH" != origin/* ]]
        then
            WHOLE_BRANCH=origin/$BRANCH
        else
            WHOLE_BRANCH=$BRANCH
        fi
        git branch -f test $WHOLE_BRANCH && git checkout test || return 2
    fi

    git clean -dxff
    git submodule init
    git submodule update
    git submodule foreach git clean -dxff

    if [ "$PATCH" != "" ] && [ -f $HOME/$PATCH ]
    then
        echo "apply patch $HOME/$PATCH"
        git apply --index $HOME/$PATCH &&
        git submodule update &&
        git add --all &&
        git commit -m 'compile commit' || return 3
    fi

    return 0
}


function clean()
{
    return 0
}

function errcho() { >&2 echo $@; }
