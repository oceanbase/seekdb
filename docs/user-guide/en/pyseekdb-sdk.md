# Background
OceanBase Lightweight Edition (SeekDB) aims to provide developers with simple and easy-to-use APIs, reducing the learning curve and entry barriers. Compared to relational databases, database systems like MongoDB, ES, and Milvus simplify their main operations into KV operations in their Client APIs, making them more beginner-friendly. This SDK provides efficient and easy-to-use APIs for applications to access SeekDB and OceanBase's AI-related features. Advanced users can still use MySQL-compatible drivers to directly manipulate database objects in SeekDB and OceanBase through SQL statements.

To achieve the above design goals, this SDK follows the following design principles:

1. Fixed data model with schema-free interfaces. For beginners and application prototype development, users do not need to explicitly define relational table structures
2. The main data managed is text (or text fragments, or images) and their attributes
3. All data operations are single Collection operations, cross-collection operations are not supported
4. Each record stores and manages **one** text/image

# Python Client API

## Object Hierarchy
In this SDK, the database object concepts are shown in the following diagram.

![sdk_conception_logic](assets/sdk_conception_logic.jpg)

## Client Object
### Embedded Mode
```python
import pyseekdb

client = pyseekdb.Client(path="/path/to/save/to", database="db1")
```

### Server Mode
```python
import pyseekdb

client = pyseekdb.Client(host='localhost', port=2882, database="db1", user="u01", password="pass")
```

### OceanBase Mode
To connect to a local deployment of OceanBase, you need to pass the tenant name. If connecting to cloud services in OB Cloud, you don't need to specify the tenant name.

```python
import pyseekdb

client = pyseekdb.Client(host='localhost', port=2882, tenant="tenant1", database="db1", user="u01", passwd="pass")
```

## Collection Object
### Data Model
A Collection stores a set of "records". In SeekDB and OceanBase, each Collection corresponds to the following table structure.

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

The table name for a collection in SeekDB and OceanBase uses the format `c$v1$<name>`. Here, v1 represents the version of the data model, reserved for future compatibility handling.

Future data models can be modified as follows, with different predefined structures controlled through options in create_collection:

1. If using sparse vectors instead of BM25-based full-text search, a sparsevector column needs to be added.
2. If the document content itself is stored outside the database, the document field in the table should be changed to document_file, actually storing the external file address.

### Collection Management
#### 1.create
```python
collection = client.create_collection(name="my_collection")
```

Creates a new collection. Raises an error if it already exists.

#### 2.get
```python
collection = client.get_collection(name="my_collection")
```

Gets an existing collection. Raises an error if it doesn't exist.

```python
collection = client.get_or_create_collection(name="my_collection")
```

Gets an existing collection, or creates a new one if it doesn't exist.

#### 3.delete
```python
collection = client.delete_collection(name="my_collection")
```

### Data Modification Operations
#### 1.add
```python
collection.add(
    ids=["item1", "item2", ...],
    documents=["lorem ipsum...", "doc2", "doc3", ...],
    metadatas=[{"chapter": 3, "verse": 16}, {"chapter": 3, "verse": 5}, {"chapter": 29, "verse": 11}, ...],
)
```

Adds documents to the collection. Each document can have associated metadata. The server will automatically assign a unique id to each document and automatically generate embeddings for each document. The above interface is internally implemented in the SDK as executing the following SQL statement:

```sql
insert into c$v1$coll1 (_id, content, metadata)
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

You can also specify the id for each document when adding documents.

#### 2.update
```python
collection.update(
    ids=["1", "2", "3", ...],
    embeddings=[[1.1, 2.3, 3.2], [4.5, 6.9, 4.4], [1.1, 2.3, 3.2], ...],
    metadatas=[{"chapter": 3, "verse": 16}, {"chapter": 3, "verse": 5}, {"chapter": 29, "verse": 11}, ...],
    documents=["doc1", "doc2", "doc3", ...],
)
```

This operation specifies an id and updates the corresponding record's data. If any specified id does not exist, an error is raised.

```python
collection.upsert(
    ids=["1", "2", "3", ...],
    embeddings=[[1.1, 2.3, 3.2], [4.5, 6.9, 4.4], [1.1, 2.3, 3.2], ...],
    metadatas=[{"chapter": 3, "verse": 16}, {"chapter": 3, "verse": 5}, {"chapter": 29, "verse": 11}, ...],
    documents=["doc1", "doc2", "doc3", ...],
)
```

If a record with the specified id exists, it performs an `update` operation; otherwise, it performs an `add` operation.

#### 3.delete
```python
collection.delete(
    ids=["1", "2", "3",...],
)
```

Deletes records based on the specified ids.

```python
collection.delete(
    ids=["1", "2", "3",...],
    where={"chapter": "20"}
)
```

Deletes records with specified ids whose metadata satisfies the where condition. ids can be empty.

### Query Operations
#### Vector Similarity Query
```python
collection.query(
    query_texts=["thus spake zarathustra", "the oracle speaks"],
    n_results=5
)
```

First performs vector embedding on the query to generate vectors, then returns the 5 most similar records based on vector similarity. When `n_results` is not specified, it defaults to 10.

```python
collection.query(
    query_embeddings=[[11.1, 12.1, 13.1],[1.1, 2.3, 3.2], ...],
    n_results=5,
)
```

Queries records in the collection directly based on the specified vector embeddings, returning the 5 most similar records. When ids are specified, it is limited to records in those sets.

#### Query by ID
```python
collection.get(ids=["1", "2", ...], limit=5, offset=10)
```

Returns records based on the specified ids. If limit is not specified, it defaults to 100. If offset is not specified, it defaults to 0. If ids is empty, it returns data from the entire collection.

#### Filter Conditions
Both query and get interfaces can use where and where_document for filtering.

```python
collection.query(
    query_embeddings=[[11.1, 12.1, 13.1],[1.1, 2.3, 3.2], ...],
    n_results=5,
    where={"page": 10}, # query records with metadata field 'page' equal to 10
    where_document={"$contains": "search string"} # query records with the search string in the records' document
)
```

Uses `where` to filter properties in metadata, returning records that satisfy the filter conditions. Uses `where_document` to filter document content, returning records that satisfy the filter conditions.

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

Uses `where` to filter metadata, supporting the following expressions:

+ Comparison operators: $eq, $lt, $gt, $lte, $gte, $ne, $in, $nin
+ Logical operators: $or, $and, $not

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

Uses `where_document` to filter document content, supporting the following expressions:

+ $contains, uses full-text index search
+ $regex, uses regular expression matching
+ Logical operators: $or, $and

#### Specify Return Fields
Query and get results always include record ids. In addition, document and metadata are returned by default. You can use the include parameter to specify return fields.

```python
collection.query(query_texts=["my query"]) # 'ids', 'documents', and 'metadatas' are returned

collection.get(include=["documents"]) # Only 'ids' and 'documents' are returned

collection.query(
    query_texts=["my query"],
    include=["documents", "metadatas", "embeddings"]
) # 'ids', 'documents', 'metadatas', and 'embeddings' are returned
```

#### Hybrid Search
Hybrid search supports multi-way recall of vector search and full-text search results, and re-ranks the results.

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

Here, query, knn, and rank must be provided. In knn, you can use query_embeddings to query directly based on the provided vector similarity. Both query and knn can use where for filtering.

> In the future, where_document can add more matching methods similar to ElasticSearch's full-text queries.
>

#### Query Results
All the above query operations return a JSON object with the following structure:

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

## Database Object
A database is a collection of collections. The `AdminClient` class provides interfaces with obvious meanings:

```python
def AdminClient(settings: Settings = Settings()) -> AdminAPI
--
def create_database(name: str, tenant: str = DEFAULT_TENANT) -> None
def get_database(name: str, tenant: str = DEFAULT_TENANT) -> Database
def delete_database(name: str, tenant: str = DEFAULT_TENANT) -> None
def list_databases(limit: Optional[int] = None, offset: Optional[int] = None, tenant: str = DEFAULT_TENANT) -> Sequence[Database]
```

## Examples
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
