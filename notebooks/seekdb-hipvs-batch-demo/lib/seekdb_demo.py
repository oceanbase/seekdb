"""seekdb + hipVS notebook demo helpers.

Everything runs via the `mysql` CLI against the local observer on port 2881.
The GPU path is driven declaratively by `lib=cuvs` indexes and by the
`dbms_vector.batch_knn` batch operator -- no model API key or GPU-toggle environment variable is needed.
"""
import os, subprocess, time, struct, re, sys
from pathlib import Path

SEEKDB   = os.environ.get("SEEKDB_HOME", "/seekdb_workspace")
DATASETS = os.path.join(SEEKDB, "datasets")
SCRIPTS  = os.path.join(SEEKDB, "scripts")
MYSQL    = ["mysql", "-h127.0.0.1", "-P2881", "-uroot"]


def sh(cmd, inp=None):
    result = subprocess.run(cmd, input=inp, capture_output=True, text=True, timeout=660)
    if cmd and cmd[0] == "mysql" and result.returncode:
        raise RuntimeError("Notebook SQL failed: " + result.stderr[:1000])
    return result


def sql(q, timed=False):
    t = time.perf_counter()
    r = sh(MYSQL + ["-e", "SET ob_query_timeout=600000000;" + q])
    dt = time.perf_counter() - t
    return (dt, r.returncode) if timed else (r.returncode, r.stdout, r.stderr)


def sql_rows(q):
    r = sh(MYSQL + ["-N", "-e", q])
    return [ln.split("\t") for ln in r.stdout.strip().splitlines() if ln]


# ---------------- environment / hardware ----------------
def gpu_info():
    """Best-effort AMD GPU + ROCm info parsed from amd-smi; degrades gracefully."""
    info = {"name": "AMD Radeon (gfx1100)", "gfx": "gfx1100",
            "vram_total_mb": None, "vram_used_mb": None, "rocm": None}
    for p in ("/opt/rocm/.info/version", "/opt/rocm/.info/version-dev"):
        if os.path.exists(p):
            info["rocm"] = open(p).read().strip().splitlines()[0]
            break
    st = sh(["bash", "-lc", "amd-smi static 2>/dev/null"]).stdout
    m = re.search(r"MARKET_NAME:\s*(.+)", st)
    if m and m.group(1).strip():
        info["name"] = m.group(1).strip()
    m = re.search(r"TARGET_GRAPHICS_VERSION:\s*(gfx\w+)", st)
    if m:
        info["gfx"] = m.group(1).strip()
    mu = sh(["bash", "-lc", "amd-smi metric --mem-usage 2>/dev/null"]).stdout
    m = re.search(r"TOTAL_VRAM:\s*(\d+)", mu)
    if m:
        info["vram_total_mb"] = int(m.group(1))
    m = re.search(r"USED_VRAM:\s*(\d+)", mu)
    if m:
        info["vram_used_mb"] = int(m.group(1))
    return info


def vram_used_mb():
    mu = sh(["bash", "-lc", "amd-smi metric --mem-usage 2>/dev/null"]).stdout
    m = re.search(r"USED_VRAM:\s*(\d+)", mu)
    return int(m.group(1)) if m else None


def stack_info():
    """Confirm the GPU backend is compiled into the shipped seekdb binary."""
    r = sh(["bash", "-lc", "ldd %s/bin/seekdb 2>/dev/null | grep -c -i cuvs" % SEEKDB])
    try:
        linked = int(r.stdout.strip() or "0")
    except Exception:
        linked = 0
    return {"seekdb_bin": os.path.exists(SEEKDB + "/bin/seekdb"),
            "bridge": os.path.exists(SEEKDB + "/bridge/libseekdb_cuvs_bridge.so"),
            "libcuvs_linked": linked}


# ---------------- observer ----------------
def start_observer(base=None):
    base = base or str(Path.cwd()/"run/obs1")
    r = sh([sys.executable, str(Path(__file__).with_name("start_server.py")), str(base)])
    print((r.stdout + r.stderr).strip())
    return r.returncode == 0


def version():
    rows = sql_rows("select version()")
    return rows[0][0] if rows else "(not connected)"


# ---------------- data ----------------
def require_notebook_state(namespace, names, instruction):
    """Report an interrupted/out-of-order notebook before using stale outputs."""
    missing = [name for name in names if name not in namespace]
    if missing:
        raise RuntimeError("Notebook prerequisites missing: %s. %s" % (", ".join(missing), instruction))


def _probe_vector(probe_id=1):
    rows = sql_rows("use vec; select pv from probes_100 where id=%d" % probe_id)
    if not rows or not rows[0] or rows[0][0] in (None, "NULL", ""):
        raise RuntimeError("Probe %d is missing from probes_100. Run section 3 data preparation successfully before querying." % probe_id)
    return rows[0][0]


def _load_f32(path, n, dim):
    raw = open(path, "rb").read(n * dim * 4)
    return struct.unpack("<%df" % (n * dim), raw)


def _lit(vals, i, dim):
    return "[" + ",".join("%.6f" % x for x in vals[i * dim:(i + 1) * dim]) + "]"


def setup_data(n_index=10000, probe_sizes=(100, 500, 1000, 2000, 4000), dim=128):
    """Create t_cuvs (lib=cuvs) + t_vsag (lib=vsag) over the same n_index base
    vectors, plus tiled probe tables and the output tables. Loads via mysql CLI."""
    base = _load_f32(os.path.join(DATASETS, "base.f32"), n_index, dim)
    qy   = _load_f32(os.path.join(DATASETS, "query.f32"), 100, dim)
    o = ["create database if not exists vec;", "use vec;",
         "alter system set ob_vector_memory_limit_percentage=30;",
         "drop table if exists t_cuvs; drop table if exists t_vsag;",
         "create table t_cuvs(c1 int primary key, c2 vector(%d), vector index i(c2) "
         "with (distance=l2,type=hnsw,lib=cuvs,m=16,ef_construction=200,ef_search=64));" % dim,
         "create table t_vsag(c1 int primary key, c2 vector(%d), vector index i(c2) "
         "with (distance=l2,type=hnsw,lib=vsag,m=16,ef_construction=200,ef_search=64));" % dim]
    for t in ("t_cuvs", "t_vsag"):
        for s in range(0, n_index, 1000):
            vals = ",".join("(%d,%r)" % (i, _lit(base, i, dim))
                            for i in range(s, min(s + 1000, n_index)))
            o.append("insert into %s values %s;" % (t, vals))
    for N in probe_sizes:
        o.append("drop table if exists probes_%d;" % N)
        o.append("create table probes_%d(id int primary key, pv vector(%d));" % (N, dim))
        for s in range(0, N, 500):
            vals = ",".join("(%d,%r)" % (i, _lit(qy, i % 100, dim))
                            for i in range(s, min(s + 500, N)))
            o.append("insert into probes_%d values %s;" % (N, vals))
    o.append("drop table if exists bk_out; create table bk_out"
             "(probe_id bigint,neighbor_id bigint,distance float,rk int);")
    o.append("drop table if exists cpu_out; create table cpu_out"
             "(probe_id bigint,neighbor_id bigint);")
    r = sh(MYSQL, inp="\n".join(o) + "\n")
    return r.returncode == 0, r.stderr


def wait_index_ready(table="t_cuvs", k=10, timeout=90, floor=15):
    """Wait for the async vector-index build: a short floor, then poll a probe
    query until its top-k stabilises (more robust than a fixed sleep)."""
    vec = _probe_vector()
    time.sleep(floor)
    t0 = time.time(); last = None
    while vec and time.time() - t0 < timeout:
        rows = sql_rows("use vec; select c1 from %s order by l2_distance(c2,%s) "
                        "approximate limit %d" % (table, vec, k))
        if len(rows) >= k:
            cur = tuple(r[0] for r in rows[:k])
            if cur == last:
                return round(time.time() - t0 + floor)
            last = cur
        time.sleep(3)
    raise TimeoutError("Vector index %s did not return stable top-%d results within %ss; data preparation is incomplete." % (table, k, timeout))


# ---------------- benchmarks ----------------
def cpu_lateral(N, k=10):
    """CPU baseline: nested-loop similarity join on the lib=vsag (CPU/VSAG) index."""
    q = ("use vec; truncate cpu_out; insert into cpu_out "
         "select p.id, n.c1 from probes_%d p, lateral("
         "select c1 from t_vsag order by l2_distance(c2,p.pv) approximate limit %d) n;" % (N, k))
    elapsed = sql(q, timed=True)[0]
    output_table = "cpu_out" if "truncate cpu_out" in q else "bk_out"
    output_count = int(sql_rows("use vec; select count(*) from " + output_table)[0][0])
    if output_count != N * k:
        raise RuntimeError("Incomplete batch result: %s %d != %d" % (output_table, output_count, N * k))
    return elapsed


def gpu_batch(N, k=10):
    """GPU batch operator: one dbms_vector.batch_knn call for all N probes."""
    q = 'use vec; call dbms_vector.batch_knn("t_cuvs","probes_%d",%d,"bk_out");' % (N, k)
    elapsed = sql(q, timed=True)[0]
    output_table = "cpu_out" if "truncate cpu_out" in q else "bk_out"
    output_count = int(sql_rows("use vec; select count(*) from " + output_table)[0][0])
    if output_count != N * k:
        raise RuntimeError("Incomplete batch result: %s %d != %d" % (output_table, output_count, N * k))
    return elapsed


def sweep(probe_sizes=(100, 500, 1000, 2000, 4000), k=10, warm=True):
    import pandas as pd
    if warm:
        cpu_lateral(min(probe_sizes), k); gpu_batch(min(probe_sizes), k)
    rows = []
    for N in probe_sizes:
        c = cpu_lateral(N, k); g = gpu_batch(N, k)
        rows.append(dict(N=N, cpu_ms=round(c * 1000, 1), gpu_ms=round(g * 1000, 1),
                         speedup=round(c / g, 2),
                         cpu_probes_s=round(N / c), gpu_probes_s=round(N / g)))
    return pd.DataFrame(rows)


def single_query(table, probe_id=1, k=10):
    """Time one APPROXIMATE query (probe vector taken from probes_100)."""
    v = _probe_vector(probe_id)
    q = "use vec; select c1 from %s order by l2_distance(c2,%s) approximate limit %d;" % (table, v, k)
    return sql(q, timed=True)[0]


# ---------------- recall ----------------
def _load_gt(nq=100, k=10):
    raw = open(os.path.join(DATASETS, "gt_100x10.i32"), "rb").read(nq * k * 4)
    g = struct.unpack("<%di" % (nq * k), raw)
    return {q: set(g[q * k:(q + 1) * k]) for q in range(nq)}


def recall_at_k(out_table, id_col="probe_id", nbr_col="neighbor_id", k=10):
    """recall@k of an output table vs ground truth (probe id maps mod 100 into gt)."""
    gt = _load_gt()
    got = {}
    for pid, nid in sql_rows("use vec; select %s,%s from %s" % (id_col, nbr_col, out_table)):
        got.setdefault(int(pid) % 100, []).append(int(nid))
    hit = tot = 0
    for pid, nbrs in got.items():
        for x in nbrs[:k]:
            tot += 1
            hit += (x in gt[pid])
    return round(hit / tot, 4) if tot else 0.0
