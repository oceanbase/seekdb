#!/usr/bin/env bash

# 检测当前 shell 类型
if [ -n "$ZSH_VERSION" ]; then
    SHELL_TYPE="zsh"
elif [ -n "$BASH_VERSION" ]; then
    SHELL_TYPE="bash"
else
    SHELL_TYPE="unknown"
fi

RELEASE="el7"

function version_ge() {
    local result
    result=$(awk -v v1="$VERSION_ID" -v v2="$1" 'BEGIN{print(v1>=v2)?"1":"0"}' 2>/dev/null)
    [ "$result" = "1" ]
}

function get_os_release()
{
    # macOS (Darwin) detection
    if [[ "$(uname -s)" == "Darwin" ]]; then
        local macos_ver
        macos_ver=$(sw_vers -productVersion 2>/dev/null)
        if [[ -z "$macos_ver" ]]; then
            echo "[ERROR] failed to get macOS version via sw_vers" 1>&2 && return 1
        fi
        local macos_major
        macos_major=$(echo "$macos_ver" | cut -d. -f1)
        if [[ "$macos_major" -ge 15 ]]; then
            RELEASE="macos15"
        else
            RELEASE="macos13"
        fi
        echo "os = macOS, version = $macos_ver, RELEASE = $RELEASE"
        return
    fi

    if [[ ! -f /etc/os-release ]]; then
        echo "[ERROR] os release info not found" 1>&2 && return 1
    fi
    # 兼容 bash 和 zsh 的 source 命令
    if [ "$SHELL_TYPE" = "zsh" ]; then
        setopt shwordsplit
    fi
    source /etc/os-release || return 1
    case "$ID" in
      alinux)
        version_ge "3.0" && RELEASE="al8" && return
        version_ge "2.1903" && RELEASE="el7" && return
      ;;
      alios)
        version_ge "8.0" && RELEASE="el8" && return
        version_ge "7.2" && RELEASE="el7" && return
      ;;
      ubuntu)
        version_ge "22.04" && RELEASE="el9" && return
        version_ge "16.04" && RELEASE="el7" && return
      ;;
    esac
    echo "os = $ID, version = $VERSION_ID, RELEASE = $RELEASE"

}



# 获取脚本所在目录，兼容 bash 和 zsh
if [ "$SHELL_TYPE" = "zsh" ]; then
    export OB_BASE_DIR=$(readlink -f "$(dirname ${(%):-%x})/../..")
else
    export OB_BASE_DIR=$(readlink -f "$(dirname ${BASH_SOURCE[0]})/../..")
fi
export OB_DO_GLOBAL_CONFIG=${OB_DO_GLOBAL_CONFIG:-~/.ob_do_global}
[[ -f $OB_DO_GLOBAL_CONFIG ]] && source $OB_DO_GLOBAL_CONFIG
export DEPLOY_PATH=$OB_BASE_DIR/tools/deploy
export OBD_FORCE_UPDATE_PLUGINS=1
if grep 'dep_create.sh' $OB_BASE_DIR/build.sh 2>&1 >/dev/null
then
    export DEP_PATH=$OB_BASE_DIR/deps/3rd
else
    export DEP_PATH=$OB_BASE_DIR/rpm/.dep_create/var
fi
export ARCHITECTURES=$(arch)
get_os_release || exit 1
export OBD_DEPS_PATH="$OB_BASE_DIR/deps/init/oceanbase.$RELEASE.$ARCHITECTURES.deps"
export OBD_BIN=${_OBD_BIN:-$DEP_PATH/usr/bin/obd}
export OBDO_CMD_BIN=${_OBDO_CMD_BIN:-$DEP_PATH/usr/bin/ob_do_cmd}
alias obd="${OBD_BIN}"
export OBD_HOME=${_OBD_HOME:-$DIR/tools/deploy}
export OBD_INSTALL_PRE=${_OBD_INSTALL_PRE:-$DEP_PATH} 
export OBD_PORT_GEN=$((100*($(id -u)%500)+10000))

if [ ${_OBD_PROFILE} ]; then
    source ${_OBD_PROFILE}
fi

if [ -f $OBD_INSTALL_PRE/etc/profile.d/obd.sh ] 
then
  source $OBD_INSTALL_PRE/etc/profile.d/obd.sh 
fi
if [[ "${OB_DO_NO_GLOBAL_CLUSTER:-0}" == "0" ]]; then
    obd_dev=${OBD_HOME_GLOBAL:-~/.obd_dev}
    mkdir -p $obd_dev/cluster
    mkdir -p $OBD_HOME/.obd
    [ ! -d $OBD_HOME/.obd/cluster ] && ln -fs $obd_dev/cluster $OBD_HOME/.obd
fi

# 将 OBD 安装目录下的 plugins/workflows/mirror 等链接到 .obd 运行时目录
# OBD 二进制在 $OBD_HOME/.obd/ 下查找这些资源，但实际安装在 $OBD_INSTALL_PRE/usr/obd/
_obd_res_src="$OBD_INSTALL_PRE/usr/obd"
_obd_res_dst="$OBD_HOME/.obd"
if [ -d "$_obd_res_src" ]; then
    mkdir -p "$_obd_res_dst"
    for _dir in plugins workflows mirror config_parser optimize; do
        if [ -d "$_obd_res_src/$_dir" ] && [ ! -L "$_obd_res_dst/$_dir" ]; then
            rm -rf "$_obd_res_dst/$_dir"
            ln -fs "$_obd_res_src/$_dir" "$_obd_res_dst/$_dir"
        fi
    done
    unset _dir
fi
unset _obd_res_src _obd_res_dst

# 设置自动补全，兼容 bash 和 zsh
# 注意：OBD 的补全功能已经通过 $OBD_INSTALL_PRE/etc/profile.d/obd.sh 加载
# 这里只需要确保在 zsh 中也能正常工作
if [ "$SHELL_TYPE" = "zsh" ]; then
    # zsh 自动补全设置
    autoload -U compinit && compinit
    # 如果存在 OBD 的补全函数，为 zsh 设置补全
    if command -v _obd_complete_func >/dev/null 2>&1; then
        compdef _obd_zsh_complete_func obd
    fi
fi

function _obd_sh_reply_deploy_names() {
    if [[ -d $OBD_HOME/.obd/cluster ]]
    then
    res=$(ls -p $OBD_HOME/.obd/cluster 2>/dev/null | sed "s#/##")
    if [ "$SHELL_TYPE" = "bash" ]; then
        COMPREPLY=( $(compgen -o filenames -W "${res}" -- ${cur}) )
    else
        # zsh 补全
        reply=($(echo $res))
    fi
    fi
}

function _obd_sh_reply_current_files() {
    filename=${cur##*/}
    dirname=${cur%*$filename}
    res=$(ls -a -p $dirname 2>/dev/null | sed "s#^#$dirname#")
    if [ "$SHELL_TYPE" = "bash" ]; then
        compopt -o nospace
        COMPREPLY=( $(compgen -o filenames -W "${res}" -- ${cur}) )
    else
        # zsh 补全
        reply=($(echo $res))
    fi
}

function _obd_sh_reply_yaml_files() {
    filename=${cur##*/}
    dirname=${cur%*$filename}
    res=$(ls -a -p $dirname 2>/dev/null | grep -E '(*.yaml|*.yml)' | sed "s#^#$dirname#")
    if [ "$SHELL_TYPE" = "bash" ]; then
        compopt -o nospace
        COMPREPLY=( $(compgen -o filenames -W "${res}" -- ${cur}) )
    else
        # zsh 补全
        reply=($(echo $res))
    fi
}

function _obd_sh_complete_func
{
  local all_cmds
  declare -A all_cmds
  COMPREPLY=()
  cur="${COMP_WORDS[COMP_CWORD]}"
  prev="${COMP_WORDS[COMP_CWORD-1]}"
  all_cmds["$*"]="prepare deploy redeploy reinstall start stop restart destroy upgrade mysqltest pid ssh less gdb sql mysql edit list sys display create_tenant drop_tenant sysbench tpcc graph display-trace set-config"
  case $prev in
  list)
    return 0
    ;;
  -p|--path)
  _obd_sh_reply_current_files
  ;;
  -c|--config)
  _obd_sh_reply_yaml_files
  ;;
  -n|--deploy-name)
  _obd_sh_reply_deploy_names
  ;;
  *)
    if [ "$SHELL_TYPE" = "bash" ]; then
      valid_len=$COMP_CWORD
      words=( ${COMP_WORDS[@]::valid_len} )
    else
      # zsh 使用 words 数组
      valid_len=$CURRENT
    fi
    index=valid_len
    while (( index >= 1 )); do
        target="${words[*]}"
        cmd=${all_cmds[$target]}
        if [[ "$cmd" != "" ]]
        then
          if [[ $cmd =~ ^_obd_sh_reply.* ]]
          then
            $cmd
            break
          else
            if [ "$SHELL_TYPE" = "bash" ]; then
              if [[ $cmd =~ .*'=' ]]
              then
                compopt -o nospace
              fi
              COMPREPLY=( $(compgen -W "${cmd}" -- ${cur}) )
            else
              # zsh 补全
              reply=($(echo $cmd))
            fi
            break
          fi
        fi
        index=$(( index - 1))
        tmp=${words[*]::index}
        [[ "$tmp" != "" ]] && parent_cmd=${all_cmds[$tmp]}
        if [[ "$parent_cmd" =~ ^_obd_sh_reply.*  || " $parent_cmd " =~ " ${words[index]} " ]]; then
          words[index]='*'
        else
          break
        fi
    done
    ;;
  esac


}

# zsh 专用的补全函数
function _obd_zsh_complete_func() {
    local cur prev
    cur="${words[CURRENT]}"
    prev="${words[CURRENT-1]}"

    case $prev in
        list)
            return 0
            ;;
        -p|--path|-c|--config)
            _obd_zsh_reply_current_files
            ;;
        obd)
            reply=(mirror cluster test update repo demo web devmode env tool)
            ;;
        cluster)
            reply=(autodeploy tenant start deploy redeploy restart reload destroy stop edit-config list display upgrade chst check4ocp reinstall)
            ;;
        tenant)
            reply=(create drop show)
            ;;
        mirror)
            reply=(clone create list update enable disable)
            ;;
        repo)
            reply=(list)
            ;;
        test)
            reply=(mysqltest sysbench tpcc)
            ;;
        devmode)
            reply=(enable disable)
            ;;
        tool)
            reply=(command db_connect dooba graph)
            ;;
        env)
            reply=(set unset show clear)
            ;;
        *)
            # 对于其他情况，尝试获取部署名称
            if [[ -d $OBD_HOME/.obd/cluster ]]; then
                _obd_zsh_reply_deploy_names
            fi
            ;;
    esac
}

# zsh 专用的回复函数
function _obd_zsh_reply_deploy_names() {
    if [[ -d $OBD_HOME/.obd/cluster ]]; then
        local res=$(ls -p $OBD_HOME/.obd/cluster 2>/dev/null | sed "s#/##")
        reply=($(echo $res))
    fi
}

function _obd_zsh_reply_current_files() {
    local filename=${cur##*/}
    local dirname=${cur%*$filename}
    local res=$(ls -a -p $dirname 2>/dev/null | sed "s#^#$dirname#")
    reply=($(echo $res))
}
