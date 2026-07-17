# What is OceanBase SeekDB?
OceanBase SeekDB is a brand-new AI Native database product developed by the OceanBase team. Built upon the core engine of the OceanBase database, it further meets the demands of AI application scenarios, supporting essential AI features such as comprehensive vector indexing, full-text indexing, hybrid search, and AI Functions. To continuously enhance the developer experience, SeekDB has undergone extensive resource and performance optimization, allowing it to run in environments with as little as 1 core CPU and 1GB of memory. It features minimal deployment and starts up in seconds, supporting both client/server and embedded deployment modes to significantly lower the barrier to use.

## Core Features
### 🚀 Minimal Deployment, Built for Developers and AI Agents
- Minimum resource requirement: Runs on just 1 core CPU and 1GB memory
- Rapid startup: Initializes in seconds with no complex configuration
- Single-node architecture: No external dependencies for deployment
- Dual deployment modes: Supports both client/server and embedded (Python-compatible) modes, allowing seamless switching

### 🐚 Rapidly Evolving AI Capabilities
- Vector Indexing: High-performance multi-dimensional data retrieval, supporting massive vector processing with flexible access interfaces (SQL/Python/Java)
- Full-Text Indexing: Intelligent text search with multi-language tokenization, relevance ranking, and fuzzy search
- Hybrid Search: Unified querying for multi-modal data, combining vector and scalar searches with intelligent ranking
- AI Functions: Built-in intelligent functions to simplify AI development
- Other rapidly iterating AI-related features

### 🧱 MySQL Compatibility with Powerful HTAP Capabilities
- Compatible with standard MySQL protocols, enabling AI + HTAP operations using SQL
- Supports mainstream MySQL ecosystem tools for seamless replacement of MySQL
- Powerful HTAP capabilities: Supports hybrid transactional and analytical processing

## Installation

```bash
pip install seekdb
```

## Usage

```python
import seekdb

# Open a database
seekdb.open()

# Connect to a database
conn = seekdb.connect()

# Use the connection
cursor = conn.cursor()
cursor.execute("SELECT * FROM oceanbase.DBA_OB_USERS")
results = cursor.fetchall()

# Close the connection
conn.close()

```

## Platform Support

- Linux x86_64
- Linux aarch64

## Requirements

- CPython 3.8+

## License

This package is licensed under Apache 2.0.
