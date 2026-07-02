<div align="center">

<picture>
  <source media="(prefers-color-scheme: dark)" srcset="https://mdn.alipayobjects.com/huamei_ytl0i7/afts/img/A*pKqtRILxGioAAAAAQLAAAAgAejCYAQ/original" width="420">
  <source media="(prefers-color-scheme: light)" srcset="https://mdn.alipayobjects.com/huamei_ytl0i7/afts/img/A*6BO4Q6D78GQAAAAAQFAAAAgAejCYAQ/original" width="420">
  <img alt="seekdb logo" src="images/logo.svg" width="420">
</picture>

# **Write. Search. Fork. The State Store for AI Agents.**

<p>
    <a href="https://github.com/oceanbase/seekdb/stargazers">
        <img alt="GitHub Stars" src="https://img.shields.io/github/stars/oceanbase/seekdb?style=flat-square&logo=github&color=yellow" />
    </a>
    <a href="https://github.com/oceanbase/seekdb/releases">
        <img alt="Latest Release" src="https://img.shields.io/github/v/release/oceanbase/seekdb?style=flat-square&color=blue" />
    </a>
    <a href="https://github.com/oceanbase/seekdb/commits">
        <img alt="Commit Activity" src="https://img.shields.io/github/commit-activity/m/oceanbase/seekdb?style=flat-square&color=green" />
    </a>
    <a href="https://github.com/oceanbase/seekdb/graphs/contributors">
        <img alt="Contributors" src="https://img.shields.io/github/contributors/oceanbase/seekdb?style=flat-square&color=orange" />
    </a>
    <a href="https://github.com/oceanbase/seekdb/issues">
        <img alt="Issues" src="https://img.shields.io/github/issues/oceanbase/seekdb?style=flat-square" />
    </a>
    <a href="https://github.com/oceanbase/seekdb/blob/HEAD/LICENSE">
        <img alt="License" src="https://img.shields.io/badge/License-Apache_2.0-blue.svg?style=flat-square" />
    </a>
    <a href="https://pepy.tech/projects/pyseekdb">
        <img alt="Downloads" src="https://static.pepy.tech/badge/pyseekdb" />
    </a>
    <a href="https://discord.gg/74cF8vbNEs">
        <img alt="Join Discord" src="https://img.shields.io/badge/Discord-Join%20Chat-5865F2?logo=discord&style=flat-square" />
    </a>
    <a href="https://seekdb.ai">
        <img alt="Documentation" src="https://img.shields.io/badge/Docs-seekdb.ai-4285F4?style=flat-square&logo=read-the-docs&logoColor=white" />
    </a>
    <a href="https://deepwiki.com/oceanbase/seekdb">
        <img alt="Ask DeepWiki" src="https://deepwiki.com/badge.svg" />
    </a>
    <a href="https://www.linkedin.com/company/oceanbase" target="_blank" rel="noopener noreferrer">
        <img src="https://custom-icon-badges.demolab.com/badge/LinkedIn-0A66C2?logo=linkedin-white&logoColor=fff" alt="follow on LinkedIn">
    </a>
    <a href="https://www.youtube.com/@OceanBaseDB">
        <img alt="YouTube" src="https://img.shields.io/badge/YouTube-red?logo=youtube">
    </a>
</p>

MySQL-compatible · Embedded or Server · Hybrid Vector + Full-text Search · COW Sandbox

⚡ 1,523 QPS streaming write+search (10× Milvus, 3× Elasticsearch)<br>
🌿 FORK/MERGE sandboxes for safe agent exploration<br>
🔍 Vector + full-text + scalar in one SQL query<br>
🐬 Full ACID, MySQL protocol, works with LangChain/LlamaIndex/Dify
VLDB test

**English** | [中文版](README_CN.md) | [日本語](README_JP.md)

[30-Second Try](#30-second-try) · [Quick Start](#quick-start) · [Why seekdb](#why-seekdb-for-agents) · [Ecosystem](#ecosystem--integrations) · [Contributing](#development)

<sub>If you find seekdb useful, consider giving it a <a href="https://github.com/oceanbase/seekdb/stargazers">star</a> — it helps others discover the project.</sub>

---

</div>

## ⚡ Performance at a Glance

<div align="center">
  <img src="images/benchmark.svg" alt="seekdb benchmark: 10.7× the QPS of Milvus, 3.2× of Elasticsearch" width="720" />
</div>

> 📖 [Read the launch blog →](docs/blog/launch_blog_en.md) · 🔁 [Reproduce the benchmark →](https://github.com/oceanbase/vdb-streambench)

---

<a id="30-second-try"></a>

## ⏱️ 30-Second Try

<div align="center">
  <img src="images/demo.gif" alt="seekdb 30-second demo" width="720" />
</div>

```bash
pip install -U pyseekdb   # pyseekdb is the Python SDK for seekdb
```

> 📋 [View demo.py source →](images/demo.py)

No servers, no schemas, no embedding setup. Embedded mode runs in-process; switch to server / OceanBase mode with one line. [More examples →](#more-examples)

---

<a id="why-seekdb-for-agents"></a>

## ✨ Why seekdb for Agents?

### 🔥 Streaming Write + Concurrent Search, Without the P99 Spike

Agent workloads are continuous write + millisecond-later read. seekdb's
**async index pipeline (Change Stream)** decouples DML from index build,
and its **two-level HNSW** (incremental + snapshot) makes newly-written
vectors immediately searchable.

<div align="center">
  <img src="images/architecture.svg" alt="seekdb async index pipeline architecture" width="720" />
</div>

The write path commits and returns *without waiting* on index construction.
The Change Stream pipeline consumes the redo log asynchronously and updates
the delta HNSW. Queries hit both delta and snapshot indexes with fine-grained
read locks — **this is why P99 stays flat under concurrency.**

> **The result: 1,523 QPS with 21.7 ms concurrent P99 — 10.7× the QPS of
> Milvus, and P99 jitter of just 1.1× when concurrency rises (vs ~10×
> for ES / Milvus on the same workload).**

<sub>Source: [`src/share/change_stream/`](src/share/change_stream/) · [`src/share/vector_index/`](src/share/vector_index/)</sub>

### 🌿 Copy-on-Write Sandboxes for Agent Exploration

`FORK DATABASE` snapshots an entire database in seconds — no data copy.
Agents experiment freely (write, query, even break tables); then `MERGE TABLE`
commits the work back, or `DROP DATABASE` discards it. Kernel-level COW,
not application-layer save/restore.

```sql
-- Snapshot in seconds, no data copy
FORK DATABASE agent_state TO agent_sandbox_42;

-- Agent reads/writes freely on the sandbox...
USE agent_sandbox_42;
INSERT INTO memory (session_id, embedding, content) VALUES (...);

-- Accept the work back to mainline (strategies: FAIL / THEIRS / OURS)
MERGE TABLE agent_sandbox_42.memory INTO agent_state.memory STRATEGY THEIRS;
-- ...or throw it away:
DROP DATABASE agent_sandbox_42;
```

<sub>Source: [`tools/deploy/mysql_test/test_suite/fork_table/`](tools/deploy/mysql_test/test_suite/fork_table/)</sub>

### 🔍 Hybrid Search in a Single SQL

Vector + full-text + scalar filter pushed into one execution plan.
No N+1 client-side merging, no glue code to combine results.

```sql
SELECT id, title, l2_distance(emb, '[0.12,0.34,...]') AS dist
FROM docs
WHERE MATCH(content) AGAINST('quarterly report')
  AND author_id = 42
  AND created_at > '2026-01-01'
ORDER BY dist APPROXIMATE LIMIT 10;
```

### 🐬 MySQL-Compatible, ACID, Embeddable

Built on the proven OceanBase SQL engine. Works as an embedded library,
a single-node server, or in the OceanBase distributed cluster. Full ACID,
real-time writes, and the entire MySQL ecosystem out of the box.

---

<a id="quick-start"></a>

## 🎬 Quick Start

### Installation

Choose your platform:

<details open>
<summary><b>☁️ Cloud (Zero Install)</b></summary>

One curl, a running database — no signup, no credit card.

```bash
curl -X POST https://d0.seekdb.ai/api/v1/instances
```

Free for 7 days. [Learn more →](https://d0.seekdb.ai)

</details>

<details open>
<summary><b>🐍 Python (Recommended for AI/ML)</b></summary>

```bash
pip install -U pyseekdb
```

</details>

<details>
<summary><b>🐳 Docker (Quick Testing)</b></summary>

```bash
docker run -d \
  --name seekdb \
  -p 2881:2881 \
  -p 2886:2886 \
  -v ./data:/var/lib/oceanbase \
  oceanbase/seekdb:latest
```
Please refer to the [document](https://github.com/oceanbase/docker-images/blob/main/seekdb/README.md) of this docker image for details.

</details>

<details>
<summary><b>📦 Binary (Standalone)</b></summary>

```bash
# Linux (one-line install, may need sudo)
curl -fsSL https://obportal.s3.ap-southeast-1.amazonaws.com/download-center/opensource/seekdb/seekdb_install.sh | bash

# macOS (Homebrew)
brew tap oceanbase/seekdb
brew install seekdb
```

See [deployment docs](https://docs.seekdb.ai/seekdb/deploy-by-systemd/) for DEB/RPM offline install and configuration details.

</details>

<a id="more-examples"></a>

### 📝 More Examples

For the full Python SDK walkthrough — connection modes, embedding functions, metadata filters — see the [pyseekdb User Guide](https://github.com/oceanbase/pyseekdb).

<details open>
<summary><b>🤖 Agent Memory Pattern (continuous write + immediate retrieval)</b></summary>

The canonical agent loop: write an observation, retrieve relevant context
milliseconds later, repeat. seekdb's async index pipeline keeps both
sides fast under sustained concurrency.

```python
import pyseekdb

client = pyseekdb.Client(path="./agent_state.db")
memory = client.get_or_create_collection(name="episodic")

for step in agent.run():
    # Persist the observation
    memory.upsert(ids=[step.id], documents=[step.observation])

    # Retrieve relevant context — milliseconds after the write,
    # served by the incremental HNSW (no waiting on a background rebuild)
    relevant = memory.query(query_texts=step.next_query, n_results=5)

    agent.act(relevant)
```

</details>

<details>
<summary><b>🗄️ SQL — Schema + Hybrid Search</b></summary>

```sql
-- Table with vector column, full-text index, and HNSW vector index
CREATE TABLE articles (
  id        INT PRIMARY KEY,
  title     TEXT,
  content   TEXT,
  embedding VECTOR(384),
  FULLTEXT INDEX idx_fts (content) WITH PARSER ik,
  VECTOR   INDEX idx_vec (embedding) WITH (DISTANCE=l2, TYPE=hnsw, LIB=vsag)
) ORGANIZATION = HEAP;

-- Hybrid search: vector similarity + full-text match in one query
SELECT id, title,
       l2_distance(embedding, '[0.12, 0.34, ...]') AS dist
FROM articles
WHERE MATCH(content) AGAINST('quarterly report')
ORDER BY dist APPROXIMATE
LIMIT 10;
```

Python developers can access this via SQLAlchemy or any MySQL driver.

</details>


## 📚 Use Cases

<details open>
<summary><b>🎯 Agentic AI — Memory, Sandbox & State</b></summary>

Agents need a state store that handles continuous memory writes,
millisecond-later retrieval, branching for exploration, and rollback when
things go wrong. seekdb is built for exactly this:

- **Streaming-friendly storage** — write a memory, query it in the next ms
- **COW sandboxes** — `FORK DATABASE` for safe experimentation, `MERGE` to accept, `DROP` to roll back
- **Hybrid retrieval** — vector + full-text + relational in one SQL
- **MySQL protocol** — works with LangChain, LlamaIndex, Dify out of the box

Personal assistants · Enterprise automation · Vertical agents · Agent platforms

</details>

<details>
<summary><b>🧩 Other Use Cases</b></summary>

seekdb's hybrid retrieval + multi-model engine also fits classic AI workloads:

- **📖 RAG & Knowledge Retrieval** — vector + full-text + scalar filters with multi-level access control. *Enterprise QA, customer support, industry insights, personal knowledge bases.*
- **🔍 Semantic Search** — embedding-based search across text, images, and other modalities. *Product search, text-to-image, image-to-product.*
- **💻 AI-Assisted Coding** — semantic code search, multi-project isolation, time-travel queries for IDE plugins and code agents. *Local IDEs, web IDEs, design-to-web.*
- **⬆️ Enterprise Application Intelligence** — MySQL-compatible AI layer for legacy systems, with row/column hybrid storage. *Document intelligence, business insights, finance systems.*
- **📱 On-Device & Edge AI** — embedded / micro-server modes for resource-constrained devices. *In-vehicle systems, AI education, companion robots, healthcare devices.*

</details>

---

<a id="ecosystem--integrations"></a>

## 🌟 Ecosystem & Integrations

<div align="center">

<p>
    <a href="https://github.com/langchain-ai/langchain/pulls?q=is%3Apr+is%3Aclosed+oceanbase">
        <img src="https://img.shields.io/badge/LangChain-✅-00A67E?style=flat-square&logo=langchain" alt="LangChain" />
    </a>
    <a href="https://github.com/run-llama/llama_index/pulls?q=is%3Apr+is%3Aclosed+oceanbase">
        <img src="https://img.shields.io/badge/LlamaIndex-✅-00A67E?style=flat-square&logo=llama" alt="LlamaIndex" />
    </a>
    <a href="https://github.com/langgenius/dify/pulls?q=is%3Apr+is%3Aclosed+oceanbase">
        <img src="https://img.shields.io/badge/Dify-✅-00A67E?style=flat-square&logo=dify" alt="Dify" />
    </a>
    <a href="https://github.com/langchain-ai/langchain/pulls?q=is%3Apr+is%3Aclosed+oceanbase">
        <img src="https://img.shields.io/badge/LangGraph-✅-00A67E?style=flat-square&logo=langgraph" alt="LangGraph" />
    </a>
    <a href="https://github.com/coze-dev/coze-studio/pulls?q=is%3Apr+oceanbase+is%3Aclosed">
        <img src="https://img.shields.io/badge/Coze-✅-00A67E?style=flat-square&logo=coze" alt="Coze" />
    </a>
    <a href="https://huggingface.co">
        <img src="https://img.shields.io/badge/HuggingFace-✅-00A67E?style=flat-square&logo=huggingface" alt="HuggingFace" />
    </a>
</p>

<sub>+ Camel-AI · DB-GPT · FastGPT · Firecrawl · Spring-AI-Alibaba · Cloudflare Workers AI · Jina AI · Ragas · Instructor · Baseten — see [User Guide](https://docs.seekdb.ai/seekdb/seekdb-overview) for the full list.</sub>

</div>

---

## 🌐 Next Steps & Community

- 📖 **[Read the docs →](https://docs.seekdb.ai/)** — Quickstart, API reference, integration guides
- 📝 **[Launch blog →](docs/blog/launch_blog_en.md)** — The architecture behind 10.7× the QPS of Milvus
- 🐛 **[Open an issue →](https://github.com/oceanbase/seekdb/issues)** — Report bugs, request features
- 🤝 **[Contribute →](CONTRIBUTING.md)** — Help build the agent-era state store

<div align="center">

<p>
    <a href="https://discord.gg/74cF8vbNEs">
        <img src="https://img.shields.io/badge/Discord-Join%20Chat-5865F2?style=for-the-badge&logo=discord&logoColor=white" alt="Discord" />
    </a>
    <a href="https://github.com/oceanbase/seekdb/discussions">
        <img src="https://img.shields.io/badge/GitHub%20Discussion-181717?style=for-the-badge&logo=github&logoColor=white" alt="GitHub Discussion" />
    </a>
    <a href="https://ask.oceanbase.com/">
        <img src="https://img.shields.io/badge/Forum-Chinese%20Community-FF6900?style=for-the-badge" alt="Forum" />
    </a>
</p>

</div>

---

<a id="development"></a>

## 🛠️ Development

### Build from Source

Before building, please install the required toolchain and dependencies for your operating system. See [Install Toolchain](docs/developer-guide/en/toolchain.md) for detailed instructions.

```bash
# Clone the repository
git clone https://github.com/oceanbase/seekdb.git
cd seekdb
bash build.sh debug --init --make
mkdir -p ~/seekdb/bin
cp build_debug/src/observer/seekdb ~/seekdb/bin
cd ~/seekdb
./bin/seekdb
```

In this example, the working directory is $HOME/seekdb, please use a fresh directory for testing. Please see the [Developer Guide](docs/developer-guide/en/README.md) for detailed instructions.

### Contributing

We welcome contributions! See our [Contributing Guide](CONTRIBUTING.md) to get started.

<a href="https://github.com/oceanbase/seekdb/graphs/contributors">
  <img src="https://contrib.rocks/image?repo=oceanbase/seekdb&max=400" alt="Contributors" />
</a>

---

## 📈 Star History

<a href="https://star-history.com/#oceanbase/seekdb&Date">
  <img alt="Star History Chart" src="https://api.star-history.com/svg?repos=oceanbase/seekdb&type=Date" width="720" />
</a>

If seekdb is useful to you, **a star helps others find it.** ⭐

---

## 📄 License

seekdb is built by the [OceanBase](https://en.oceanbase.com/) team — the same database engine running in production at Alipay, Taobao, DiDi, Xiaomi, and more. Fully open-source under the [Apache License, Version 2.0](LICENSE).


