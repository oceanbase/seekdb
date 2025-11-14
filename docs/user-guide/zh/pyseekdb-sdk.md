# 背景
OceanBase 轻量版（SeekDB）要给开发者提供简单易用的 API，减少入门和学习负担。相对于关系数据库，MongoDB、ES、Milvus等数据库系统的 Client API 把主要操作都简化为 KV，对初学用户更友好。本套 SDK 为应用程序访问 SeekDB 和 OceanBase 的 AI 相关功能提供高效和易用的 API。而对于高级用户，仍然可以使用 MySQL 兼容驱动，通过 SQL 语句直接操作 SeekDB 和 OceanBase 中的数据库对象。

为了达到上述设计目标，本 SDK遵循以下设计原则：

1. 固定数据模型，接口为 schemafree，初学用户和应用原型开发，不需要用户显式定义关系表结构
2. 主要管理的数据是文本（或文本片段、或图片）及其属性
3. 数据操作都是单 Collection 操作，不支持跨多个Collection的操作
4. 每条记录保存和管理**一个**文本/图片

# Python客户端 API

## 对象层次
本 SDK 中，呈现的数据库对象概念如下图。

![sdk_conception_logic](assets/sdk_conception_logic.jpg)

## Client 对象
### 嵌入式模式
```python
import pyseekdb

client = pyseekdb.Client(path="/path/to/save/to", database="db1")
```

### 服务器模式
```python
import pyseekdb

client = pyseekdb.Client(host='localhost', port=2882, database="db1", user="u01“, password="pass")
```

### OceanBase 模式
连接 OceanBase 的本地部署，需要传递租户名。如果是连接 OB Cloud中的云服务，无需指定租户名。

```python
import pyseekdb

client = pyseekdb.Client(host='localhost', port=2882, tenant="tenant1", database="db1", user="u01“, passwd="pass")
```

## Collection对象
### 数据模型
Collection 中保存一组“记录”，在 SeekDB 和 OceanBase 中，每一个 Collection 对应如下一个表结构。

```sql
create table c$v1$coll1
(
  _id varbinary(512) PRIMARY KEY NOT NULL,
  document string,
  embedding vector(?),
  metadata json,
  FULLTEXT INDEX idx_fts(document),
  VECTOR INDEX idx_vec (embedding)
);
```

collection 在 SeekDB 和 OceanBase 中的表名采用 `c$v1$<名称>`。其中v1表示数据模型的版本，为未来兼容性处理预留。

未来数据模型可以做如下变形，不同的预定义结构，通过 create_collection 的选项可控制：

1. 如果不使用基于 BM25 的全文搜索，而使用稀疏向量，需要添加一列稀疏向量 sparsevector 字段。
2. 如果文档内容本身存储在数据库之外，表中 document 字段改为 document_file, 实际存储外部文件地址

### Collection管理
#### 1.create
```python
collection = client.create_collection(name="my_collection")
```

新建，如果已经存在则报错。

#### 2.get
```python
collection = client.get_collection(name="my_collection")
```

获取已经存在的collection，如果不存在，则报错。

```python
collection = client.get_or_create_collection(name="my_collection")
```

获取已经存在的collection，如果不存在，则新建。

#### 3.delete
```python
collection = client.delete_collection(name="my_collection")
```

### 数据修改操作
#### 1.add
```python
collection.add(
    ids=["item1", "item2", ...],
    documents=["lorem ipsum...", "doc2", "doc3", ...],
    metadatas=[{"chapter": 3, "verse": 16}, {"chapter": 3, "verse": 5}, {"chapter": 29, "verse": 11}, ...],
)

```

给 collection 中新增文档，每篇文档可以设定相关的metadata。服务端会给每篇文档自动分配一个唯一 id，并自动生成每篇文档的embedding。上述接口，在 SDK 内部实现为执行以下 SQL 语句：

```sql
insert into c$coll1 (content, metadata)
values (('item1', 'lorem ipsum...', '{"chapter": 3, "verse": 16}'),
        ('item2', 'doc2', '{"chapter": 3, "verse": 5}'),
        ('item3', 'doc3', '{"chapter": 29, "verse": 11}'));
```

```python
collection.add(
    ids=["1", "2", "3", ...],
    documents=["lorem ipsum...", "doc2", "doc3", ...],
    metadatas=[{"chapter": 3, "verse": 16}, {"chapter": 3, "verse": 5}, {"chapter": 29, "verse": 11}, ...],
)

```

也可以在新增文档时，指定每篇文档的 id。

#### 2.update
```python
collection.update(
    ids=["1", "2", "3", ...],
    embeddings=[[1.1, 2.3, 3.2], [4.5, 6.9, 4.4], [1.1, 2.3, 3.2], ...],
    metadatas=[{"chapter": 3, "verse": 16}, {"chapter": 3, "verse": 5}, {"chapter": 29, "verse": 11}, ...],
    documents=["doc1", "doc2", "doc3", ...],
)
```

本操作指定 id，更新对应记录的数据。如果指定的任一 id 不存在，则报错。

```python
collection.upsert(
    ids=["1", "2", "3", ...],
    embeddings=[[1.1, 2.3, 3.2], [4.5, 6.9, 4.4], [1.1, 2.3, 3.2], ...],
    metadatas=[{"chapter": 3, "verse": 16}, {"chapter": 3, "verse": 5}, {"chapter": 29, "verse": 11}, ...],
    documents=["doc1", "doc2", "doc3", ...],
)
```

如果指定 id 的记录存在，则执行 `update`操作，否则执行 `add`操作。

#### 3.delete
```python
collection.delete(
    ids=["1", "2", "3",...],
)
```

根据指定 id 删除记录。

```python
collection.delete(
    ids=["1", "2", "3",...],
	  where={"chapter": "20"}
)
```

删除指定 id 的记录中，metadata 满足 where 条件的记录。ids 可以为空。

### 查询操作
#### 按向量相似度查询
```python
collection.query(
    query_texts=["thus spake zarathustra", "the oracle speaks"],
    n_results=5
)
```

首先对查询执行向量嵌入产生向量，然后根据向量相似度，返回最相近的 5 条记录。当不指定 `n_results` 的值时，默认为 10。

```python
collection.query(
    query_embeddings=[[11.1, 12.1, 13.1],[1.1, 2.3, 3.2], ...],
    n_results=5,
)

```

根据指定向量嵌入，直接查询collection中的记录，返回最相近的 5 条记录。当指定 ids 时，限于这些集合中的记录。

#### 按 ID 查询
```python
collection.get(ids=["1", "2", ...], limit=5, offset=10)
```

根据指定 id 返回记录。如果不指定 limit，默认为 100。如果不指定 offset，默认为 0。如果 ids 为空，则从整个 collection 中返回数据。

#### 过滤条件
query 和 get 接口中，都可以使用 where 和 where_document 进行过滤。

```python
collection.query(
    query_embeddings=[[11.1, 12.1, 13.1],[1.1, 2.3, 3.2], ...],
    n_results=5,
    where={"page": 10}, # query records with metadata field 'page' equal to 10
    where_document={"$contains": "search string"} # query records with the search string in the records' document
)
```

使用 `where `对 metadata 中的属性进行过滤，返回满足过滤条件的记录。使用 `where_document` 对文档内容进行过滤，返回满足过滤条件的记录。

```python
collection.query(
    query_texts=["first query", "second query"],
    where={
        "$and": [
            {"page": {"$gte": 5 }},
            {"page": {"$lte": 10 }},
        ]
    }
)
```

使用 `where` 对 metadata 进行过滤，支持以下表达式：

+ 比较运算符：$eq, $lt, $gt, $lte, $gte, $ne, $in, $nin
+ 逻辑运算符：$or, $and, $not

```python
collection.query(
    query_texts=["query1", "query2"],
    where_document={
        "$and": [
            {"$contains": "search_string_1"},
            {"$regex": "[a-z]+"},
        ]
    }
)
```

使用 `where_document` 对文档内容进行过滤，支持以下表达式：

+ $contains，使用全文索引搜索
+ $regex，使用正则表达式匹配
+ 逻辑运算符：$or, $and

#### 指定返回字段
query 和 get 的返回结果中，总是包含记录 id。除此以外，默认会返回 document 和 metadata。可以使用include 参数指定返回字段。

```python
collection.query(query_texts=["my query"]) # 'ids', 'documents', and 'metadatas' are returned

collection.get(include=["documents"]) # Only 'ids' and 'documents' are returned

collection.query(
    query_texts=["my query"],
    include=["documents", "metadatas", "embeddings"]
) # 'ids', 'documents', 'metadatas', and 'embeddings' are returned
```

#### 混合搜索
混合搜索支持对向量搜索和全文搜索的结果进行多路召回，并对结果进行重排序。

```python
collection.hybrid_search(
    query={
        # full-text search query here
        where_document={"$contains": "search string"},
        where={...},
        n_results=10
    },
    knn={
        # vector search query here
        query_texts=["thus spake zarathustra", "the oracle speaks"],
        where={...},
        n_results=10
    },
    rank={
        "rrf": {}
    },
    n_results=5,
    include=["documents", "metadatas", "embeddings"]
)
```

其中，query、knn 和 rank 是必须提供的。knn 中，可以使用 query_embeddings 直接根据提供的向量相似度进行查询。query 和 knn 中，都可以使用 where 进行过滤。

> 未来，where_document下可以增加类似 ElasticSearch 的全文查询的更多匹配方式。
>

#### 查询结果
上述所有查询操作，返回如下结构的 JSON 对象：

```json
[{'_id': '1',
  'document': 'a document text'
  'embedding': [0.35803765, -0.6023496, 0.18414013, -0.26286206, 0.90294385],
  'metadata': {
    'chapter': 3,
    'verse': 16
     'color': 'pink_8682'
    },
  },
]
```

## Database 对象
database 是一组 collection 的集合。`AdminClient`类提供如下含义显而易见的接口：

```python
def AdminClient(settings: Settings = Settings()) -> AdminAPI
--
def create_database(name: str, tenant: str = DEFAULT_TENANT) -> None
def get_database(name: str, tenant: str = DEFAULT_TENANT) -> Database
def delete_database(name: str, tenant: str = DEFAULT_TENANT) -> None
def list_databases(limit: Optional[int] = None, offset: Optional[int] = None, tenant: str = DEFAULT_TENANT) -> Sequence[Database]
```

## 示例
```python
import pyseekdb
client = pyseekdb.Client()

collection = client.get_or_create_collection(name="my_collection")

collection.upsert(
    ids=["itemA", "itemB"],
    documents=[
        "This is a document about pineapple",
        "This is a document about oranges"
    ]
)

results = collection.query(
    query_texts=["This is a query document about florida"], # SeekDB will embed this for you
    n_results=2 # how many results to return
)

print(results)

```
