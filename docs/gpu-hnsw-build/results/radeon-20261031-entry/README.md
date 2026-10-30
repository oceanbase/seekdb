# 2026-10-31 入口与部署验收原始结果

完整流程与限制见 [验收报告](../../ENTRY_AUDIT_20261031.md)。`candidate-*` 对应 b35b7d4 的空目录完整构建及组件安装后 SQL；`input-matrix-summary.json` 和五个输入 JSON 使用此前已验证的 0e86 主程序；`copy-configurations.json` 对应后来补充的部署脚本。不要把这三组执行源版本混写。

`candidate-source-hashes.json` 保存完整构建前后未变的 9,248 个源码哈希；`full-build-source-sha256.json` 保存其中 18 个相关文件。发布时 17 个仍与完整构建相同，`copy.sh` 的新版本单独验证，汇总在 `release-source-provenance.json` 与 `release-source-sha256.json`。

在完整仓库根目录执行下列离线检查，可核对发布源码、六轮原始计时/质量、DML/过滤、重启、五组额外输入和部署分支。它不会运行数据库或 GPU，也不能代替完整构建。发布源码检查针对当前工作树，后续实现改动后应回到对应发布版本检查。

```bash
python3 - <<'PY'
import hashlib, json, pathlib, sys
sys.path.insert(0, 'docs/gpu-hnsw-build')
from verify_evidence import verify_runs
root = pathlib.Path('.')
folder = root / 'docs/gpu-hnsw-build/results/radeon-20261031-entry'
load = lambda name: json.loads((folder / name).read_text(encoding='utf-8'))
hashes = load('release-source-sha256.json')
for name, expected in hashes.items():
    data = (root / name).read_bytes().replace(b'\r\n', b'\n')
    assert hashlib.sha256(data).hexdigest() == expected, name
medians, speedup = verify_runs(load('candidate-sql.json'))
assert load('candidate-build-status.json')['completed']
assert load('candidate-acceptance-summary.json')['completed']
assert load('candidate-contract.json')['completed']
assert len(load('candidate-contract.json')['checks']) == 27
for case in ['odd_dim', 'small_m64', 'high_dim', 'large_values', 'tiny_values']:
    verify_runs(load(case + '.json'))
deployment = load('copy-configurations.json')
assert deployment['completed']
assert all(case['returncode'] == case['expected_returncode']
           for case in deployment['cases'].values())
print(json.dumps({'verified': True, 'source_files': len(hashes),
                  'medians_seconds': medians, 'speedup': speedup,
                  'scope': 'archived evidence consistency; no SQL/GPU execution'}))
PY
```

新证据没有重新执行 profiler，不包含本轮 GPU kernel trace。实际建索引的 GPU 日志在 SQL JSON 中；158 次 kernel dispatch 的 profile 属于单独保留的 2026-10-30 历史材料。完整数据库日志、执行脚本及剥离调试信息后的 Linux 产物另有本地归档。
