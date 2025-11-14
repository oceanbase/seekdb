<div align="center">

# <img src="images/logo.svg" alt="OceanBase Logo" width="40%" />

### **🔷 The AI-native hybrid search database**

**Powerful AI search capabilities · Lightweight · Production-ready**

</div>

---
<p align="center">
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

<div align="center">

**English** | [中文版](README_CN.md)

---

</div>

## 🚀 What is OceanBase SeekDB?

**OceanBase SeekDB** is the lightweight, embedded version of OceanBase Database - a powerful AI search database designed for the AI applications. It combines enterprise-grade database capabilities with cutting-edge **AI search ** features, such as Vector search, fulltext search, Json.

---

## 🔥 Why OceanBase SeekDB?

---
| Feature | OceanBase SeekDB | Traditional DB | Vector-only DB | Full-Text Engine |
|---------|----------------|----------------|----------------|------------------|
| **Embedded Mode** | ✅ Native | ⚠️ Possible | ⚠️ Possible | ⚠️ Possible |
| **SQL Support** | ✅ Full SQL | ✅ Full SQL | ❌ Limited | ❌ Limited |
| **Vector Search** | ✅ Built-in | ❌ Limited | ✅ Supported | ❌ Limited |
| **Full-Text Search** | ✅ Built-in | ✅ Supported | ❌ Limited | ✅ Advanced |
| **Json** | ✅ Yes | ⚠️ Varies by Product | ❌ Not Supported | ❌ Not Supported |
| **ACID Transactions** | ✅ Full | ✅ Full | ❌ Limited | ❌ Limited |
| **Easy Migration** | ✅ MySQL Compatible | ✅ Standard | ❌ No | ❌ No |


---

## ✨ Key Features

### 🎯 **AI-Powered Search**
- **Vector Similarity Search**: Optimize vector query accuracy, performance, and cost for different scenarios using different algorithms
- **Hybrid Search**: Combine vector search, scalar search, and full-text retrieval for optimal results
- **Full-Text Search**: Built-in full-text indexing for keyword-based searches
- **Json**：Built-in Json schema and query, support Json index.

### 📦 **Embedded & Lightweight**
- **Zero Dependencies**: Run embedded in your application - no separate database server required
- **Tiny Footprint**: Minimal memory and disk usage, perfect for edge devices and containers
- **Single Binary**: Easy to deploy and distribute with your application
- **Local-First**: Work offline, sync when ready

### ⚡ **Simple & Developer-Friendly**
- **MySQL Compatible**: Use familiar SQL syntax - no learning curve
- **Instant Setup**: Get started in seconds, not minutes
- **Rich APIs**: Support for Python, Java, Go, and more
- **Comprehensive Docs**: Clear documentation with examples for every use case

### 🚀 **Production-Ready**
- **Stability**: More than 15 years of technical expertise and 4000+ enterprise implementations
- **ACID Compliance**: Full transaction support with strong consistency guarantees
- **Horizontal Scalability**: Scale from single node to distributed cluster seamlessly
- **Enterprise Security**: Built-in encryption, authentication, and access control

---

## 🎬 Quick Start

### Installation

Choose your platform:

<details>
<summary><b>🐍 Python (Recommended for AI/ML)</b></summary>

```bash
pip install pylibseekdb
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
import pyseekdb
client = pyseekdb.Client()

collection = client.get_or_create_collection(name="my_collection")

collection.upsert(
    ids=["1", "2", "3"],
    documents=[
        "It's rainy today",
        "It was cloudy yesterday",
        "The forecast for tomorrow is fine weather"
   ]
)

results = collection.query(
    query_texts=[" What's the weather like today"], # SeekDB will embed this for you
    n_results=2 # how many results to return
)

print(results)
```
Please refer to the [User Guide](docs/user-guide/en/pyseekdb-sdk.md) for more details.
</details>
<details>
<summary><b>🗄️ 🐍 Old Python SDK</b></summary>

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

```python
import pylibseekdb as seekdb

# Open a database
seekdb.open()

# Connect to a database
conn = seekdb.connect()

# Use the connection
cursor = conn.cursor()
cursor.execute("""-- Create table with vector column
CREATE TABLE articles (
    id INT PRIMARY KEY,
    title TEXT,
    content TEXT,
    embedding VECTOR(384)
);""")

cursor.execute("""-- Create vector index for fast similarity search
CREATE INDEX idx_vector ON articles USING VECTOR (embedding);""")

cursor.execute("""-- Insert documents with embeddings
-- Note: Embeddings should be pre-computed using your embedding model
INSERT INTO articles (id, title, content, embedding)
VALUES
    (1, 'AI and Machine Learning', 'Artificial intelligence is transforming...', '[0.1, 0.2, ...]'),
    (2, 'Database Systems', 'Modern databases provide high performance...', '[0.3, 0.4, ...]'),
    (3, 'Vector Search', 'Vector databases enable semantic search...', '[0.5, 0.6, ...]');""")

cursor.execute("""-- Example: Hybrid search combining vector and full-text
-- Replace '[query_embedding]' with your actual query embedding vector
SELECT
    title,
    content,
    embedding <-> '[query_embedding]' AS vector_distance,
    MATCH(content) AGAINST('your keywords' IN NATURAL LANGUAGE MODE) AS text_score
FROM articles
WHERE MATCH(content) AGAINST('your keywords' IN NATURAL LANGUAGE MODE)
ORDER BY vector_distance ASC, text_score DESC
LIMIT 10;""")

results = cursor.fetchall()

# Close the connection
conn.close()
```
</details>


## 📚 Use Cases

<div align="center">

| 🎯 **RAG Systems** | 🔍 **Semantic Search** | 💬 **Chatbots** | 🎬 **Recommendations** |
|:---:|:---:|:---:|:---:|
| Build retrieval-augmented generation pipelines with vector search | Power semantic search across documents, images, and multimedia | Create intelligent chatbots with memory and context | Build recommendation engines with hybrid search |

</div>

### 🎯 Real-World Examples

- **📚 Document Q&A**: Build ChatGPT-like document search with RAG
- **🖼️ Image Search**: Find similar images using vision embeddings
- **💼 E-commerce**: Semantic product search and recommendations
- **🔬 Scientific Research**: Search through research papers and datasets
- **📊 Business Intelligence**: Combine SQL analytics with AI search

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

OceanBase SeekDB is licensed under the [Apache License, Version 2.0](LICENSE).


