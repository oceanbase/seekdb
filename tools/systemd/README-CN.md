# 通过软件源安装 seekdb 数据库
如果你想在Linux rpm或deb平台上部署seekdb，可以使用yum或apt进行单节点安装，并通过systemd进行简单管理

**注意**

- 该方法仅能用做学习研究或测试使用；
- 千万不要使用此方法用于带有重要数据的场景，比如生产环境。

## 通过 YUM 仓库安装 seekdb
配置yum源，安装seekdb，会自动安装所需依赖
```bash
yum install -y yum-utils
yum-config-manager --add-repo https://mirrors.aliyun.com/oceanbase/OceanBase.repo
yum install -y seekdb
```

## 通过 APT 仓库安装 seekdb
配置apt源，安装seekdb，会自动安装所需依赖
```bash
apt update
apt install -y lsb-release wget gnupg2 mysql-client curl
wget http://mirrors.oceanbase.com/oceanbase/oceanbase_deb.pub && apt-key add oceanbase_deb.pub
echo "deb http://mirrors.oceanbase.com/oceanbase/community/stable/$(lsb_release -is | awk '{print tolower($0)}')/$(lsb_release -cs)/$(dpkg --print-architecture)/ ./" | tee -a /etc/apt/sources.list.d/oceanbase.list
apt update
apt install -y seekdb
```

## 依赖列表说明：
| 组件 | 版本 |
|-------|-------|
| libaio | / |
| systemd | / |

# 启动方法
可以通过以下指令进行启动：
```bash
systemctl start seekdb
```

可以通过以下指令将seekdb服务设置为开机自启动：
```bash
systemctl enable seekdb
```

## systemd介绍
Systemd提供了自动化管理seekdb的启动和停止，可以通过systemctl指令对seekdb进行管理控制，例如：
```bash
systemctl {start|stop|restart|status} seekdb
```

## 通过systemd配置seekdb
systemd提供了配置文件`/etc/seekdb/seekdb.cnf`,可以在第一次初始化启动前修改配置
