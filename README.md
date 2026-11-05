<div align="center">

## [⚡ Performance at a Glance](https://github.com/oceanbase/seekdb/stargazers)

<div align="center">
  <img src="images/benchmark.svg" alt="seekdb benchmark: 10.7× the QPS of Milvus, 3.2× of Elasticsearch" width="720" />
</div>

> [📖 ](https://github.com/oceanbase/seekdb/stargazers)[Read the launch blog →](docs/blog/launch_blog_en.md) · 🔁 [Reproduce the benchmark →](https://github.com/oceanbase/vdb-streambench)

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

No manual server deployment, schema definition, or embedding setup for this Python demo. [Run a local database with Python, JavaScript, or Go →](#embedded-sdks)

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

<sub>Source:  · </sub>

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

<sub>Source: </sub>

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

<a id="embedded-sdks"></a>

### Embedded use

Use a local seekdb database from Python, Node.js, or Go without deploying a database service yourself. Data is stored in a local directory and remains available when you reopen it.

<details open>
<summary><b>🐍 Python</b></summary>

Use [pylibseekdb](https://pypi.org/project/pylibseekdb/) to start a local seekdb database, then execute SQL with PyMySQL. Requires Python 3.11 or newer.

```bash
python3 -m pip install -U pylibseekdb pymysql
```

Save the following as `example.py`. It gets the connection arguments from `connection_options()` and passes them to PyMySQL. The context managers close the cursor and SQL connection before releasing the runtime handle:

```python
from contextlib import closing

import pylibseekdb
import pymysql

with closing(pylibseekdb.open("./seekdb.db")) as db:
    options = db.connection_options()
    with pymysql.connect(
        database="test",
        autocommit=True,
        cursorclass=pymysql.cursors.DictCursor,
        **options,
    ) as connection:
        with connection.cursor() as cursor:
            cursor.execute("SELECT 1 AS value")
            print(cursor.fetchall())
```

Run it; the result is `[{'value': 1}]`:

```bash
python3 example.py
```

[Full usage guide](https://github.com/oceanbase/seekdb-bindings/blob/main/python/README.md) · [Supported platforms and requirements](https://pypi.org/project/pylibseekdb/#requirements)

For a higher-level Collection API with text embedding and hybrid search, see [pyseekdb](https://github.com/oceanbase/pyseekdb).

</details>

<details open>
<summary><b>🟨 JavaScript / TypeScript (Node.js)</b></summary>

Use [@oceanbase/seekdb-node](https://www.npmjs.com/package/@oceanbase/seekdb-node) to start a local seekdb database, then execute SQL with `mysql2`. Requires Node.js 22 or newer and supports both JavaScript and TypeScript.

```bash
npm install @oceanbase/seekdb-node mysql2
```

Keep npm optional dependencies enabled so npm installs the matching native runtime. The binding manages a separate local seekdb process; close SQL connections before releasing its runtime handle.

Save the following as `example.mjs` (ES modules; TypeScript declarations are also included):

```javascript
import { seekdb } from "@oceanbase/seekdb-node";
import mysql from "mysql2/promise";

const db = await seekdb.open("./seekdb.db");
let connection;
try {
  const endpoint = db.connectionOptions();
  const location = endpoint.transport === "tcp"
    ? { host: endpoint.host, port: endpoint.port }
    : { socketPath: endpoint.transport === "unix_socket"
        ? endpoint.unixSocket : endpoint.namedPipe };
  connection = await mysql.createConnection({
    ...location,
    user: endpoint.user,
    password: "",
    database: "test",
  });
  const [rows] = await connection.query("SELECT 1 AS value");
  console.log(rows);
} finally {
  try {
    if (connection) await connection.end();
  } finally {
    db.close();
  }
}
```

Run it; the result is `[ { value: 1 } ]`:

```bash
node example.mjs
```

[Full usage guide and supported platforms](https://www.npmjs.com/package/@oceanbase/seekdb-node)

For a higher-level JavaScript Collection API, see [seekdb-js](https://github.com/oceanbase/seekdb-js).

</details>

<details open>
<summary><b>🐹 Go</b></summary>

Use [seekdb-go](https://github.com/oceanbase/seekdb-go) to start a local seekdb database, then execute SQL with `go-sql-driver/mysql`. This example requires Go 1.24 or newer, `CGO_ENABLED=1`, and a C compiler.

```bash
mkdir seekdb-go-example
cd seekdb-go-example
go mod init example.com/seekdb-demo
```

Save the following as `main.go`. It uses the endpoint returned by `ConnectionOptions` and closes the SQL connections before releasing the runtime handle:

```go
package main

import (
	"database/sql"
	"fmt"
	"log"
	"net"

	"github.com/go-sql-driver/mysql"
	seekdb "github.com/oceanbase/seekdb-go"
)

func main() {
	if err := run(); err != nil {
		log.Fatal(err)
	}
}

func run() error {
	db, err := seekdb.Open("./seekdb.db")
	if err != nil {
		return err
	}
	defer db.Close()

	endpoint, err := db.ConnectionOptions()
	if err != nil {
		return err
	}
	cfg := mysql.NewConfig()
	cfg.User, cfg.DBName = endpoint.User, "test"
	switch endpoint.Transport {
	case "unix_socket":
		cfg.Net, cfg.Addr = "unix", endpoint.UnixSocket
	case "tcp":
		cfg.Net, cfg.Addr = "tcp", net.JoinHostPort(endpoint.Host, fmt.Sprint(endpoint.Port))
	default:
		return fmt.Errorf("unsupported transport %q", endpoint.Transport)
	}
	connection, err := sql.Open("mysql", cfg.FormatDSN())
	if err != nil {
		return err
	}
	defer connection.Close()

	var value int
	if err := connection.QueryRow("SELECT 1 AS value").Scan(&value); err != nil {
		return err
	}
	fmt.Println(value)
	return nil
}
```

Run it; Go resolves the dependencies and downloads the native bundle for your current platform. The result is `1`:

```bash
go run -mod=mod .
```

A regular `go get` for seekdb-go or `go mod tidy` may download bundles for all supported platforms.

[Full usage guide](https://github.com/oceanbase/seekdb-go#install) · [Supported platforms and requirements](https://github.com/oceanbase/seekdb-go/blob/main/docs/support.md)

</details>

These examples store data in `./seekdb.db` and can be rerun. The path is a database directory. Initializing a new directory can take longer than subsequent opens.

### Server deployments

<details open>
<summary><b>☁️ Cloud (Zero Install)</b></summary>

One curl, a running database — no signup, no credit card.

```bash
curl -X POST https://d0.seekdb.ai/api/v1/instances
```

Free for 7 days. [Learn more →](https://d0.seekdb.ai)

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

Start with the runnable [Python, JavaScript, and Go examples](#embedded-sdks) above. The following application pattern assumes an existing agent implementation.

<details>
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

---

## 🌐 Next Steps & Community

- 📖 **[Read the docs →](https://docs.seekdb.ai/)** — Quickstart, API reference, integration guides
- 📝 **[Launch blog →](docs/blog/launch_blog_en.md)** — The architecture behind 10.7× the QPS of Milvus
- 🐛 **[Open an issue →](https://github.com/oceanbase/seekdb/issues)** — Report bugs, request features
- 🤝 **[Contribute →](CONTRIBUTING.md)** — Help build the agent-era state store

<div align="center">

---

<a id="development"></a>

## 🛠️ Development

### Build from Source

Before building, please install the required toolchain and dependencies for your operating system. See [Install Toolchain](docs/developer-guide/en/toolchain.md) for detailed instructions.

```bash
# Clone the repository
git clone https://github.com/oceanbase/seekdb.git
cd seekdb
./build.py init
./build.py build seekdb
mkdir -p ~/seekdb/bin
cp build/bin/src/observer/seekdb ~/seekdb/bin
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


OB-P:ozynKWfu_yXWKAa4tRyElg
