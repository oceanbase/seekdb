# Namespace fork 开发资料备份

2026-10-09：按用户要求，将此前只保留在工作区的文档、测试和结果记录提交到当前开发分支，供回溯和备份。合入前再移除这些临时开发资料。

- `namespace-fork/`：设计、TODO、工单、过程记录、定向测试、故障注入脚本及验证结果。当前结项见 `namespace-fork/source-tree-progress.md`。
- `excluded-from-branch/`：此前移出代码提交的早期设计、ADR 和测试脚本；本次按原路径归档，目录名保留以免破坏脚本引用。
- `namespace-fork/handoff-ticket07.md`：最初 `/tmp/seekdb-ns-fork-handoff-ticket07.md` 交接文档的副本，内容描述当时状态。
- 四件套入口为 `namespace-fork/run_four_gates.py`；带故障注入的构建通过 `gate_probe_injection.py` 开关。部分脚本含本机绝对路径，换环境时需要调整。

归档包含文本源码、文档、JSON 证据和不超过 256 KiB 的日志。原始大日志、二进制、构建产物、缓存、运行数据、指向本机产物目录的软链接和 TLS wallet 未提交，仍保留在原位置。测试证书可用已提交的 `tools/obtest/generate_wallet.sh` 重新生成。

这是当前文件的首次集中快照；较早的设计变化可从阶段记录、结果文件和代码提交追溯，不代表补建了此前每次文档编辑的 Git 版本。归档文件中的历史状态和本机产物路径保留原样。
