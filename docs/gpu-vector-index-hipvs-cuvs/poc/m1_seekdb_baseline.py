#!/usr/bin/env python3
# M1: seekdb CPU (VSAG/HNSW) baseline via pyseekdb (embedded).
# Shared dataset under /work/datasets (reused by hipVS M2 + M4). L2-normalized so
# top-k ranking is identical under L2 / cosine / inner-product.
# NOTE: seekdb builds the vector index ASYNCHRONOUSLY (Change Stream). Querying
# right after a bulk add returns near-random results -> we WAIT for the index.
import os, time, json, numpy as np, pyseekdb

DS = "/work/datasets"; RES = "/work/results"
os.makedirs(DS, exist_ok=True); os.makedirs(RES, exist_ok=True)
N, DIM, Q, K, SEED = 10000, 128, 100, 10, 42
bpath = f"{DS}/base_{N}x{DIM}.npy"; qpath = f"{DS}/query_{Q}x{DIM}.npy"; gpath = f"{DS}/gt_{Q}x{K}.npy"

def l2norm(x):
    return x / np.linalg.norm(x, axis=1, keepdims=True).clip(1e-9)

if os.path.exists(bpath) and os.path.exists(qpath) and os.path.exists(gpath):
    base = np.load(bpath); query = np.load(qpath); gt = np.load(gpath)
else:
    rng = np.random.default_rng(SEED)
    base = l2norm(rng.standard_normal((N, DIM)).astype("float32"))
    query = l2norm(rng.standard_normal((Q, DIM)).astype("float32"))
    gt = np.argsort(-(query @ base.T), axis=1)[:, :K].astype("int64")
    np.save(bpath, base); np.save(qpath, query); np.save(gpath, gt)
print("dataset:", base.shape, query.shape, "gt:", gt.shape)

DIST = "l2"
cfg = pyseekdb.HNSWConfiguration(dimension=DIM, distance=DIST, ef_construction=200, ef_search=200)
client = pyseekdb.Client(path=f"{RES}/m1_seekdb.db")
try:
    client.delete_collection("m1")
except Exception:
    pass
col = client.get_or_create_collection(name="m1", configuration=cfg, embedding_function=None)
print("index: HNSW/vsag dim=%d distance=%s ef_construction=200 ef_search=200" % (DIM, DIST))
ids = [str(i) for i in range(N)]

t0 = time.time()
for i in range(0, N, 1000):
    col.add(ids=ids[i:i + 1000], embeddings=base[i:i + 1000].tolist())
build = time.time() - t0
print("added:", col.count(), "add_s=%.2f" % build)

def probe(nq=10):
    h = 0
    for i in range(nq):
        got = set(int(x) for x in col.query(query_embeddings=[query[i].tolist()], n_results=K)["ids"][0])
        h += len(got & set(gt[i].tolist()))
    return h / (nq * K)

waited = 0.0; pr = probe(10)
while pr < 0.5 and waited < 90:
    time.sleep(3); waited += 3; pr = probe(10)
print("index ready after ~%ds (probe recall=%.3f)" % (waited, pr))

lat = []; hit = 0
for i in range(Q):
    t = time.time()
    got = [int(x) for x in col.query(query_embeddings=[query[i].tolist()], n_results=K)["ids"][0]]
    lat.append((time.time() - t) * 1000)
    hit += len(set(got) & set(gt[i].tolist()))
recall = hit / (Q * K)
lat = np.array(lat)
p50, p99, mean = float(np.percentile(lat, 50)), float(np.percentile(lat, 99)), float(lat.mean())
print("recall@%d=%.4f p50=%.2fms p99=%.2fms" % (K, recall, p50, p99))

md = f"""# M1 - seekdb CPU baseline (pyseekdb embedded)
- dataset: N={N}, dim={DIM}, Q={Q}, K={K}, seed={SEED} (L2-normalized random Gaussian; GT=brute-force top-{K})
- engine: pyseekdb {pyseekdb.__version__}, HNSW/vsag, distance={DIST}, ef_construction=200, ef_search=200 (CPU)
- add(build) {build:.2f}s ({N/build:.0f} vec/s); async index ready ~{int(waited)}s after add
- recall@{K} = {recall:.4f}
- query latency: p50={p50:.2f}ms p99={p99:.2f}ms mean={mean:.2f}ms
- NOTE: querying immediately after bulk add gives near-random recall (async Change Stream index build); must wait for index.
"""
open(f"{RES}/M1_seekdb_baseline.md", "w").write(md)
json.dump({"N": N, "dim": DIM, "Q": Q, "K": K, "distance": DIST, "add_s": build,
           "index_wait_s": waited, "recall": recall, "p50_ms": p50, "p99_ms": p99,
           "mean_ms": mean, "pyseekdb": pyseekdb.__version__},
          open(f"{RES}/M1_seekdb_baseline.json", "w"), indent=2)
print("WROTE", f"{RES}/M1_seekdb_baseline.md")
