<div align="center">

# <img src="https://mdn.alipayobjects.com/huamei_ytl0i7/afts/img/A*6BO4Q6D78GQAAAAAQFAAAAgAejCYAQ/original" width="420">

### **🔷 The AI-Native Search Database**

**Unifies vector, text, structured and semi-structured data in a single engine, enabling hybrid search and in-database AI workflows.**

</div>

---
<div align="center">
<p>
    <a href="https://oceanbase.ai">
        <img alt="Documentation" height="20" src="https://img.shields.io/badge/OceanBase.ai-4285F4?style=for-the-badge&logo=read-the-docs&logoColor=white" />
    </a>
    <a href="https://www.linkedin.com/company/oceanbase" target="_blank">
        <img src="https://custom-icon-badges.demolab.com/badge/LinkedIn-0A66C2?logo=linkedin-white&logoColor=fff" alt="follow on LinkedIn">
    </a>
    <a href="https://www.youtube.com/@OceanBaseDB">
        <img alt="Static Badge" src="https://img.shields.io/badge/YouTube-red?logo=youtube">
    </a>
    <a href="https://discord.gg/74cF8vbNEs">
        <img alt="Discord" src="https://img.shields.io/discord/74cF8vbNEs?label=Discord&logo=discord&style=flat-square&color=5865F2" />
    </a>
    <a href="https://pepy.tech/projects/">
        <img alt="Downloads" src="https://static.pepy.tech/badge/seekdb" />
    </a>
    <a href="https://github.com/oceanbase/seekdb/blob/master/LICENSE">
        <img alt="License" src="https://img.shields.io/badge/License-Apache_2.0-blue.svg" />
    </a>
</p>
</div>

<div align="center">

**English** | [中文版](README_CN.md)

---

</div>

## 🚀 What is OceanBase seekdb?

**OceanBase seekdb** unifies vector, text, structured and semi-structured data in a single engine, enabling hybrid search and in-database AI workflows.

---

## 🔥 Why OceanBase seekdb?

---
| **Feature** | **OceanBase seekdb** | **OceanBase** | **MySQL 9.0** | **Chroma** | **Elasticsearch** | **DuckDB** | **Milvus** | **PostgreSQL**<br/>**+pgvector** |
| --- | --- | --- | --- | --- | --- | --- | --- | --- |
| **Embedded Database** | **✅ Supported** | ❌ Not Supported | ❌ Not Supported (removed in 8.0) | ✅ Supported | ❌ Not Supported | ✅ Supported | ✅ Supported | ❌ Not Supported |
| **Single-Node Database** | **✅ Supported** | ✅ Supported | ✅ Supported | ✅ Supported | ✅ Supported | ✅ Supported | ✅ Supported | ✅ Supported |
| **Distributed Database** | ❌ Not Supported | ✅ Supported | ❌ Not Supported | ❌ Not Supported | ✅ Supported | ❌ Not Supported | ✅ Supported | ❌ Not Supported |
| **MySQL Compatible** | **✅ Supported** | ✅ Supported | ✅ Supported | ❌ Not Supported | ❌ Not Supported | ✅ Supported | ❌ Not Supported | ❌ Not Supported |
| **Vector Search** | **✅ Supported** | ✅ Supported | ❌ Not Supported | ✅ Supported | ✅ Supported | ✅ Supported | ✅ Supported | ✅ Supported |
| **Full-Text Search** | **✅ Supported** | ✅ Supported | ✅ Supported | ✅ Supported | ✅ Supported | ✅ Supported | ⚠️ Limited | ✅ Supported |
| **Hybrid Search** | **✅ Supported** | ✅ Supported | ❌ Not Supported | ✅ Supported | ✅ Supported | ❌ Not Supported | ✅ Supported | ⚠️ Limited |
| **OLTP** | **✅ Supported** | **✅ Supported** | **✅ Supported** | ❌ Not Supported | ❌ Not Supported | ❌ Not Supported | ❌ Not Supported | **✅ Supported** |
| **OLAP** | **✅ Supported** | ✅ Supported | ❌ Not Supported | ❌ Not Supported | ⚠️ Limited | ✅ Supported | ❌ Not Supported | **✅ Supported** |
| **Open Source License** | Apache 2.0 | MulanPubL 2.0 | GPL 2.0 | Apache 2.0 | AGPLv3<br/>+SSPLv1<br/>+Elastic 2.0 | MIT | Apache 2.0 | PostgreSQL License |

---

## ✨ Key Features

### Hybrid Search
1. Supports multi-channel retrieval in a single SQL query, combining vector-based semantic search with keyword-based search for optimized recall.
2. Query reranking supports weighted scores, Reciprocal Rank Fusion (RRF), and LLM-based reranking for enhanced results.
3. Scalar filters are pushed down to storage for optimized performance, and multi-table joins allow related data retrieval.

### Vector & Full-Text Search
1. Supports dense vectors and sparse vectors, with multiple distance metrics including Manhattan, Euclidean, inner product, and cosine similarity.
2. Vector indexes support in-memory types such as HNSW, HNSW-SQ, HNSW-BQ, and disk-based types including IVF and IVF-PQ, optimizing storage costs.
3. Full-text search supports keyword, phrase, and Boolean queries, with BM25 ranking for relevance.

### AI Functions
1. Manage built-in AI services via the DBMS_AI_SERVICE package in SQL, and register external LLM services.
2. Convert text to vector embeddings directly in SQL using the AI_EMBED function.
3. Generate text in SQL with AI_COMPLETE, supporting reusable prompt templates.
4. Rerank text using LLM-based models in SQL via AI_RERANK.



---

## 🎬 Quick Start

### Installation

Choose your platform:

<details>
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
  -v ./data:/var/lib/oceanbase/store \
  oceanbase/seekdb:latest
```

</details>

<details>
<summary><b>📦 Binary (Standalone)</b></summary>

```bash
# Linux
rpm -ivh seekdb-1.x.x.x-xxxxxxx.el8.x86_64.rpm
```
Please replace the version number with the actual RPM package version.

</details>

<details>
<summary><b>☁️ Cloud </b></summary>

Quickly experience OceanBase Cloud on AWS Marketplace - a highly scalable distributed database for transactional, analytical, and AI workloads. [Get started now](https://aws.amazon.com/marketplace/pp/prodview-d2evwth3ztaja?sr=0-1&ref_=beagle&applicationId=AWSMPContessa)

</details>

### 🎯 AI Search Example

Build a semantic search system in 5 minutes:

<details>
<summary><b>🗄️ 🐍 New Python SDK</b></summary>

```bash
# install sdk first
pip install -U pyseekdb
```

```python
"""
this example demonstrates the most common operations with embedding functions:
1. Create a client connection
2. Create a collection with embedding function
3. Add data using documents (embeddings auto-generated)
4. Query using query texts (embeddings auto-generated)
5. Print query results

This is a minimal example to get you started quickly with embedding functions.
"""

import pyseekdb
from pyseekdb import DefaultEmbeddingFunction

# ==================== Step 1: Create Client Connection ====================
# You can use embedded mode, server mode, or OceanBase mode
# For this example, we'll use server mode (you can change to embedded or OceanBase)

# Embedded mode (local SeekDB)
client = pyseekdb.Client(
    path="./seekdb.db",
    database="test"
)
# Alternative: Server mode (connecting to remote SeekDB server)
# client = pyseekdb.Client(
#     host="127.0.0.1",
#     port=2881,
#     database="test",
#     user="root",
#     password=""
# )

# Alternative: Remote server mode (OceanBase Server)
# client = pyseekdb.Client(
#     host="127.0.0.1",
#     port=2881,
#     tenant="test",  # OceanBase default tenant
#     database="test",
#     user="root",
#     password=""
# )

# ==================== Step 2: Create a Collection with Embedding Function ====================
# A collection is like a table that stores documents with vector embeddings
collection_name = "my_simple_collection"

# Create collection with default embedding function
# The embedding function will automatically convert documents to embeddings
collection = client.create_collection(
    name=collection_name,
    #embedding_function=DefaultEmbeddingFunction()  # Uses default model (384 dimensions)
)

print(f"Created collection '{collection_name}' with dimension: {collection.dimension}")
print(f"Embedding function: {collection.embedding_function}")

# ==================== Step 3: Add Data to Collection ====================
# With embedding function, you can add documents directly without providing embeddings
# The embedding function will automatically generate embeddings from documents

documents = [
    "Machine learning is a subset of artificial intelligence",
    "Python is a popular programming language",
    "Vector databases enable semantic search",
    "Neural networks are inspired by the human brain",
    "Natural language processing helps computers understand text"
]

ids = ["id1", "id2", "id3", "id4", "id5"]

# Add data with documents only - embeddings will be auto-generated by embedding function
collection.add(
    ids=ids,
    documents=documents,  # embeddings will be automatically generated
    metadatas=[
        {"category": "AI", "index": 0},
        {"category": "Programming", "index": 1},
        {"category": "Database", "index": 2},
        {"category": "AI", "index": 3},
        {"category": "NLP", "index": 4}
    ]
)

print(f"\nAdded {len(documents)} documents to collection")
print("Note: Embeddings were automatically generated from documents using the embedding function")

# ==================== Step 4: Query the Collection ====================
# With embedding function, you can query using text directly
# The embedding function will automatically convert query text to query vector

# Query using text - query vector will be auto-generated by embedding function
query_text = "artificial intelligence and machine learning"

results = collection.query(
    query_texts=query_text,  # Query text - will be embedded automatically
    n_results=3  # Return top 3 most similar documents
)

print(f"\nQuery: '{query_text}'")
print(f"Query results: {len(results['ids'][0])} items found")

# ==================== Step 5: Print Query Results ====================
for i in range(len(results['ids'][0])):
    print(f"\nResult {i+1}:")
    print(f"  ID: {results['ids'][0][i]}")
    print(f"  Distance: {results['distances'][0][i]:.4f}")
    if results.get('documents'):
        print(f"  Document: {results['documents'][0][i]}")
    if results.get('metadatas'):
        print(f"  Metadata: {results['metadatas'][0][i]}")

# ==================== Step 6: Cleanup ====================
# Delete the collection
client.delete_collection(collection_name)
print(f"\nDeleted collection '{collection_name}'")

```
Please refer to the [User Guide](https://github.com/oceanbase/pyseekdb) for more details.
</details>

<details>
<summary><b>🗄️ 🐍 Old Python SDK</b></summary>

```bash
# install old sdk first, not recommended
pip install -U pyobvector
```

```python
from pyobvector import *

# Don't support Embedded mode
client = MilvusLikeClient(uri="127.0.0.1:2881", user="test@test")

test_collection_name = "ann_test"
# define the schema of collection with optional partitions
range_part = ObRangePartition(False, range_part_infos = [
    RangeListPartInfo('p0', 100),
    RangeListPartInfo('p1', 'maxvalue'),
], range_expr='id')
schema = client.create_schema(partitions=range_part)
# define field schema of collection
schema.add_field(field_name="id", datatype=DataType.INT64, is_primary=True)
schema.add_field(field_name="embedding", datatype=DataType.FLOAT_VECTOR, dim=3)
schema.add_field(field_name="meta", datatype=DataType.JSON, nullable=True)
# define index parameters
idx_params = client.prepare_index_params()
idx_params.add_index(
    field_name='embedding',
    index_type=VecIndexType.HNSW,
    index_name='vidx',
    metric_type="L2",
    params={"M": 16, "efConstruction": 256},
)
# create collection
client.create_collection(
    collection_name=test_collection_name,
    schema=schema,
    index_params=idx_params,
)

# insert value
# prepare data
vector_value1 = [0.748479, 0.276979, 0.555195]
vector_value2 = [0, 0, 0]
data1 = [{'id': i, 'embedding': vector_value1} for i in range(10)]
data1.extend([{'id': i, 'embedding': vector_value2} for i in range(10, 13)])
data1.extend([{'id': i, 'embedding': vector_value2} for i in range(111, 113)])
# insert data
client.insert(collection_name=test_collection_name, data=data1)

# do search
res = client.search(collection_name=test_collection_name, data=[0, 0, 0], anns_field='embedding', limit=5, output_fields=['id'])
# For example, the result will be:
# [{'id': 112}, {'id': 111}, {'id': 10}, {'id': 11}, {'id': 12}]

```
Please refer to the [User Guide](https://github.com/oceanbase/pyobvector) for more details.
</details>

<details>
<summary><b>🗄️ SQL</b></summary>

```sql
-- Create table with vector column
CREATE TABLE articles (
    id INT PRIMARY KEY,
    title TEXT,
    content TEXT,
    embedding VECTOR(384)
);

-- Create vector index for fast similarity search
CREATE INDEX idx_vector ON articles USING VECTOR (embedding);

-- Insert documents with embeddings
-- Note: Embeddings should be pre-computed using your embedding model
INSERT INTO articles (id, title, content, embedding)
VALUES
    (1, 'AI and Machine Learning', 'Artificial intelligence is transforming...', '[0.1, 0.2, ...]'),
    (2, 'Database Systems', 'Modern databases provide high performance...', '[0.3, 0.4, ...]'),
    (3, 'Vector Search', 'Vector databases enable semantic search...', '[0.5, 0.6, ...]');

-- Example: Hybrid search combining vector and full-text
-- Replace '[query_embedding]' with your actual query embedding vector
SELECT
    title,
    content,
    embedding <-> '[query_embedding]' AS vector_distance,
    MATCH(content) AGAINST('your keywords' IN NATURAL LANGUAGE MODE) AS text_score
FROM articles
WHERE MATCH(content) AGAINST('your keywords' IN NATURAL LANGUAGE MODE)
ORDER BY vector_distance ASC, text_score DESC
LIMIT 10;

```
We suggest developers use sqlalchemy to access data by SQL for python developers.
</details>


## 📚 Use Cases

### 📖 RAG & Knowledge Retrieval
Large language models are limited by their training data. RAG introduces timely and trusted external knowledge to improve answer quality and reduce hallucination. seekdb enhances search accuracy through vector search, full-text search, hybrid search, built-in AI functions, and efficient indexing, while multi-level access control safeguards data privacy across heterogeneous knowledge sources.
1. Enterprise QA
2. Customer support
3. Industry insights
4. Personal knowledge

### 🔍 Semantic Search Engine
Traditional keyword search struggles to capture intent. Semantic search leverages embeddings and vector search to understand meaning and connect text, images, and other modalities. seekdb's hybrid search and multi-model querying deliver more precise, context-aware results across complex search scenarios.
1. Product search
2. Text-to-image
3. Image-to-product

### 🎯 Agentic AI Applications
Agentic AI requires memory, planning, perception, and reasoning. seekdb provides a unified foundation for agents through metadata management, vector/text/mixed queries, multimodal data processing, RAG, built-in AI functions and inference, and robust privacy controls—enabling scalable, production-grade agent systems.
1. Personal assistants
2. Enterprise automation
3. Vertical agents
4. Agent platforms

### 💻 AI-Assisted Coding & Development
AI-powered coding combines natural-language understanding and code semantic analysis to enable generation, completion, debugging, testing, and refactoring. seekdb enhances code intelligence with semantic search, multi-model storage for code and documents, isolated multi-project management, and time-travel queries—supporting both local and cloud IDE environments.
1. IDE plugins
2. Design-to-web
3. Local IDEs
4. Web IDEs

### ⬆️ Enterprise Application Intelligence
AI transforms enterprise systems from passive tools into proactive collaborators. seekdb provides a unified AI-ready storage layer, fully compatible with MySQL syntax and views, and accelerates mixed workloads with parallel execution and hybrid row-column storage. Legacy applications gain intelligent capabilities with minimal migration across office, workflow, and business analytics scenarios.
1. Document intelligence
2. Business insights
3. Finance systems

### On-Device & Edge AI Applications
Edge devices—from mobile to vehicle and industrial terminals—operate with constrained compute and storage. seekdb's lightweight architecture supports embedded and micro-server modes, delivering full SQL, JSON, and hybrid search under low resource usage. It integrates seamlessly with OceanBase cloud services to enable unified edge-to-cloud intelligent systems.
1. Personal assistants
2. In-vehicle systems
3. AI education
4. Companion robots
5. Healthcare devices

---

## 🌟 Ecosystem & Integrations

<div align="center">

<p>
    <a href="https://huggingface.co">
        <img src="https://img.shields.io/badge/HuggingFace-✅-00A67E?style=flat-square&logo=huggingface" alt="HuggingFace" />
    </a>
    <a href="https://github.com/langchain-ai/langchain/pulls?q=is%3Apr+is%3Aclosed+oceanbase">
        <img src="https://img.shields.io/badge/LangChain-✅-00A67E?style=flat-square&logo=langchain" alt="LangChain" />
    </a>
    <a href="https://github.com/langchain-ai/langchain/pulls?q=is%3Apr+is%3Aclosed+oceanbase">
        <img src="https://img.shields.io/badge/LangGraph-✅-00A67E?style=flat-square&logo=langgrap" alt="LangGraph" />
    </a>
    <a href="https://github.com/langgenius/dify/pulls?q=is%3Apr+is%3Aclosed+oceanbase">
        <img src="https://img.shields.io/badge/Dify-✅-00A67E?style=flat-square&logo=dify" alt="Dify" />
    </a>
    <a href="https://github.com/coze-dev/coze-studio/pulls?q=is%3Apr+oceanbase+is%3Aclosed">
        <img src="https://img.shields.io/badge/Coze-✅-00A67E?style=flat-square&logo=coze" alt="Coze" />
    </a>
    <a href="https://github.com/run-llama/llama_index/pulls?q=is%3Apr+is%3Aclosed+oceanbase">
        <img src="https://img.shields.io/badge/LlamaIndex-✅-00A67E?style=flat-square&logo=llama" alt="LlamaIndex" />
    </a>
    <a href="https://firecrawl.dev">
        <img src="https://img.shields.io/badge/Firecrawl-✅-00A67E?style=flat-square&logo=firecrawl" alt="Firecrawl" />
    </a>
    <a href="https://github.com/labring/FastGPT/pulls?q=is%3Apr+oceanbase+is%3Aclosed">
        <img src="https://img.shields.io/badge/FastGPT-✅-00A67E?style=flat-square&logo=FastGPT" alt="FastGPT" />
    </a>
    <a href="https://db-gpt.io">
        <img src="https://img.shields.io/badge/DB--GPT-✅-00A67E?style=flat-square&logo=db-gpt" alt="DB-GPT" />
    </a>
    <a href="https://github.com/camel-ai/camel/pulls?q=is%3Apr+oceanbase+is%3Aclosed">
        <img src="https://img.shields.io/badge/camel-✅-00A67E?style=flat-square&logo=camel" alt="Camel-AI" />
    </a>
    <a href="https://github.com/alibaba/spring-ai-alibaba">
        <img src="https://img.shields.io/badge/spring--ai--alibaba-✅-00A67E?style=flat-square&logo=spring" alt="spring-ai-alibaba" />
    </a>
    <a href="https://developers.cloudflare.com/workers-ai">
        <img src="https://img.shields.io/badge/Cloudflare%20Workers%20AI-✅-00A67E?style=flat-square&logo=cloudflare" alt="Cloudflare Workers AI" />
    </a>
    <a href="https://jina.ai">
        <img src="https://img.shields.io/badge/Jina%20AI-✅-00A67E?style=flat-square&logo=jina" alt="Jina AI" />
    </a>
    <a href="https://ragas.io">
        <img src="https://img.shields.io/badge/Ragas-✅-00A67E?style=flat-square&logo=ragas" alt="Ragas" />
    </a>
    <a href="https://jxnl.github.io/instructor">
        <img src="https://img.shields.io/badge/Instructor-✅-00A67E?style=flat-square&logo=instructor" alt="Instructor" />
    </a>
    <a href="https://baseten.co">
        <img src="https://img.shields.io/badge/Baseten-✅-00A67E?style=flat-square&logo=baseten" alt="Baseten" />
    </a>
</p>

<p>
Please refer to the [User Guide](docs/user-guide/README.md) for more details.
</p>


</div>

---


## 🤝 Community & Support

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

## 🛠️ Development

### Build from Source

```bash
# Clone the repository
git clone https://github.com/oceanbase/seekdb.git
cd seekdb
bash build.sh debug --init --make
./debug/observer
```

See the [Developer Guide](docs/developer-guide/en/README.md) for detailed instructions.

### Contributing

We welcome contributions! See our [Contributing Guide](CONTRIBUTING.md) to get started.

---


## 📄 License

OceanBase seekdb is licensed under the [Apache License, Version 2.0](LICENSE).


