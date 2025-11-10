# OceanBase SeekDB 开发者手册

## 介绍

* **面向人群** 手册的目标受众是OceanBase SeekDB的贡献者，无论是新人还是老手。
* **目标** 手册的目标是帮助贡献者成为OceanBase SeekDB的专家，熟悉其设计和实现，从而能够在现实世界中流畅地使用它以及深入开发OceanBase SeekDB本身。

## 手册结构

本手册按照开发者的学习路径组织，分为以下几个部分：

### 第一部分：开发环境搭建

这部分帮助新手快速搭建开发环境，开始使用SeekDB。

1. [安装工具链](toolchain.md) - 安装C++编译工具链
2. [获取代码，编译运行](build-and-run.md) - 克隆代码、编译和运行SeekDB
3. [配置IDE](ide-settings.md) - 配置VSCode + ccls进行代码阅读和开发

### 第二部分：开发规范与实践

了解SeekDB的编程规范和开发习惯，确保代码风格一致。

1. [编程惯例](coding-convention.md) - SeekDB特有的编程习惯和约定（快速入门）
2. [编程规范](coding-standard.md) - 详细的C++编码规范和约束（深入参考）

### 第三部分：测试与调试

学习如何编写测试和调试代码。

1. [编写并运行单元测试](unittest.md) - 使用Google Test编写和运行单元测试
2. [运行MySQL测试](mysqltest.md) - 运行mysqltest集成测试
3. [调试](debug.md) - 使用GDB、日志等方式调试SeekDB

### 第四部分：核心系统设计

深入理解SeekDB的核心设计和实现。

1. [内存管理](memory.md) - SeekDB的内存管理机制和多租户内存隔离
2. [日志系统](logging.md) - 日志的使用方法和实现细节
3. [基础数据结构](container.md) - SeekDB提供的容器类（替代STL）

### 第五部分：贡献代码

参与SeekDB开发的完整流程。

1. [提交代码和Pull Request](contributing.md) - 如何贡献代码到SeekDB项目
