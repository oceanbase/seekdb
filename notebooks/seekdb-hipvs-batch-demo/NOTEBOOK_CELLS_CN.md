# Notebook 每个代码单元的完整填写内容

截图中的 `lib = /workspace/seekdb_hipvs_notebook/lib` 表示仍加载课程旧目录；第 3 节只有 `import json`，也不是已修复单元。请勿把截图里保存的 12.65 倍表格当作本次执行结果。

以下 11 块代码逐字取自提交 `9fdf9784c08f5c849459e06b7cd78681e161f4e4` 的修复版 Notebook；该版本曾在 Radeon Cloud 全新内核执行 11/11 个代码单元成功。每块放入独立的 **Code** 单元，按顺序运行。章节说明文字属于 Markdown，不需要改成代码。

## 先确认打开正确文件

在 JupyterLab 菜单 File → Open from Path… 中打开：

`/workspace/seekdb-hipvs-batch-demo/seekdb_hipvs_batch_demo.ipynb`

选择 Kernel → Restart Kernel and Run All Cells…，确认运行。如果你打开的就是这个修复文件，无须手工替换下面的代码。

如果一定要在原课程 Notebook 中逐块替换，请先新增一个 Code 单元运行下面的目录检查，再填写其余 11 块。它让辅助库、服务目录和输出目录都指向修复工程；缺少目录或辅助文件时会立即停下，不会混用旧版本。

```python
from pathlib import Path
import os

PROJECT = Path('/workspace/seekdb-hipvs-batch-demo')
required = [PROJECT / 'lib' / name for name in ('seekdb_demo.py', 'start_server.py', 'sdb.py')]
assert all(p.is_file() for p in required), '缺少修复工程：请先上传并完整解压修复包，不能只复制 ipynb。'
os.chdir(PROJECT)
print('Working directory:', Path.cwd())
```

修复包在仓库 `notebooks/seekdb-hipvs-batch-demo.zip`。解压包内 `seekdb-hipvs-notebook-demo/notebooks/seekdb-hipvs-batch-demo` 整个目录到 `/workspace/seekdb-hipvs-batch-demo`。保留 `lib`、数据准备和运行脚本；运行仍需课程镜像的 `/seekdb_workspace`、ROCm 和 AMD GPU。无需模型 API Key。

如果之前加载过旧辅助模块，先重启内核，再执行目录检查和后续单元。

## 代码块 1：初始化：导入 Path 并设置本地 telemetry 标识

```python
from pathlib import Path
import os
import time
import uuid


def _notebook_directory():
    candidates = []
    for variable_name in ("__vsc_ipynb_file__", "__session__"):
        notebook_path = globals().get(variable_name)
        if notebook_path:
            candidates.append(Path(notebook_path).expanduser().resolve().parent)
    candidates.append(Path.cwd().resolve())

    for candidate in dict.fromkeys(candidates):
        if (candidate / "lib" / "seekdb_demo.py").is_file():
            return candidate
    return Path.cwd().resolve()


def _load_or_create_scope_uuid(file_path, timeout=5):
    deadline = time.monotonic() + timeout
    last_error = None

    while time.monotonic() < deadline:
        try:
            return str(uuid.UUID(file_path.read_text(encoding="ascii").strip()))
        except FileNotFoundError:
            generated_id = str(uuid.uuid4())
            try:
                descriptor = os.open(
                    file_path,
                    os.O_WRONLY | os.O_CREAT | os.O_EXCL,
                    0o600,
                )
            except FileExistsError:
                pass
            else:
                try:
                    with os.fdopen(descriptor, "w", encoding="ascii") as stream:
                        stream.write(generated_id + "\n")
                        stream.flush()
                        os.fsync(stream.fileno())
                except Exception:
                    file_path.unlink(missing_ok=True)
                    raise
                return generated_id
        except (OSError, ValueError) as exc:
            last_error = exc
        time.sleep(0.05)

    raise RuntimeError(
        f"Unable to read a valid telemetry UUID from {file_path}: {last_error}"
    )


_TELEMETRY_ID_FILE = _notebook_directory() / ".seekdb_telemetry_instance_id"
_TELEMETRY_INSTANCE_ID = _load_or_create_scope_uuid(_TELEMETRY_ID_FILE)
os.environ["SEEKDB_TELEMETRY_INSTANCE_ID"] = _TELEMETRY_INSTANCE_ID
```

## 代码块 2：初始化：加载配套辅助代码和绘图库

```python
import sys, os, subprocess, time, importlib

# ---- locate the sibling lib/ folder robustly (VS Code / JupyterLab / nbconvert) ----
def _find_lib():
    cands = []
    p = globals().get("__vsc_ipynb_file__")      # VS Code injects the .ipynb abs path
    if p: cands.append(os.path.dirname(os.path.abspath(p)))
    p = globals().get("__session__")             # some Jupyter builds expose the .ipynb path
    if p: cands.append(os.path.dirname(os.path.abspath(p)))
    cands.append(os.getcwd())                    # interactive Jupyter / nbconvert cwd = notebook dir
    for d in dict.fromkeys(cands):               # de-dup, keep order
        if d and os.path.isfile(os.path.join(d, "lib", "seekdb_demo.py")):
            return os.path.join(d, "lib")
    return os.path.join(os.getcwd(), "lib")      # last resort
_LIB = _find_lib(); sys.path.insert(0, _LIB)
import seekdb_demo as sk; importlib.reload(sk)
import pandas as pd
import matplotlib
%matplotlib inline
import matplotlib.pyplot as plt

# ---- AMD showcase plot theme ----
AMD_RED, CPU_GRAY = "#ED1C24", "#5B6770"
plt.rcParams.update({
    "figure.facecolor": "white", "axes.facecolor": "white",
    "axes.grid": True, "grid.alpha": 0.25, "axes.edgecolor": "#cccccc",
    "font.size": 11, "axes.titlesize": 12, "axes.titleweight": "bold",
    "axes.spines.top": False, "axes.spines.right": False,
})
pd.set_option("display.max_columns", None)
print("imports + theme ready.  lib =", _LIB)
```

## 代码块 3：第 1 节：环境与硬件自检

```python
gi = sk.gpu_info(); si = sk.stack_info()
def _row(k, v): return {"项 / item": k, "值 / value": v}
env = pd.DataFrame([
    _row("GPU",               gi["name"]),
    _row("架构 arch",         "%s / RDNA3" % gi["gfx"]),
    _row("显存 VRAM",         ("%d MB total, %s MB used" % (gi["vram_total_mb"], gi["vram_used_mb"])) if gi["vram_total_mb"] else "n/a"),
    _row("ROCm",              gi["rocm"] or "n/a"),
    _row("seekdb 二进制 bin", "OK" if si["seekdb_bin"] else "MISSING"),
    _row("cuVS 桥接库 bridge","OK" if si["bridge"] else "MISSING"),
    _row("libcuvs 链接 link", "%d   (>0 = GPU backend compiled in)" % si["libcuvs_linked"]),
    _row("SEEKDB_HOME",       sk.SEEKDB),
])
env
```

## 代码块 4：第 3 节（上）：启动 seekdb

```python
from pathlib import Path
import time
if "sk" not in globals():
    raise RuntimeError("Run the initialization/import cell first, or use Restart Kernel and Run All Cells.")
for _name in ("_DATA_READY", "df", "cpu", "gpu", "r_cpu", "r_gpu", "score"):
    globals().pop(_name, None)
import json

_OBSERVER_BASE = Path.cwd() / "run" / "obs1"
if not sk.start_observer(str(_OBSERVER_BASE)):
    raise RuntimeError("seekdb observer failed to start")
print("seekdb version:", sk.version())

_telemetry_file = _OBSERVER_BASE / "run" / "telemetry.json"
_deadline = time.monotonic() + 15
_last_error = None

while time.monotonic() < _deadline:
    try:
        _telemetry_text = _telemetry_file.read_text(encoding="utf-8")
        telemetry = json.loads(_telemetry_text)
        break
    except (FileNotFoundError, json.JSONDecodeError, OSError) as exc:
        _last_error = exc
        time.sleep(0.25)
else:
    _log_file = _OBSERVER_BASE / "log" / "seekdb.log"
    _diagnostics = []
    if _log_file.is_file():
        _diagnostics = [
            line
            for line in _log_file.read_text(errors="replace").splitlines()
            if "telemetry" in line.lower()
        ][-20:]
    raise RuntimeError(
        f"telemetry.json was not generated within 15s: {_telemetry_file}\n"
        f"Last error: {_last_error}\n"
        f"Telemetry diagnostics:\n" + "\n".join(_diagnostics)
    )

print()
print(f"Complete telemetry.json: {_telemetry_file}")
print("telemetry.json parsed successfully; full runtime identifiers remain in the local file.")
```

## 代码块 5：第 3 节（下）：准备数据及等待索引

```python
if "sk" not in globals():
    raise RuntimeError("Run the initialization/import cell first, or use Restart Kernel and Run All Cells.")
for _name in ("_DATA_READY", "df", "cpu", "gpu", "r_cpu", "r_gpu", "score"):
    globals().pop(_name, None)
N_INDEX     = 10000
PROBE_SIZES = (100, 500, 1000, 2000, 4000)
ok, err = sk.setup_data(N_INDEX, PROBE_SIZES)
assert ok, "data load failed: %s" % err[:300]
print("data loaded: t_cuvs (lib=cuvs, GPU) + t_vsag (lib=vsag, CPU), %d vectors each" % N_INDEX)
waited = sk.wait_index_ready("t_cuvs", timeout=90)
print("vector index ready (~%ds)  |  t_cuvs = %s rows,  t_vsag = %s rows" % (
    waited,
    sk.sql_rows("use vec; select count(*) from t_cuvs")[0][0],
    sk.sql_rows("use vec; select count(*) from t_vsag")[0][0]))
_DATA_READY = True
```

## 代码块 6：第 4 节：单条查询对照

```python
if "sk" not in globals():
    raise RuntimeError("Run the initialization/import cell first, or use Restart Kernel and Run All Cells.")
sk.require_notebook_state(globals(), ['_DATA_READY'], 'Run section 3 startup AND data preparation first.')
globals().pop('cpu', None)
globals().pop('gpu', None)
globals().pop('score', None)
for _ in range(2):
    sk.single_query("t_cuvs"); sk.single_query("t_vsag")           # warm up
cpu = min(sk.single_query("t_vsag") for _ in range(5)) * 1000
gpu = min(sk.single_query("t_cuvs") for _ in range(5)) * 1000
print("single query   CPU (lib=vsag): %.2f ms    GPU (lib=cuvs): %.2f ms" % (cpu, gpu))

fig, ax = plt.subplots(figsize=(5.4, 3.6))
bars = ax.bar(["CPU (VSAG)", "GPU (cuVS)"], [cpu, gpu], color=[CPU_GRAY, AMD_RED], width=0.55)
ax.bar_label(bars, fmt="%.2f ms", padding=3, fontweight="bold")
ax.set_ylabel("latency (ms)"); ax.set_title("Single-query latency  (lower = better)")
ax.set_ylim(0, max(cpu, gpu) * 1.3)
plt.tight_layout(); plt.show()
print("=> 单查 GPU 无收益。真正的加速在 *批量* -> 下一节。")
```

## 代码块 7：第 5 节（上）：执行批量测试

```python
if "sk" not in globals():
    raise RuntimeError("Run the initialization/import cell first, or use Restart Kernel and Run All Cells.")
sk.require_notebook_state(globals(), ['_DATA_READY', 'PROBE_SIZES'], 'Run section 3 data preparation first.')
globals().pop('df', None)
globals().pop('score', None)
df = sk.sweep(PROBE_SIZES)
df
```

## 代码块 8：第 5 节（下）：绘图和导出 CSV

```python
if "sk" not in globals():
    raise RuntimeError("Run the initialization/import cell first, or use Restart Kernel and Run All Cells.")
sk.require_notebook_state(globals(), ['df'], 'Run section 5: df = sk.sweep(PROBE_SIZES). A saved output is not live data.')
fig, ax = plt.subplots(1, 3, figsize=(15.5, 4.3))
ax[0].plot(df.N, df.cpu_ms, "o-", color=CPU_GRAY, lw=2, label="CPU (VSAG nested-loop)")
ax[0].plot(df.N, df.gpu_ms, "s-", color=AMD_RED, lw=2, label="GPU (cuVS batch_knn)")
ax[0].set_xlabel("batch size N (probes)"); ax[0].set_ylabel("wall time (ms)")
ax[0].set_title("Wall time vs N  (lower = better)"); ax[0].legend()

ax[1].plot(df.N, df.speedup, "^-", color=AMD_RED, lw=2); ax[1].axhline(1, ls="--", c="gray")
for x, y in zip(df.N, df.speedup):
    ax[1].annotate("%.1fx" % y, (x, y), textcoords="offset points", xytext=(0, 7),
                   ha="center", fontweight="bold", fontsize=9)
ax[1].set_xlabel("batch size N (probes)"); ax[1].set_ylabel("speedup (CPU / GPU)")
ax[1].set_title("GPU batch speedup  (higher = better)")

ax[2].plot(df.N, df.cpu_ms / df.N, "o-", color=CPU_GRAY, lw=2, label="CPU")
ax[2].plot(df.N, df.gpu_ms / df.N, "s-", color=AMD_RED, lw=2, label="GPU (amortized)")
ax[2].set_xlabel("batch size N (probes)"); ax[2].set_ylabel("ms / probe")
ax[2].set_title("Per-probe cost  (GPU collapses)"); ax[2].legend()

fig.suptitle("seekdb x hipVS  -  GPU batch vector search on AMD gfx1100 (RDNA3)",
             fontweight="bold", fontsize=13)
plt.tight_layout(); os.makedirs("results", exist_ok=True)
plt.savefig("results/seekdb_hipvs_speedup.png", dpi=120, bbox_inches="tight"); plt.show()
df.to_csv("results/summary.csv", index=False)
peak = df.speedup.max(); peakN = int(df.N.iloc[df.speedup.idxmax()])
print("peak speedup: %.1fx at N=%d   ->  saved results/seekdb_hipvs_speedup.png + summary.csv" % (peak, peakN))
```

## 代码块 9：第 6 节：召回率测量

```python
if "sk" not in globals():
    raise RuntimeError("Run the initialization/import cell first, or use Restart Kernel and Run All Cells.")
sk.require_notebook_state(globals(), ['_DATA_READY'], 'Run section 3 data preparation first.')
globals().pop('r_cpu', None)
globals().pop('r_gpu', None)
globals().pop('score', None)
sk.cpu_lateral(100); sk.gpu_batch(100)                    # fill cpu_out / bk_out
r_cpu = sk.recall_at_k("cpu_out"); r_gpu = sk.recall_at_k("bk_out")
print("recall@10   CPU (VSAG): %.4f    GPU (cuVS batch): %.4f" % (r_cpu, r_gpu))

fig, ax = plt.subplots(figsize=(5.4, 3.6))
bars = ax.bar(["CPU (VSAG)", "GPU (cuVS)"], [r_cpu, r_gpu], color=[CPU_GRAY, AMD_RED], width=0.55)
ax.bar_label(bars, fmt="%.3f", padding=3, fontweight="bold")
ax.set_ylabel("recall@10"); ax.set_title("Recall@10 vs ground truth  (higher = better)")
ax.set_ylim(0, 1.08)
plt.tight_layout(); plt.show()
print("Recall values above are measured for this configuration; single/batch agreement is checked below.")
```

## 代码块 10：第 7 节：生成总结记分牌

```python
if "sk" not in globals():
    raise RuntimeError("Run the initialization/import cell first, or use Restart Kernel and Run All Cells.")
sk.require_notebook_state(globals(), ['df', 'cpu', 'gpu', 'r_cpu', 'r_gpu'], 'Complete sections 3, 4, 5 and 6 before generating the scoreboard.')
peak = df.speedup.max(); peakN = int(df.N.iloc[df.speedup.idxmax()])
gpu_qps = int(df.gpu_probes_s.max()); cpu_qps = int(df.cpu_probes_s.max())
score = pd.DataFrame([
    {"指标 metric": "单查 single-query",       "结果 result": "GPU %.2f ms  vs  CPU %.2f ms   (no win)" % (gpu, cpu)},
    {"指标 metric": "批量峰值加速 batch peak",  "结果 result": "%.1fx  @ N=%d" % (peak, peakN)},
    {"指标 metric": "GPU 批量吞吐 throughput",  "结果 result": "%d probes/s   (CPU %d/s)" % (gpu_qps, cpu_qps)},
    {"指标 metric": "召回 recall@10",           "结果 result": "cuVS %.3f   vs   VSAG %.3f" % (r_gpu, r_cpu)},
    {"指标 metric": "驱动 how",                 "结果 result": "lib=cuvs + dbms_vector.batch_knn  (no env var)"},
])
score
```

## 代码块 11：最后：完整性检查并导出 JSON

```python
if "sk" not in globals():
    raise RuntimeError("Run the initialization/import cell first, or use Restart Kernel and Run All Cells.")
sk.require_notebook_state(globals(), ['df', 'cpu', 'gpu', 'r_cpu', 'r_gpu'], 'Complete the preceding measurement cells first.')
# Additional checks: fail on incomplete data or missing query results.
import math
for table, expected in [("t_cuvs",10000),("t_vsag",10000)]+[("probes_%d" % v,v) for v in PROBE_SIZES]:
    actual=int(sk.sql_rows("use vec; select count(*) from "+table)[0][0])
    assert actual==expected,(table,actual,expected)
for table in ("cpu_out","bk_out"):
    count=int(sk.sql_rows("use vec; select count(*) from "+table)[0][0])
    assert count==1000,(table,count)
    groups=sk.sql_rows("use vec; select probe_id,count(*),count(distinct neighbor_id) from "+table+" group by probe_id")
    assert len(groups)==100 and all(int(v[1])==10 and int(v[2])==10 for v in groups)
assert list(df.N)==list(PROBE_SIZES)
assert all(math.isfinite(float(v)) and float(v)>0 for name in ("cpu_ms","gpu_ms","speedup") for v in df[name])
# Measure single/batch agreement instead of assuming it.
batch={}
for pid,nid in sk.sql_rows("use vec; select probe_id,neighbor_id from bk_out"):
    batch.setdefault(int(pid),set()).add(int(nid))
matches=0
single_hits=0
gt=sk._load_gt()
for pid,pv in sk.sql_rows("use vec; select id,pv from probes_100 order by id"):
    got=sk.sql_rows("use vec; select c1 from t_cuvs order by l2_distance(c2,'%s') approximate limit 10" % pv)
    single=set(int(row[0]) for row in got)
    assert len(single)==10 and all(0<=v<N_INDEX for v in single)
    matches += single==batch[int(pid)]
    single_hits += len(single & gt[int(pid)])
report={"completed":True,"rows":N_INDEX,"probe_sizes":list(PROBE_SIZES),
        "single_query_ms":{"cpu":cpu,"gpu":gpu},"batch_runs":df.to_dict(orient="records"),
        "recall_at_10":{"cpu":r_cpu,"gpu":r_gpu},
        "sql_results_validated":True,"gpu_single_batch_identical_queries":matches,"gpu_single_batch_checked_queries":100,"gpu_single_query_recall_at_10":single_hits/1000,"scope":"course preinstalled batch-query demo; not HNSW construction"}
Path("results/summary.json").write_text(json.dumps(report,indent=2),encoding="utf-8")
print(json.dumps(report,indent=2))
```

## 如何判定本次确实执行成功

- 第 2 块打印的 `lib` 应为 `/workspace/seekdb-hipvs-batch-demo/lib`，不再是 `/workspace/seekdb_hipvs_notebook/lib`。
- 启动、数据准备单元均无红色异常；两张索引表各 10000 行。查询必须等待这两块完成。
- 第 7 块实际生成当前内核的 `df`，再运行绘图、召回及记分牌；重启前留下的表格不能替代这个步骤。
- 最后完整性检查成功，生成 `results/summary.json`；耗时、加速比和召回率以当前实际输出为准，不填固定数字。
- 出现第一个红色报错就停止后续单元。端口被其他服务占用时按提示检查服务归属，不要直接结束所有 seekdb 进程。

## 与平台提测的关系

这份 Notebook 使用课程镜像预装的 seekdb/bridge，主要验证批量查询；题目要求的是在 seekdb 中加速 HNSW 索引构建。Notebook 的 NameError 确实要修，但仅凭它不能证明平台的失败原因。网页提测表单提交的是 Git 仓库的分支和 commit；在云端手动改 Notebook，不会自动同步到该 commit。

Notebook 通过不代表源码编译、构建加速、平台入口及全部测评通过。没有平台失败日志时，不能将二者直接等同。此文件仅增加代码填写说明，不修改已经验证过的源码及实测记录。
