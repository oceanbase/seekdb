# 贡献指南

OceanBase SeekDB 社区热情欢迎每一位对数据库技术热爱的开发者，期待携手开启思维碰撞之旅。无论是文档格式调整或文字修正、问题修复还是增加新功能，都是对 OceanBase SeekDB 社区参与和贡献方式之一，立刻开启您的 First Contribution 吧！

## 概述

本文档介绍如何为 OceanBase SeekDB 项目做出贡献，包括如何找到合适的 Issue、如何提交代码、如何创建 Pull Request 等。

## 相关文档

- [编译与运行](build-and-run.md) - 编译项目
- [编程惯例](coding-convention.md) - 了解 SeekDB 的编程风格
- [C++ 编程规范](coding-standard.md) - 详细的编程规范
- [编写单元测试](unittest.md) - 编写测试代码

## 如何找到合适的 Issue

- **新手入门**：通过 [good first issue](https://github.com/oceanbase/seekdb/issues?q=is%3Aopen+is%3Aissue+label%3A%22good+first+issue%22) 标签可以找到适合新手入门的 issue
- **Bug 修复**：通过 `bug` 标签找到当前版本的 bug
- **新功能**：通过 `new feature` 标签找到建议添加的功能

找到合适的 issue 之后，可以在 issue 下回复 `/assign` 将 issue 分配给自己。

## 代码贡献流程

以下步骤以 CentOS 7 操作系统为例。

### 1. Fork 项目仓库

1. 访问项目的 [GitHub 地址](https://github.com/oceanbase/seekdb)。
2. 点击 Fork 按钮创建远程分支。

### 2. 配置本地环境变量

```bash
working_dir=$HOME/workspace # 定义工作目录
user={GitHub账户名} # 和github上的用户名保持一致
```

### 3. 克隆代码

```bash
mkdir -p $working_dir
cd $working_dir
git clone git@github.com:$user/seekdb.git
# 也可以使用: git clone https://github.com/$user/seekdb

# 添加上游分支
cd $working_dir/oceanbase
git remote add upstream git@github.com:oceanbase/seekdb.git
# 或: git remote add upstream https://github.com/oceanbase/seekdb

# 为上游分支设置 no_push
git remote set-url --push upstream no_push

# 确认远程分支有效
git remote -v
```

### 4. 创建新分支

```bash
# 更新本地 master 分支。
new_branch_name={issue_xxx} # 设定分支名，建议直接使用issue+id的命名
cd $working_dir/oceanbase
git fetch upstream
git checkout master
git rebase upstream/master
git checkout -b $new_branch_name
```

### 5. 开发

在新建的分支上完成开发

### 6. 提交代码

```
# 检查本地文件状态
git status

# 添加您希望提交的文件
# 如果您希望提交所有更改，直接使用 `git add .`
git add <file> ...
# 为了让 github 自动将 pull request 关联上 github issue,
# 建议 commit message 中带上 "fixed #{issueid}", 其中{issueid} 为issue 的id,
git commit -m "fixed #xxxx: update the xx"

# 在开发分支执行以下操作
git fetch upstream
git rebase upstream/master
git push -u origin $new_branch_name
```

### 7. 创建 PR

1. 访问您 Fork 的仓库。
2. 单击 {new_branch_name} 分支旁的 Compare & pull request 按钮。

### 8. 签署 CLA 协议

签署[Contributor License Agreement (CLA)](https://cla-assistant.io/oceanbase) ；在提交 Pull Request 的过程中需要签署后才能进入下一步流程。如果没有签署，在提交流程会有如下报错：

![image](https://user-images.githubusercontent.com/5435903/204097095-6a19d2d1-ee0c-4fb6-be2d-77f7577d75d2.png)

### 9. 代码审查与合并

有review、合并权限的维护者，会帮助开发者进行代码review；review意见通过后，后续的操作都会由维护者进行，包括运行各项测试，最终代码会由维护者通过后合入

### 10. 祝贺成为贡献者

当 pull request 合并后，则所有的 contributing 工作全部完成，恭喜您，您成为 OceanBase SeekDB 贡献者！

## 贡献类型

除了代码贡献，您还可以通过以下方式为项目做出贡献：

- **文档改进**：修正文档错误、改进文档结构、添加示例
- **问题报告**：提交 bug 报告或功能建议
- **代码审查**：帮助审查其他人的 Pull Request
- **社区支持**：在 Issue 和讨论区帮助其他开发者

感谢您对 OceanBase SeekDB 项目的贡献！
