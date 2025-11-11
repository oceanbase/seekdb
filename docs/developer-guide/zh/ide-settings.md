
---
title: IDE 配置
---

# IDE 配置

本文档介绍如何配置开发环境以便更好地阅读 OceanBase SeekDB 的代码。

## 概述

为了更好的阅读 OceanBase SeekDB 的代码，我们建议使用一个可以方便索引 SeekDB 代码的 IDE。

- **Windows**：推荐使用 `Source Insight`
- **Mac/Linux**：推荐使用 `VSCode + ccls`

> **注意**：由于 `Source Insight` 使用起来非常简单，所以本文档主要介绍 `VSCode + ccls` 的配置方法。

## 相关文档

- [编译与运行](build-and-run.md) - 编译项目
- [编程惯例](coding-convention.md) - 了解 SeekDB 的编程风格

## 为什么选择 VSCode + ccls

### ccls 简介

[ccls](https://github.com/MaskRay/ccls) 是基于 [cquery](https://github.com/jacobdufault/cquery) 的，cquery 是一个 C/C++/Objective-C 的 [LSP](https://en.wikipedia.org/wiki/Language_Server_Protocol) 之一（简单来说，LSP 用于提供编程语言特定的功能，如代码补全、语法高亮、警告和错误标记，以及重构例程）。

### 为什么选择 ccls 而不是 clangd

在 C/C++ LSP 领域，比较有名的工具有 clangd 和 ccls。这里我们推荐 ccls，因为：

1. **索引性能**：ccls 构建索引的速度比 clangd 慢，但是构建完成后，ccls 访问索引的速度比 clangd 快
2. **Unity 编译支持**：clangd 不支持 unity 编译，而 SeekDB 是通过 unity 编译的，clangd 无法通过 compile_commands.json 构建索引

### 远程开发

SeekDB 的代码量非常大，而且 SeekDB 不能在 Mac 或者 Windows 下编译，所以我们建议在远程服务器上下载代码，然后在本地使用 VSCode 访问远程服务器上的代码。

## 在远程服务器上配置 ccls

> **注意**：下面的 `/path/to` 只是一个路径示例，请替换成你的实际路径。

## ccls 安装

## 在 CentOS 上安装 ccls

> 注意：如果没有权限执行 `yum`，请使用 `sudo yum ...`。

```bash
yum install epel-release
yum install snapd # On centos8: yum install snapd --nobest
systemctl enable --now snapd.socket
ln -s /var/lib/snapd/snap /snap
snap install ccls --classic
```

然后把下面的命令添加到你的环境变量文件中，例如 '~/.bashrc' 或者 '~/.bash_profile'

```bash
export PATH=/var/lib/snapd/snap/bin:$PATH
```

刷新一下环境变量，例如：

```bash
source ~/.bashrc   # or
source ~/.bash_profile
```

## 在 Ubuntu 上安装 ccls

```bash
apt-get -y install ccls
```

> 注意：如果没有权限执行 `apt-get`，请使用 `sudo apt-get ...`。

## 检查安装是否成功

运行这个命令判断是否安装成功。

```bash
ccls --version
```

## VSCode 配置

## 远程插件

在远程主机上下载源代码后，可以很容易地在远程主机上设置调试环境。同时，由于远程主机更强大，应用程序可以更快地运行。即使网络出现问题，用户也可以很容易地访问远程主机上的源代码，只需在重新连接远程服务器后等待重新加载即可。

### 安装

在VSCode的扩展商店中下载并安装Remote插件。

![remote plugin](images/ide-settings-remote-plugin.png)

### 使用

**注意**：确保本地机器和远程机器之间的连接正常。

安装插件后，VSCode左下角会出现一个图标。

![remote plugin usage](images/ide-settings-remote-plugin-usage.png)

点击图标，选择 `Connect to Host`，或者按快捷键 `ctrl+shift+p`，选择 `Remote-SSH:Connect to Host`：

![connec to remote ](images/ide-settings-connect-to-remote-server.png)

输入远程服务器的`用户名@IP地址`，然后输入密码，VSCode会连接到远程服务器，准备打开远程机器的文件或目录。

![input password](images/ide-settings-input-password.png)

输入密码之后，VSCode会连接到远程服务器，准备打开远程机器的文件或目录。

如果需要指定端口，请选择 `Add New SSH Host`，然后输入ssh命令，然后选择一个配置文件来存储ssh配置。

![ssh port](images/ide-settings-use-different-ssh-port.png)

![ssh config file](images/ide-settings-choose-ssh-config.png)

接着，配置好的机器可以在 `Connect to Host` 中找到。

每次都需要输入密码，如果想跳过这个步骤，可以配置SSH免密登录。

## C/C++ 插件

不推荐使用C/C++插件，因为无法为SeekDB提供良好的索引功能，并且与ccls插件不兼容。

如果有一些简单的场景，可以在VSCode的扩展商店中下载并安装C/C++插件。

![cpp plugins](images/ide-settings-cpp-plugins.png)

C/C++插件可以自动完成代码和语法高亮，但是这个插件无法为OceanBase构建索引，很难跳转到OceanBase的符号。

## ccls 插件

### 安装 ccls 插件

![ccls plugin](images/ide-settings-ccls-plugin.png)

> 要使用 ccls，就建议卸载 C/C++ 插件。

### 配置 ccls 插件

1. 插件设置按钮然后选择 **Extension Settings**

![ccls plugin settings](images/ide-settings-ccls-plugin-settings.png)

2. 设置 `ccls.index.threads`。ccls 默认使用系统80%的CPU核心作为默认的并行度。我们可以在VSCode的配置页面中搜索 `threads` 并设置如下数字。

> 默认情况下，SeekDB使用unity编译，比普通情况下消耗更多的内存。如果并行度太高，例如8核16G的系统，系统可能会挂起。

![ccls threads config](images/ide-settings-ccls-threads-config.png)

## 使用

### 步骤 1: 下载源码

使用 git 下载源码：

```bash
git clone https://github.com/oceanbase/seekdb.git
cd seekdb
```

### 步骤 2: 生成 compile_commands.json

生成 `compile_commands.json` 文件，这是 ccls 构建索引所必需的：

```bash
bash build.sh ccls --init
```

执行完成后，可以在 SeekDB 源码根目录下看到 `compile_commands.json` 文件。

### 步骤 3: 重启 VSCode 并构建索引

执行完上面的步骤后，需要重启 VSCode，然后在 VSCode 底部可以看到构建索引的过程：

![ccls-indexing](images/ide-settings-ccls-indexing.png)

索引构建完成后，可以很容易地找到任何打开文件的函数引用和类成员，如下面的示例：

![ccls index example](images/ide-settings-ccls-index-example.png)

### 快捷键设置

推荐配置以下快捷键以提高开发效率：

![ccls shortkey](images/ide-settings-ccls-keyboard-settings.png)

![ccls shortkey](images/ide-settings-ccls-keyboard-settings2.png)
