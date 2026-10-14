# Copyright (c) 2025 OceanBase.
# Licensed under the Apache License, Version 2.0 (the "License");
# you may not use this file except in compliance with the License.
# You may obtain a copy of the License at http://www.apache.org/licenses/LICENSE-2.0
# Unless required by applicable law or agreed to in writing, software distributed
# under the License is distributed on an "AS IS" BASIS, WITHOUT WARRANTIES OR
# CONDITIONS OF ANY KIND, either express or implied. See the License for the
# specific language governing permissions and limitations under the License.

"""LightRAG graph storage for SeekDB's GRAPH_TABLE feature branch.

Provision schema.sql once in a dedicated database. Set SEEKDB_HOST, SEEKDB_PORT,
SEEKDB_USER, SEEKDB_PASSWORD and SEEKDB_DATABASE; add this directory to PYTHONPATH.
Call register() before LightRAG(..., graph_storage="SeekDBGraphStorage").
Vector/KV/document-status storage is unchanged. Tested against LightRAG commit
453dce83d6d0354a06e46c8d4029a0895c4e054b; requires PyMySQL and Python >= 3.10.

Every database operation owns a connection/transaction. Blocking DB I/O runs in a worker,
not the event loop. A cancelled caller waits for that worker's terminal outcome;
no write continues after the call returns. Cancellation/transport loss can still
mean a committed write: mutations are idempotent and may safely be retried.
No cross-storage (graph/vector/document) atomicity is promised.
Keyset iterators use one snapshot per page; export_snapshot() reads nodes and
edges together in one transaction for offline community analysis. It materializes
the whole namespace in client memory; it is not a streaming or incremental export.
Labels are bounded to 2048 UTF-8 bytes, scopes to 512 bytes and graph-view
depth to 16. These limits fail explicitly; graph-view truncation is reported in
LightRAG's is_truncated field, not silently applied to ordinary graph retrieval.
"""

from __future__ import annotations

import asyncio
import json
import os
import time
from dataclasses import dataclass, field

import pymysql
from lightrag.base import BaseGraphStorage
from lightrag.types import KnowledgeGraph, KnowledgeGraphEdge, KnowledgeGraphNode
from lightrag.utils import validate_graph_attributes


BATCH_SIZE = 256
MAX_LABEL_BYTES = 2048
MAX_SCOPE_BYTES = 512
TRANSACTION_MAX_ATTEMPTS = 3
TRANSACTION_RETRY_DELAY = 0.01


def _key(value, capacity=MAX_LABEL_BYTES):
    if not isinstance(value, str):
        raise TypeError("graph identifiers must be strings")
    encoded = value.encode("utf-8")
    if len(encoded) > capacity:
        raise ValueError(f"graph identifier exceeds {capacity} UTF-8 bytes")
    return encoded


def _label(value):
    return value.decode("utf-8") if isinstance(value, bytes) else value


def _json(value):
    return json.loads(value) if isinstance(value, (str, bytes)) else value


def _chunks(values, size=BATCH_SIZE):
    for start in range(0, len(values), size):
        yield values[start:start + size]


def _marks(values):
    return ",".join(["%s"] * len(values))


def _key_marks(values):
    # Keep empty byte strings typed as binary too: some MySQL clients encode
    # bytes as hex literals, whose uncast empty form can compare differently.
    return ",".join(["CAST(%s AS BINARY)"] * len(values))


@dataclass
class SeekDBGraphStorage(BaseGraphStorage):
    _options: dict = field(default_factory=dict, init=False, repr=False)
    _scope: int | None = field(default=None, init=False, repr=False)

    def __post_init__(self):
        self._options = dict(
            host=os.environ.get("SEEKDB_HOST", "127.0.0.1"),
            port=int(os.environ.get("SEEKDB_PORT", "2881")),
            user=os.environ["SEEKDB_USER"],
            password=os.environ.get("SEEKDB_PASSWORD", ""),
            database=os.environ["SEEKDB_DATABASE"], charset="utf8mb4",
            autocommit=False, cursorclass=pymysql.cursors.DictCursor,
            connect_timeout=10, read_timeout=60, write_timeout=60,
        )
        _key(self.workspace, MAX_SCOPE_BYTES)
        _key(self.namespace, MAX_SCOPE_BYTES)

    def _transaction(self, operation, write, scoped):
        for attempt in range(TRANSACTION_MAX_ATTEMPTS):
            connection = pymysql.connect(**self._options)
            try:
                with connection.cursor() as cursor:
                    cursor.execute("SET SESSION TRANSACTION ISOLATION LEVEL REPEATABLE READ")
                    cursor.execute("SET ob_query_timeout=30000000")
                    connection.begin()
                    if scoped:
                        if self._scope is None:
                            raise RuntimeError("initialize graph storage before use")
                        if write:
                            # All writers, including other processes, lock before
                            # touching nodes/edges so deletion cannot create orphans.
                            cursor.execute("SELECT id FROM lr_seekdb_scopes WHERE id=%s FOR UPDATE",
                                           (self._scope,))
                            if cursor.fetchone() is None:
                                raise RuntimeError("graph namespace was removed externally")
                    result = operation(cursor)
                connection.commit()
                return result
            except pymysql.OperationalError as error:
                connection.rollback()
                # RR may reject a contended namespace lock or a later write.
                # Only this explicit serialization conflict permits replay of the
                # whole mutation with a new snapshot. Transport/commit ambiguity
                # and cleanup failures must propagate, never trigger replay.
                if (not write or not error.args or error.args[0] != 6235
                        or attempt + 1 == TRANSACTION_MAX_ATTEMPTS):
                    raise
            except BaseException:
                connection.rollback()
                raise
            finally:
                connection.close()
            time.sleep(TRANSACTION_RETRY_DELAY * (2 ** attempt))

    async def _run(self, operation, *, write=False, scoped=True):
        worker = asyncio.create_task(asyncio.to_thread(self._transaction, operation, write, scoped))
        cancelled = False
        while not worker.done():
            try:
                await asyncio.shield(worker)
            except asyncio.CancelledError:
                cancelled = True
            except BaseException:
                if cancelled:
                    raise asyncio.CancelledError from None
                raise
        if cancelled:
            # Retrieve an exception before propagating cancellation, avoiding an
            # unobserved task error. The worker has already closed its connection.
            if not worker.cancelled():
                worker.exception()
            raise asyncio.CancelledError
        return worker.result()

    async def initialize(self):
        def init(cursor):
            cursor.execute("INSERT INTO lr_seekdb_scopes(workspace,namespace) VALUES(%s,%s) "
                           "ON DUPLICATE KEY UPDATE id=id",
                           (_key(self.workspace, MAX_SCOPE_BYTES), _key(self.namespace, MAX_SCOPE_BYTES)))
            cursor.execute("SELECT id FROM lr_seekdb_scopes WHERE workspace=CAST(%s AS BINARY) "
                           "AND namespace=CAST(%s AS BINARY)",
                           (_key(self.workspace, MAX_SCOPE_BYTES), _key(self.namespace, MAX_SCOPE_BYTES)))
            return cursor.fetchone()["id"]
        self._scope = await self._run(init, write=True, scoped=False)

    async def index_done_callback(self):
        # Each mutation has already committed. No buffered operations to flush.
        return None

    def _nodes(self, cursor, node_ids):
        result = {}
        for batch in _chunks(list(dict.fromkeys(node_ids))):
            cursor.execute(f"SELECT id,node_key,attributes FROM lr_seekdb_nodes "
                           f"WHERE scope_id=%s AND node_key IN ({_key_marks(batch)})",
                           (self._scope, *map(_key, batch)))
            result.update({_label(row["node_key"]): row for row in cursor.fetchall()})
        return result

    async def get_nodes_batch(self, node_ids):
        return await self._run(lambda cursor: {
            key: _json(row["attributes"]) for key, row in self._nodes(cursor, node_ids).items()})

    async def get_node(self, node_id):
        return (await self.get_nodes_batch([node_id])).get(node_id)

    async def has_node(self, node_id):
        return node_id in await self.get_nodes_batch([node_id])

    async def has_nodes_batch(self, node_ids):
        return set(await self.get_nodes_batch(node_ids))

    @staticmethod
    def _attributes(data):
        validate_graph_attributes(data, context="SeekDB graph attributes")
        return json.dumps(data, ensure_ascii=False, allow_nan=False)

    def _upsert_nodes(self, cursor, rows):
        for batch in _chunks(rows):
            cursor.executemany("INSERT INTO lr_seekdb_nodes(scope_id,node_key,attributes) "
                               "VALUES(%s,%s,%s) ON DUPLICATE KEY UPDATE "
                               "attributes=JSON_MERGE_PATCH(attributes,VALUES(attributes))", batch)

    async def upsert_nodes_batch(self, nodes):
        # Validate/serialize the complete batch before the first write. Multiple
        # packets are still one transaction, never a sequence of partial commits.
        rows = [(self._scope, _key(key), self._attributes(data)) for key, data in nodes]
        await self._run(lambda cursor: self._upsert_nodes(cursor, rows), write=True)

    async def upsert_node(self, node_id, node_data):
        await self.upsert_nodes_batch([(node_id, node_data)])

    async def upsert_edges_batch(self, edges):
        prepared = []
        for source, target, data in edges:
            _key(source)
            _key(target)
            source, target = sorted((source, target))
            prepared.append((source, target, self._attributes(data)))
        def write(cursor):
            labels = sorted({node for source, target, _ in prepared for node in (source, target)})
            # NetworkX/LightRAG materializes missing endpoints when adding edges.
            self._upsert_nodes(cursor, [(self._scope, _key(node), "{}") for node in labels])
            nodes = self._nodes(cursor, labels)
            rows = [(self._scope, nodes[src]["id"], nodes[dst]["id"], attrs)
                    for src, dst, attrs in prepared]
            for batch in _chunks(rows):
                cursor.executemany("INSERT INTO lr_seekdb_edges VALUES(%s,%s,%s,%s) "
                                   "ON DUPLICATE KEY UPDATE attributes="
                                   "JSON_MERGE_PATCH(attributes,VALUES(attributes))", batch)
        await self._run(write, write=True)

    async def upsert_edge(self, source_node_id, target_node_id, edge_data):
        await self.upsert_edges_batch([(source_node_id, target_node_id, edge_data)])

    def _incident(self, cursor, node_ids):
        # UNION (not UNION ALL) lists a simple undirected edge once even for a
        # self-loop or when both endpoints are in the requested batch.
        edges = set()
        for batch in _chunks(list(dict.fromkeys(node_ids))):
            start = f"(a IS entity WHERE a.scope_id=%s AND a.node_key IN ({_key_marks(batch)}))"
            cursor.execute(
                f"SELECT src,dst FROM GRAPH_TABLE(lr_seekdb_graph MATCH {start}"
                "-[e IS relation]->(b IS entity) COLUMNS(a.node_key AS src,b.node_key AS dst)) g "
                f"UNION SELECT src,dst FROM GRAPH_TABLE(lr_seekdb_graph MATCH {start}"
                "<-[e IS relation]-(b IS entity) COLUMNS(b.node_key AS src,a.node_key AS dst)) g",
                (self._scope, *map(_key, batch), self._scope, *map(_key, batch)))
            edges.update((_label(row["src"]), _label(row["dst"])) for row in cursor.fetchall())
        return sorted(edges)

    async def get_node_edges(self, source_node_id):
        def read(cursor):
            if source_node_id not in self._nodes(cursor, [source_node_id]):
                return None
            # LightRAG entity merge/rename callers require the queried node
            # first, as NetworkX.edges(node) returns it. Canonical storage order
            # must not leak through this node-relative public API.
            return [(source_node_id, dst if src == source_node_id else src)
                    for src, dst in self._incident(cursor, [source_node_id])]
        return await self._run(read)

    async def get_nodes_edges_batch(self, node_ids):
        def read(cursor):
            result = {node: [] for node in node_ids}
            for src, dst in self._incident(cursor, node_ids):
                if src in result:
                    result[src].append((src, dst))
                if dst in result and src != dst:
                    result[dst].append((dst, src))
            return result
        return await self._run(read)

    async def node_degrees_batch(self, node_ids):
        def read(cursor):
            result = {node: 0 for node in node_ids}
            for batch in _chunks(list(result)):
                marks = _key_marks(batch)
                cursor.execute(
                    "SELECT node_key,COUNT(*) AS degree FROM ("
                    "SELECT n.node_key FROM lr_seekdb_nodes n JOIN lr_seekdb_edges e ON e.src=n.id "
                    f"WHERE e.scope_id=%s AND n.scope_id=%s AND n.node_key IN ({marks}) UNION ALL "
                    "SELECT n.node_key FROM lr_seekdb_nodes n JOIN lr_seekdb_edges e ON e.dst=n.id "
                    f"WHERE e.scope_id=%s AND n.scope_id=%s AND n.node_key IN ({marks})"
                    ") d GROUP BY node_key",
                    (self._scope, self._scope, *map(_key, batch),
                     self._scope, self._scope, *map(_key, batch)))
                result.update({_label(row["node_key"]): row["degree"] for row in cursor.fetchall()})
            return result
        return await self._run(read)

    async def node_degree(self, node_id):
        return (await self.node_degrees_batch([node_id]))[node_id]

    async def edge_degrees_batch(self, edge_pairs):
        degrees = await self.node_degrees_batch(list({node for pair in edge_pairs for node in pair}))
        return {(src, dst): degrees[src] + degrees[dst] for src, dst in edge_pairs}

    async def edge_degree(self, src_id, tgt_id):
        return (await self.edge_degrees_batch([(src_id, tgt_id)]))[(src_id, tgt_id)]

    def _edges(self, cursor, pairs):
        result = {}
        for batch in _chunks(list(dict.fromkeys(tuple(sorted(pair)) for pair in pairs))):
            cursor.execute("SELECT a.node_key AS src,b.node_key AS dst,e.attributes "
                           "FROM lr_seekdb_edges e JOIN lr_seekdb_nodes a ON a.id=e.src "
                           "JOIN lr_seekdb_nodes b ON b.id=e.dst WHERE e.scope_id=%s AND "
                           f"(a.node_key,b.node_key) IN ({','.join(['(CAST(%s AS BINARY),CAST(%s AS BINARY))'] * len(batch))})",
                           (self._scope, *(_key(node) for pair in batch for node in pair)))
            result.update({(_label(row["src"]), _label(row["dst"])): _json(row["attributes"])
                           for row in cursor.fetchall()})
        return result

    async def get_edges_batch(self, pairs):
        requested = [(pair["src"], pair["tgt"]) for pair in pairs]
        def read(cursor):
            rows = self._edges(cursor, requested)
            return {pair: rows[tuple(sorted(pair))] for pair in requested if tuple(sorted(pair)) in rows}
        return await self._run(read)

    async def get_edge(self, source_node_id, target_node_id):
        pairs = await self.get_edges_batch([{"src": source_node_id, "tgt": target_node_id}])
        return pairs.get((source_node_id, target_node_id))

    async def has_edge(self, source_node_id, target_node_id):
        return await self.get_edge(source_node_id, target_node_id) is not None

    async def remove_nodes(self, nodes):
        def write(cursor):
            for batch in _chunks(list(self._nodes(cursor, nodes).values())):
                ids = [row["id"] for row in batch]
                cursor.execute(f"DELETE FROM lr_seekdb_edges WHERE scope_id=%s AND "
                               f"(src IN ({_marks(ids)}) OR dst IN ({_marks(ids)}))",
                               (self._scope, *ids, *ids))
                cursor.execute(f"DELETE FROM lr_seekdb_nodes WHERE scope_id=%s AND id IN ({_marks(ids)})",
                               (self._scope, *ids))
        await self._run(write, write=True)

    async def delete_node(self, node_id):
        await self.remove_nodes([node_id])

    async def remove_edges(self, edges):
        def write(cursor):
            nodes = self._nodes(cursor, list({node for pair in edges for node in pair}))
            pairs = [tuple(sorted(pair)) for pair in edges]
            rows = [(self._scope, nodes[src]["id"], nodes[dst]["id"])
                    for src, dst in pairs if src in nodes and dst in nodes]
            for batch in _chunks(rows):
                cursor.executemany("DELETE FROM lr_seekdb_edges WHERE scope_id=%s AND src=%s AND dst=%s", batch)
        await self._run(write, write=True)

    def _labels(self, cursor, after=None, limit=None):
        sql = "SELECT node_key FROM lr_seekdb_nodes WHERE scope_id=%s"
        params = [self._scope]
        if after is not None:
            sql += " AND node_key>CAST(%s AS BINARY)"
            params.append(_key(after))
        sql += " ORDER BY node_key"
        if limit is not None:
            sql += " LIMIT %s"
            params.append(limit)
        cursor.execute(sql, params)
        return [_label(row["node_key"]) for row in cursor.fetchall()]

    async def get_all_labels(self):
        return await self._run(self._labels)

    async def iter_labels(self, batch_size):
        if batch_size <= 0:
            raise ValueError("batch_size must be positive")
        after = None
        while True:
            batch = await self._run(lambda cursor: self._labels(cursor, after, batch_size))
            if not batch:
                return
            yield batch
            after = batch[-1]

    def _popular(self, cursor, limit):
        cursor.execute("SELECT n.node_key,COUNT(*) AS degree FROM lr_seekdb_nodes n JOIN "
                       "(SELECT src AS id FROM lr_seekdb_edges WHERE scope_id=%s UNION ALL "
                       "SELECT dst AS id FROM lr_seekdb_edges WHERE scope_id=%s) d ON d.id=n.id "
                       "WHERE n.scope_id=%s GROUP BY n.node_key ORDER BY degree DESC,n.node_key LIMIT %s",
                       (self._scope, self._scope, self._scope, limit))
        labels = [_label(row["node_key"]) for row in cursor.fetchall()]
        if len(labels) < limit:
            cursor.execute("SELECT n.node_key FROM lr_seekdb_nodes n WHERE n.scope_id=%s "
                           "AND NOT EXISTS(SELECT 1 FROM lr_seekdb_edges e WHERE e.scope_id=%s "
                           "AND (e.src=n.id OR e.dst=n.id)) ORDER BY n.node_key LIMIT %s",
                           (self._scope, self._scope, limit - len(labels)))
            labels.extend(_label(row["node_key"]) for row in cursor.fetchall())
        return labels

    async def get_popular_labels(self, limit=300):
        if limit <= 0:
            return []
        return await self._run(lambda cursor: self._popular(cursor, limit))

    async def search_labels(self, query, limit=50):
        if not query.strip() or limit <= 0:
            return []
        def read(cursor):
            cursor.execute("SELECT node_key FROM lr_seekdb_nodes WHERE scope_id=%s "
                           "AND INSTR(LOWER(CONVERT(node_key USING utf8mb4)),LOWER(%s))>0 "
                           "ORDER BY (INSTR(LOWER(CONVERT(node_key USING utf8mb4)),LOWER(%s))=1) DESC,"
                           "node_key LIMIT %s", (self._scope, query, query, limit))
            return [_label(row["node_key"]) for row in cursor.fetchall()]
        return await self._run(read)

    def _all_nodes(self, cursor):
        cursor.execute("SELECT node_key,attributes FROM lr_seekdb_nodes WHERE scope_id=%s ORDER BY node_key",
                       (self._scope,))
        return [{**_json(row["attributes"]), "id": _label(row["node_key"])} for row in cursor.fetchall()]

    async def get_all_nodes(self):
        return await self._run(self._all_nodes)

    def _all_edges(self, cursor, after=None, limit=None):
        sql = ("SELECT a.node_key AS src,b.node_key AS dst,e.attributes FROM lr_seekdb_edges e "
               "JOIN lr_seekdb_nodes a ON a.id=e.src JOIN lr_seekdb_nodes b ON b.id=e.dst WHERE e.scope_id=%s")
        params = [self._scope]
        if after is not None:
            sql += " AND (a.node_key,b.node_key)>(CAST(%s AS BINARY),CAST(%s AS BINARY))"
            params.extend(map(_key, after))
        sql += " ORDER BY a.node_key,b.node_key"
        if limit is not None:
            sql += " LIMIT %s"
            params.append(limit)
        cursor.execute(sql, params)
        return [{**_json(row["attributes"]), "source": _label(row["src"]), "target": _label(row["dst"])}
                for row in cursor.fetchall()]

    async def get_all_edges(self):
        return await self._run(self._all_edges)

    async def export_snapshot(self):
        """Return {nodes, edges} from one namespace and one RR snapshot.

        Adapter-specific extension, not a LightRAG BaseGraphStorage method.
        Uses the get_all_nodes/get_all_edges record shapes, including isolated
        nodes and stored source/version attributes. Undirected relationships
        retain the adapter's single canonical edge record, without mirroring.
        No truncation or partial result is returned on failure. This snapshot
        covers graph storage only, not external vector/document stores, and
        carries no resumable snapshot token. Memory cost is O(nodes + edges +
        attributes); use only for namespaces that fit in client memory.
        """
        def read(cursor):
            # Do not compose the public async getters: each owns a different
            # transaction and may see a different graph during concurrent writes.
            return {"nodes": self._all_nodes(cursor), "edges": self._all_edges(cursor)}
        return await self._run(read)

    async def iter_edges(self, batch_size):
        if batch_size <= 0:
            raise ValueError("batch_size must be positive")
        after = None
        while True:
            batch = await self._run(lambda cursor: self._all_edges(cursor, after, batch_size))
            if not batch:
                return
            yield batch
            after = (batch[-1]["source"], batch[-1]["target"])

    async def get_knowledge_graph(self, node_label, max_depth=3, max_nodes=1000):
        cap = self.global_config.get("max_graph_nodes", 1000)
        budget = cap if max_nodes is None else min(max_nodes, cap)
        if budget <= 0 or not 0 <= max_depth <= 16:
            raise ValueError("positive node budget and depth between 0 and 16 required")
        def read(cursor):
            truncated = False
            if node_label == "*":
                labels = self._popular(cursor, budget + 1)
                truncated = len(labels) > budget
                selected = set(labels[:budget])
            else:
                selected = set(self._nodes(cursor, [node_label]))
                frontier = sorted(selected)
                for _ in range(max_depth):
                    neighbors = {node for pair in self._incident(cursor, frontier) for node in pair} - selected
                    remaining = budget - len(selected)
                    frontier = sorted(neighbors)[:remaining]
                    selected.update(frontier)
                    if len(neighbors) > remaining:
                        truncated = True
                        break
                    if not frontier:
                        break
            nodes = self._nodes(cursor, sorted(selected))
            pairs = [pair for pair in self._incident(cursor, sorted(selected))
                     if pair[0] in selected and pair[1] in selected]
            properties = self._edges(cursor, pairs)
            return KnowledgeGraph(
                nodes=[KnowledgeGraphNode(id=key, labels=[key], properties=_json(row["attributes"]))
                       for key, row in sorted(nodes.items())],
                edges=[KnowledgeGraphEdge(id=json.dumps(pair, ensure_ascii=False), type="RELATED",
                                          source=pair[0], target=pair[1], properties=properties[pair])
                       for pair in pairs], is_truncated=truncated)
        return await self._run(read)

    async def drop(self):
        def write(cursor):
            cursor.execute("DELETE FROM lr_seekdb_edges WHERE scope_id=%s", (self._scope,))
            cursor.execute("DELETE FROM lr_seekdb_nodes WHERE scope_id=%s", (self._scope,))
        await self._run(write, write=True)
        return {"status": "success", "message": "graph namespace cleared"}


def register():
    """Opt-in registration; importing the module alone changes no upstream registry."""
    from lightrag.kg import STORAGE_ENV_REQUIREMENTS, STORAGE_IMPLEMENTATIONS, STORAGES
    name = "SeekDBGraphStorage"
    implementations = STORAGE_IMPLEMENTATIONS["GRAPH_STORAGE"]["implementations"]
    if name not in implementations:
        implementations.append(name)
    STORAGE_ENV_REQUIREMENTS[name] = ["SEEKDB_USER", "SEEKDB_DATABASE"]
    STORAGES[name] = __name__
