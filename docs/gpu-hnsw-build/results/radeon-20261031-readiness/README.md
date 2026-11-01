# 2026-10-31 Release 安装排查记录

本目录不包含新的 GPU 性能成绩。

- `observations.json`：本次云端命令的观察结果。SSH 随后拒绝连接，未能下载完整远端原始目录；该文件明确标记为根据会话中已捕获的终端输出整理，不冒充完整原始日志。
- `install-before-excerpt.txt`：已捕获的旧版 `server` 安装输出摘录，返回 0 却缺少主程序。复现时 worker 已编译，主程序完整构建尚未完成；生成安装脚本本身没有主程序规则，故缺口不取决于它是否编译完成。
- `local-checks.json`：Linux 本地安装规则与脚本回归的命令、返回值、源码 SHA-256。
- `hipvs-regressions.log`：20 项通过，其中新增 3 项实际编译小程序并调用生产 CMake 安装规则的测试，以及 3 项验证嵌套 Cargo 并行参数的测试。
- `notebook-regressions.log`：3 项 Notebook 辅助脚本回归通过。
- `baseline-inventory-check.json`：六个头文件在赛事基线中已存在、却未登记到其头文件清单的核对结果；没有把它们归因于本次 GPU 改动。
- `retry-configure.log`、`retry-compile-first-attempt.log`、`retry-status.json`：后续另一个实例上已实际下载的原始文件。配置通过；编译因本次离线缓存遗漏独立 jemalloc 工作区的依赖而失败，不是缺少源码。补齐缓存后 SSH 再次拒绝连接，未执行恢复验证。

安装规则测试使用小程序检验组件安装行为，**不是**完整 seekdb 或 GPU 测试。完整构建及 GPU 验证尚未完成，见[排查报告](../../READINESS_AUDIT_20261031.md)。

核对本轮已测试的六个文件：

```bash
python3 - <<'PY'
import hashlib, json, pathlib
root = pathlib.Path('.')
folder = root / 'docs/gpu-hnsw-build/results/radeon-20261031-readiness'
report = json.loads((folder / 'local-checks.json').read_text(encoding='utf-8'))
assert report['completed']
assert all(run['returncode'] == 0 for run in report['runs'])
for name, expected in report['source_sha256'].items():
    data = (root / name).read_bytes().replace(b'\r\n', b'\n')
    assert hashlib.sha256(data).hexdigest() == expected, name
print('verified: local test records and six tested source files; no GPU claim')
PY
```

该命令核对记录和源码对应关系，不代替重新运行测试。重新测试安装规则需要 Linux、CMake >= 3.20 和 C++ 编译器；验证真实安装需要先构建 seekdb，再执行 `unittest/sql/vector_index/check_hipvs_install.py`。
