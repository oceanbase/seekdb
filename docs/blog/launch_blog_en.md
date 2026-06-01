# You're Using the Wrong Benchmark to Choose a Vector Database for Your Agent

**The real workload of an Agent is a streaming workload — most vector databases weren't designed for it.**

If you're choosing a vector database for your Agent, you're probably looking at ann-benchmarks or the performance comparisons published by each vendor. Those tests run a workload like this: bulk-import all the data, build the index, then run read-only queries.

**That's not an Agent's workload.**

An Agent's real workload looks like this:

```python
for step in agent.run():
    memory.write(step.observation)        # continuous writes
    relevant = memory.search(step.query)  # retrieval milliseconds later
```

Writes and retrieval happen simultaneously, separated by milliseconds, and concurrently. This workload has a name — streaming workload. [VectorDBBench](https://github.com/zilliztech/VectorDBBench) designed its StreamingPerformanceCase specifically for this: continuous writes at a fixed rate + concurrent queries, exactly like a production Agent.

VectorDBBench is maintained by Zilliz (the company behind Milvus) and is a third-party open-source benchmark framework. We used it to test 6 mainstream vector databases.

---

## A Metric Everyone Ignores: How Much Does Your P99 Spike Under Concurrency?

Test conditions: Cohere 10M dataset (768 dimensions), 16 vCPU / 64 GiB, uniform HNSW index parameters (M=16 / ef_construction=256 / ef_search=200), continuous writes at 500 rows/sec.

![seekdb streaming benchmark: 6 vector databases compared](../../images/benchmark_full.svg)

Most people look at benchmarks for QPS and serial latency only. But Agents don't run single-threaded in production. **What actually determines your SLA is the concurrent P99 — and how much it spikes as concurrency increases.**

Look at the "P99 Jitter" group in the chart:

- **ES: 10.3x** — serial P99 is only 5.2ms (faster than seekdb), but once concurrency kicks in it jumps to 53.6ms
- **Milvus: 9.7x** — serial 15.9ms, concurrent spikes straight to 153.6ms
- **seekdb: 1.1x** — from 19.7ms to 21.7ms, barely moves

This isn't a parameter-tuning issue — it's an architecture issue. The next section explains why.

> Full test scripts and configuration: [github.com/oceanbase/vdb-streambench](https://github.com/oceanbase/vdb-streambench). PRs to add more products are welcome.

---

## Why P99 Explodes Under Streaming Workloads

Milvus, ES, and Qdrant perform excellently in the scenarios they were designed for — bulk import + read-only queries. But streaming writes expose a structural problem: they continuously produce new segments. Queries must fan out to N segments, run knn on each, then merge results. This is barely manageable single-threaded, **but once concurrency ramps up, N segments x M query threads contend for CPU, and P99 skyrockets.**

**Most vector databases see their segment count balloon with streaming writes, making concurrent query contention progressively worse. seekdb's index count is fixed (always exactly two), so this problem doesn't occur.**

Specifically, seekdb v1.3.0 introduced two mechanisms designed for streaming workloads:

**First, the write path never touches the index.** After transaction commit, it writes only the redo log and returns. A separate Change Stream pipeline asynchronously consumes the redo log in the background, writing vectors into the in-memory delta HNSW index. Writes and index building are physically decoupled — writes are never blocked by index construction.

**Second, the query path always hits exactly two indexes.** seekdb maintains a delta HNSW (incremental layer, receives new writes) and a snapshot HNSW (main base layer), similar to the tiered approach of an LSM-Tree. Queries run one knn search on each index and merge results — no matter how much data has been written, the index count doesn't grow, and concurrent queries don't contend.

We hit this problem ourselves. The seekdb v1.2.0 group in the chart — 69 QPS, concurrent P99 of 410ms — that was our performance before the architecture rewrite. The old version's write path built indexes synchronously, the exact same problem as the traditional architecture described above. After the rewrite, same product, 22x QPS improvement, P99 latency reduced by 19x — all from these two mechanisms.

---

## Agents Need More Than Speed — They Need an Undo Button

Performance covered. But anyone who's built Agents knows there's another pain point: Agents need to tentatively modify data (update memory, run experiments, potentially corrupt tables). **You need a safe sandbox and rollback mechanism.**

Most vector databases have no concept of this. seekdb implements Copy-on-Write directly in the kernel:

```sql
-- Sub-second snapshot, no data copying
FORK DATABASE agent_state TO sandbox_42;

-- Agent experiments freely in the sandbox
USE sandbox_42;
INSERT INTO memory (embedding, content) VALUES ('[0.1,...]', 'new observation');

-- Experiment succeeded → merge back to mainline
MERGE TABLE sandbox_42.memory INTO agent_state.memory STRATEGY THEIRS;

-- Experiment failed → discard it, mainline unaffected
DROP DATABASE sandbox_42;
```

This is kernel-level COW, not application-layer snapshot/restore. Fork completes in seconds without copying data, and each sandbox is a fully writable database (table schemas, vector indexes, auto-increment columns all work normally). Three conflict strategies (`FAIL` / `THEIRS` / `OURS`) give you precise control over how much of the Agent's modifications can be trusted. Both `FORK DATABASE` and `FORK TABLE` granularities are supported.

---

## Hybrid Retrieval in a Single SQL Statement

Agent retrieval is rarely pure vector similarity. You often need to simultaneously filter by author, time range, and add full-text matching. In seekdb, that's a single SQL statement:

```sql
SELECT id, title, l2_distance(emb, '[0.12,0.34,...]') AS dist
FROM docs
WHERE MATCH(content) AGAINST ('quarterly report')
  AND author_id = 42
  AND created_at > '2026-01-01'
ORDER BY dist APPROXIMATE LIMIT 10;
```

Vector + full-text + scalar filtering are pushed down within the same execution plan — no need to assemble multiple query results on the client side. Fully MySQL-protocol compatible; LangChain / LlamaIndex / Dify / any MySQL client connects directly.

---

## Try It in 30 Seconds

```bash
pip install -U pyseekdb
```

```python
import pyseekdb

client = pyseekdb.Client(path="./agent_state.db")
memory = client.get_or_create_collection(name="episodic")

# Round 1: write Agent observations
memory.upsert(
    ids=["1", "2", "3"],
    documents=[
        "user prefers dark mode",
        "user speaks English and Chinese",
        "user timezone is UTC+8",
    ],
)
memory.refresh_index()

results = memory.query(query_texts="ui preferences?", n_results=1)
print(results["documents"])
# -> [['user prefers dark mode']]

# Round 2: write new observation, queryable immediately after refreshing index
memory.upsert(ids=["4"], documents=["user saw pricing page 3 times today"])
memory.refresh_index()

results = memory.query(query_texts="purchase intent signals", n_results=1)
print(results["documents"])
# -> [['user saw pricing page 3 times today']]
```

No server needed, no schema needed — embedded mode runs in-process. Writes go through the async index pipeline (same architecture as server mode); call `refresh_index()` once when you need immediate queryability. Switching to server or distributed mode requires changing only one connection parameter. You can also use [Cloud for an install-free trial](https://d0.seekdb.ai) (no signup, free for 7 days, one curl command).

---

## About seekdb

seekdb is fully open source (Apache 2.0), developed by the [OceanBase](https://en.oceanbase.com/) team. You may already be using OceanBase — it runs in production at Alipay, Taobao, DiDi, Xiaomi, and more. seekdb inherits the same storage engine and SQL executor, focused on the vector + relational hybrid workload for Agent scenarios — with 2,500+ GitHub stars since launch and integrations with LangChain / LlamaIndex / Dify / Coze and other major frameworks.

---

If you're choosing a database for your Agent — take 30 seconds to run the demo above.

**⭐ [github.com/oceanbase/seekdb](https://github.com/oceanbase/seekdb)** — a star helps more people discover this project and motivates us to keep investing in it.

Questions or want to discuss your Agent use case: [GitHub Issues](https://github.com/oceanbase/seekdb/issues) · [GitHub Discussions](https://github.com/oceanbase/seekdb/discussions)
