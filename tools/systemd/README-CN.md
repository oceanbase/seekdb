# 使用 systemd 安装和管理 seekdb

seekdb RPM 和 DEB 软件包会安装由 systemd 管理的单节点服务。该方式适用于开发、评估和其他非关键单节点场景。重要数据必须提前备份，当前生产支持策略以[产品部署文档](https://docs.seekdb.ai/seekdb/deploy-by-systemd/)为准。

## 前置条件

- 使用 systemd 的 Linux 发行版，以及可以执行 `sudo` 的用户
- 至少 1 个 CPU 核和 2 GiB 可用内存
- 已安装 `curl`、`jq` 和 MySQL 兼容客户端
- 数据目录和 redo 目录所在磁盘具有足够可用空间

当前软件包文档覆盖以下已验证系统：

- RPM：Anolis OS 8/23、CentOS 7/9、openEuler 22.03/24.03
- DEB：Debian 11/12/13、Ubuntu 20.04/22.04/24.04

必须使用与目标操作系统和 CPU 架构匹配的软件包。

## 在 RPM 系统上安装

### 在线安装

官方安装脚本会配置当前软件源并安装最新版本：

```bash
curl -fsSL https://obportal.s3.ap-southeast-1.amazonaws.com/download-center/opensource/seekdb/seekdb_install.sh | sudo bash
```

### 离线安装

从 [seekdb 下载中心](https://www.oceanbase.ai/zh-CN/download)下载与目标环境匹配的 RPM，复制到目标机器后执行：

```bash
sudo rpm -ivh seekdb-*.rpm
```

## 在 Debian 或 Ubuntu 系统上安装

### 在线安装

安装识别发行版所需的工具，添加 seekdb 软件源并安装软件包：

```bash
sudo apt-get update
sudo apt-get install -y lsb-release ca-certificates curl jq default-mysql-client
echo "deb [trusted=yes] https://mirrors.aliyun.com/oceanbase/community/stable/$(lsb_release -is | awk '{print tolower($0)}')/$(lsb_release -cs)/$(dpkg --print-architecture)/ ./" | sudo tee /etc/apt/sources.list.d/oceanbase.list
sudo apt-get update
sudo apt-get install -y seekdb
```

当前软件源采用 APT 的 `trusted=yes` 形式，不再需要已废弃的 `apt-key` 命令。

### 离线安装

从 [seekdb 下载中心](https://www.oceanbase.ai/zh-CN/download)下载与目标环境匹配的 DEB，复制到目标机器后执行：

```bash
sudo dpkg -i seekdb-*.deb
```

如果 `dpkg` 报告缺少依赖，应先从操作系统软件源安装缺失依赖，再重新执行安装。

## 安装路径

| 路径 | 用途 |
| --- | --- |
| `/usr/bin/seekdb` | seekdb 服务端二进制 |
| `/usr/bin/obshell` | 软件包包含时安装的 obshell agent |
| `/etc/seekdb/seekdb.cnf` | systemd 启动配置 |
| `/usr/lib/systemd/system/seekdb.service` | systemd unit |
| `/usr/libexec/seekdb/scripts/` | 服务启停和遥测脚本 |
| `/usr/share/seekdb/` | 管理 SQL 和运行时数据文件 |

## 配置首次启动

第一次启动服务前编辑 `/etc/seekdb/seekdb.cnf`：

```ini
# 持久选择服务使用的数据库目录。
base-dir=/var/lib/oceanbase

# 初始化新数据库时传入。
data-dir=/var/lib/oceanbase/store
redo-dir=/var/lib/oceanbase/store/redo
port=2881
cpu_count=4
```

服务启动脚本每次都会读取 `base-dir`。只有初始化新数据库时，才会传入 `data-dir`、`redo-dir`、`port`、`cpu_count` 和其他参数项。初始化完成后，修改 `seekdb.cnf` 中这些配置不会重新配置已有数据库；动态参数应使用支持的 SQL 配置接口修改。

修改 `base-dir` 会让服务指向另一个数据库目录。已有数据后不要误改该配置。

## 管理服务

安装或替换 unit 后先让 systemd 重新加载配置，再启动 seekdb：

```bash
sudo systemctl daemon-reload
sudo systemctl start seekdb
sudo systemctl status seekdb
```

服务使用 `Type=notify`。启动成功时会报告 `seekdb is ready and running`；bootstrap 失败会作为服务启动失败返回给 systemd。

常用管理命令：

```bash
sudo systemctl stop seekdb
sudo systemctl restart seekdb
sudo systemctl enable seekdb
sudo systemctl disable seekdb
```

使用以下命令排查启动和服务错误：

```bash
sudo journalctl -u seekdb --since today
sudo journalctl -u seekdb -b --no-pager
```

服务端日志位于 `<base-dir>/log/`。使用软件包默认配置时，主日志为 `/var/lib/oceanbase/log/seekdb.log`。

## 卸载

先停止服务，再使用对应的软件包管理器卸载：

```bash
sudo systemctl stop seekdb
sudo yum erase seekdb        # RPM 系统
sudo apt-get remove seekdb   # Debian/Ubuntu 系统
```

卸载软件包会保留数据库数据。卸载脚本可能生成 `/var/lib/seekdb/seekdb_clean.sh`，用于按需清理数据。

> **危险：** 以下命令会永久删除 seekdb 数据，无法恢复。执行前必须检查生成脚本，并确认其中的每一个目标路径。

```bash
sudo bash /var/lib/seekdb/seekdb_clean.sh
```
