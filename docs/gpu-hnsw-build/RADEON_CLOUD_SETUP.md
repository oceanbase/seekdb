# Radeon Cloud 创建与连接步骤

适用于 Windows 本机与中国站，2026-10-30 根据用户视频及平台文档核对。

## 1. 注册与算力

先暂停视频 **10:30**，可通过赛事专属二维码注册；普通入口为 [AMD AI 开发者计划](https://developer.amd.com.cn/)。完成本人账号注册和资料填写。

开发者积分与云端算力余额不同。按[当前兑换流程](https://amd-aim.github.io/radeon-cloud-docs/zh-cn/guides/developer-program/)，先在开发者计划「兑取算力」获得兑换码，再到 [Radeon Cloud 中国站](https://developer.amd.com.cn/radeon) 的「积分与用量 → 兑换积分」兑换。经典版在 Profile 的 Credits 区域操作。确认余额到账再启动实例；视频当时介绍的权益不代表当前账号必定可领的额度。

## 2. 先配置本机 SSH 公钥

已检查到本机存在 `C:\Users\shacknap\.ssh\id_ed25519.pub`，无需覆盖生成新的默认密钥。在**本机 PowerShell**运行：

```powershell
Get-Content "$env:USERPROFILE\.ssh\id_ed25519.pub"
```

复制完整输出（通常以 `ssh-ed25519` 开头），粘贴到云平台：

- 新版：设置 → SSH 密钥 → 新建 SSH 密钥。
- 视频中的经典版：Profile → SSH Public Keys。

名称可填 `Windows-laptop`。只复制 `.pub` 公钥，不上传或发送无 `.pub` 后缀的私钥。按[SSH 文档](https://amd-aim.github.io/radeon-cloud-docs/zh-cn/guides/ssh/)，添加的密钥只对之后启动的实例生效，**先保存公钥，再创建实例**。

## 3. 启动对应 Notebook

在中国站「模板库」搜索 **seekdb-hipvs**，从对应模板点启动，等待就绪后打开 Notebook。经典版入口名称可能不同；参照[模板库说明](https://amd-aim.github.io/radeon-cloud-docs/zh-cn/guides/gallery/)。

若需手动选镜像，当前[镜像列表](https://amd-aim.github.io/radeon-cloud-docs/zh-cn/guides/images/)列出了 `seekdb-hipvs-nb-demo_v1.4.0`，旧 `Oceanbase` 1.0.0 已被它替代。优先使用对应模板自带规格和镜像；找不到时先核对账号可见范围，不使用无关模型镜像代替。

启动前检查「工作区存储」并选择 **持久化 /workspace**；默认的临时工作区在销毁实例时会删除内容。当前文档列出 25 GB 与部分账号可选的 100 GB，编译前需核对空间。具体见[保存数据](https://amd-aim.github.io/radeon-cloud-docs/zh-cn/guides/storage/)。

镜像提供课程演示所需基础环境；为了验证本仓库的修改，随后仍要编译并启动新的 seekdb 二进制。

## 4. 本机连接实例

等待实例卡片显示 SSH 命令；若显示“正在配置 SSH”，继续等待。复制平台的**实际命令**在本机 PowerShell 执行，主机和端口必须取自自己的实例：

```powershell
ssh root@实际主机 -p 实际端口
```

需要指定现有密钥时：

```powershell
ssh -i "$env:USERPROFILE\.ssh\id_ed25519" root@实际主机 -p 实际端口
```

首次连接确认目标是自己的实例后接受主机指纹。密钥如有口令，只在本机终端输入，不发到聊天。

连接成功后，在**云端终端**执行只读检查：

```bash
hostname
uname -a
rocm-smi
ls -ld /opt/rocm /opt/hipvs /workspace
```

保存实际输出。命令或目录不存在时先记录环境差异，不用 CUDA 软件覆盖已有 ROCm 环境。

## 5. 接下来交给当前开发会话

发送实例的 SSH 命令（主机、端口、用户名）即可。不要发送密码、私钥、验证码、兑换码或带 token 的 Jupyter URL。

后续先只读检查，再同步当前分支改动，编译与验证 CPU/GPU 对照、CUVS 别名和 SQL 回归。尚未创建或验证实例前，不能声称跑出了新的 GPU 结果。

本次已通过 SSH 完成真实验证，结果与运行步骤见[提交说明](SUBMISSION.md)。实测发现持久化 `/workspace` 为 NFS，不能直接作为 seekdb 数据库目录：它不支持所需的 `fallocate`。本次源码与证据放在 `/workspace`，构建和数据库放在实例本地磁盘，测试后将二进制复制到持久化目录。不要把“文件能保存”误认为“适合直接运行数据库”。

该镜像的 Rust 1.97.1 也低于仓库锁定的 1.98.1；构建前检查实际版本。rsproxy 在本次网络下返回 403，改用可访问的注册表配置，并使用经官方 SHA-256 核对的独立 Rust 安装包。没有修改系统全局工具链或 seekdb 的版本锁定要求。

运行实例会消耗算力，闲置也计费。只有启动时选了持久化存储，`/workspace` 才会在销毁后保留；结束实例前确认代码、证据已保存，见[积分说明](https://amd-aim.github.io/radeon-cloud-docs/zh-cn/guides/credits/)。
