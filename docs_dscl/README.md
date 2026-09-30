# docs_dscl

本目录用于存放小组在 OceanBase / seekdb 前期备赛准备过程中的学习笔记、源码阅读记录等学习资料。方便团队成员互相参考，并为后续赛题选择、源码定位和比赛开发提供资料。

---

## 1. 目录结构

每位成员按照自己的 Git 用户名建立独立目录：
```text
docs_dscl/
├── README.md
├── <git_user_1>/
├── <git_user_2>/
└── <git_user_3>/

推荐结构
<git_user>/
├── README.md        # 个人学习索引
├── build/           # 编译、部署、环境配置
├── source/          # 源码阅读、调用链分析
├── study/           # 官方资料、课程、文档学习笔记
├── experiment/      # 测试、实验、benchmark
└── summary/         # 周总结、阶段总结
```

## 2. 文档内容建议

建议记录以下内容：

### 环境与编译

- 操作系统、CPU、内存等环境信息
- seekdb 编译过程遇到的问题
- 编译错误及解决办法
- 依赖、工具链配置

### 源码阅读

- 涉及目录 / 文件
- 核心类 / 函数
- 调用关系
- 自己对模块作用的理解
- 尚未解决的问题


### 学习资料

学习官方文档、课程、视频时，不需要全文摘录。

重点记录：

- 这个资料解决什么问题
- 核心概念
- 与 seekdb 源码的对应关系
- 与比赛可能相关的部分
- 自己不理解的地方，供后续讨论


## 3. 文件命名建议

尽量使用能够直接看出内容的英文或中英文名称。

推荐：

```text
build_seekdb.md
seekdb_source_overview.md
sql_execution_path.md
vector_search.md
fulltext_search.md
hybrid_search.md
transaction_notes.md
2026-10-07_summary.md
```

之后每周进行进度汇总报告