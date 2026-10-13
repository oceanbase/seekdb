# Copyright (c) 2025 OceanBase.
# Licensed under the Apache License, Version 2.0 (the "License");
# you may not use this file except in compliance with the License.
# You may obtain a copy of the License at http://www.apache.org/licenses/LICENSE-2.0
# Unless required by applicable law or agreed to in writing, software distributed
# under the License is distributed on an "AS IS" BASIS, WITHOUT WARRANTIES OR
# CONDITIONS OF ANY KIND, either express or implied. See the License for the
# specific language governing permissions and limitations under the License.

"""Live integration checks; provision schema.sql in an isolated test database.

Run with the adapter's SEEKDB_* environment and pinned LightRAG available:
python tools/integrations/lightrag/test_seekdb_graph.py
Only unique test namespaces are modified; no database/table/graph is dropped.
"""

import asyncio
import tempfile
import threading
import uuid

import numpy as np
import pymysql
from lightrag import LightRAG, QueryParam
from lightrag.utils import EmbeddingFunc, Tokenizer, compute_mdhash_id, make_relation_chunk_key

from seekdb_graph import BATCH_SIZE, TRANSACTION_MAX_ATTEMPTS, SeekDBGraphStorage, register


def storage(workspace, namespace="graph"):
    return SeekDBGraphStorage(namespace=namespace, workspace=workspace,
                              global_config={}, embedding_func=None)


async def contract_checks():
    name = "adapter_" + uuid.uuid4().hex
    graph = storage(name)
    peer = storage(name)
    sibling = storage(name, "other")
    other = storage(name + "_other")
    empty_scope = storage("", name)
    stores = [graph, peer, sibling, other, empty_scope]
    for item in stores:
        await item.initialize()
    try:
        keys = ["", "A", "A ", "a", "中文", "quote'\\%_", "é", "e\u0301"]
        await graph.upsert_nodes_batch([(key, {"description": key, "version": 1}) for key in keys])
        await graph.upsert_nodes_batch([(key, {"version": 2}) for key in keys])
        assert await graph.get_all_labels() == sorted(keys)
        for key in keys:
            actual = await graph.get_node(key)
            assert actual == {"description": key, "version": 2}, (key, actual)
        assert await sibling.get_all_labels() == []
        assert await other.get_all_labels() == []
        await empty_scope.upsert_node("", {"value": 1})
        assert await empty_scope.get_node("") == {"value": 1}
        assert await graph.get_node_edges("missing") is None
        assert await graph.get_node_edges("A") == []
        assert await graph.node_degrees_batch(["missing", "A"]) == {"missing": 0, "A": 0}

        await graph.upsert_edges_batch([("B", "A", {"weight": 1}), ("A", "B", {"source_id": "chunk1"})])
        await graph.upsert_edge("B", "A", {"weight": 1})
        assert await graph.get_node("B") == {}
        assert await graph.get_edge("B", "A") == {"weight": 1, "source_id": "chunk1"}
        assert await graph.get_node_edges("B") == [("B", "A")]
        await sibling.upsert_edge("A", "namespace_only", {"value": 1})
        await other.upsert_edge("A", "workspace_only", {"value": 1})
        assert await graph.get_node_edges("A") == [("A", "B")]
        assert await graph.get_nodes_edges_batch(["A", "B", "missing"]) == {
            "A": [("A", "B")], "B": [("B", "A")], "missing": []}
        assert await graph.edge_degrees_batch([("A", "B"), ("missing", "A")]) == {
            ("A", "B"): 2, ("missing", "A"): 1}
        assert await graph.search_labels("quote'\\%_") == ["quote'\\%_"]
        assert await graph.get_popular_labels(2) == ["A", "B"]
        assert len(await graph.get_popular_labels(100)) == len(keys) + 1
        graph_view = await graph.get_knowledge_graph("A", max_depth=1)
        assert {node.id for node in graph_view.nodes} == {"A", "B"}
        assert len(graph_view.edges) == 1 and not graph_view.is_truncated
        capped = await graph.get_knowledge_graph("*", max_nodes=1)
        assert [node.id for node in capped.nodes] == ["A"] and capped.is_truncated

        # A legacy self-loop must remain visible exactly once for repair, even
        # though LightRAG's ingestion rejects creation of such relationships.
        await graph.upsert_edge("A", "A", {"legacy": True})
        assert (await graph.get_node_edges("A")).count(("A", "A")) == 1
        await graph.remove_edges([("A", "A")])

        batch = [(f"batch{i:04d}", {"value": i}) for i in range(513)]
        await graph.upsert_nodes_batch(batch)
        await graph.upsert_edges_batch([(batch[i][0], batch[i+1][0], {"value": i}) for i in range(512)])
        assert len(await graph.get_nodes_batch([key for key, _ in batch])) == 513
        degrees = await graph.node_degrees_batch([key for key, _ in batch])
        assert degrees == {f"batch{i:04d}": 1 if i in (0, 512) else 2 for i in range(513)}
        labels = [key async for page in graph.iter_labels(37) for key in page]
        assert labels == await graph.get_all_labels()
        edges = [edge async for page in graph.iter_edges(37) for edge in page]
        assert edges == await graph.get_all_edges() and len(edges) == 513

        # The first packet really reaches the database; failure before the next
        # packet must roll it back. Do not replace the DB with a mock.
        original = graph._upsert_nodes
        def fail_after_packet(cursor, rows):
            original(cursor, rows[:256])
            raise RuntimeError("intentional batch failure")
        graph._upsert_nodes = fail_after_packet
        try:
            try:
                await graph.upsert_nodes_batch([(f"rollback{i}", {"value": i}) for i in range(300)])
                raise AssertionError("batch failure was swallowed")
            except RuntimeError as error:
                assert str(error) == "intentional batch failure"
        finally:
            graph._upsert_nodes = original
        assert await peer.get_nodes_batch([f"rollback{i}" for i in range(300)]) == {}
        try:
            await graph.upsert_nodes_batch([("valid_before_bad", {"value": 1}), ("bad", {"value": None})])
            raise AssertionError("invalid attributes accepted")
        except ValueError:
            pass
        assert await graph.get_node("valid_before_bad") is None
        largest = "\U0001f680" * 512
        await graph.upsert_node(largest, {"value": 1})
        assert await graph.get_node(largest) == {"value": 1}
        try:
            await graph.upsert_node(largest + "x", {"value": 1})
            raise AssertionError("oversized identifier was silently truncated")
        except ValueError:
            pass

        # Independent instances/connections update one property map without
        # losing disjoint attributes. Per-namespace row lock includes deletions.
        semaphore = asyncio.Semaphore(4)
        async def writer(i):
            async with semaphore:
                target = graph if i % 2 else peer
                await target.upsert_node("concurrent", {f"k{i}": i})
                if i % 2:
                    await target.delete_node("race")
                else:
                    await target.upsert_edge("race", "concurrent", {"value": i})
        await asyncio.gather(*(writer(i) for i in range(20)))
        assert await graph.get_node("concurrent") == {f"k{i}": i for i in range(20)}
        def orphans(cursor):
            cursor.execute("SELECT COUNT(*) AS n FROM lr_seekdb_edges e "
                           "LEFT JOIN lr_seekdb_nodes a ON a.id=e.src "
                           "LEFT JOIN lr_seekdb_nodes b ON b.id=e.dst "
                           "WHERE e.scope_id=%s AND (a.id IS NULL OR b.id IS NULL)", (graph._scope,))
            return cursor.fetchone()["n"]
        assert await graph._run(orphans) == 0

        reached, resume = threading.Event(), threading.Event()
        def paused_write(cursor):
            graph._upsert_nodes(cursor, [(graph._scope, b"cancelled", '{"value":1}')])
            reached.set()
            if not resume.wait(5):
                raise RuntimeError("cancel test handshake timed out")
        task = asyncio.create_task(graph._run(paused_write, write=True))
        assert await asyncio.to_thread(reached.wait, 5)
        task.cancel()
        await asyncio.sleep(0.01)
        assert not task.done(), "cancelled write escaped while worker was still live"
        resume.set()
        try:
            await task
            raise AssertionError("cancellation lost")
        except asyncio.CancelledError:
            pass
        assert await peer.get_node("cancelled") == {"value": 1}
        await graph.delete_node("A")
        assert await graph.get_node("A") is None and await graph.get_node_edges("B") == []
        print("adapter contracts: passed", flush=True)
    finally:
        for item in (graph, sibling, other, empty_scope):
            await item.drop()


async def concurrent_batch_checks():
    workspace = "batch_snapshot_" + uuid.uuid4().hex
    reader, first, second = [storage(workspace) for _ in range(3)]
    for item in (reader, first, second):
        await item.initialize()
    keys = [f"node{i:04d}" for i in range(BATCH_SIZE + 1)]
    pairs = [(keys[i], keys[(i + 1) % len(keys)]) for i in range(len(keys))]
    requested = [{"src": src, "tgt": dst} for src, dst in pairs]
    initial = {"version": 0, "w0": 0, "w1": 0}
    tasks = []
    try:
        await first.upsert_nodes_batch([(key, initial) for key in keys])
        await first.upsert_edges_batch([(src, dst, initial) for src, dst in pairs])

        async def read_across_commit(method_name, read, write):
            original = getattr(reader, method_name)
            reached, resume = threading.Event(), threading.Event()
            def paused(cursor, items):
                # The first packet really establishes the transaction snapshot.
                # Only scheduling is controlled; both packets use the real DB.
                result = original(cursor, items[:BATCH_SIZE])
                reached.set()
                if not resume.wait(10):
                    raise RuntimeError("batch snapshot resume timed out")
                result.update(original(cursor, items[BATCH_SIZE:]))
                return result
            setattr(reader, method_name, paused)
            task = asyncio.create_task(read())
            try:
                assert await asyncio.to_thread(reached.wait, 10), "first packet was not read"
                await write()
                resume.set()
                result = await task
                assert len(result) == len(keys)
                assert all(row["version"] == 0 for row in result.values()), result
            finally:
                resume.set()
                # Drain the real DB worker before restoring the hook or cleanup.
                await asyncio.gather(task, return_exceptions=True)
                setattr(reader, method_name, original)

        await read_across_commit(
            "_nodes", lambda: reader.get_nodes_batch(keys),
            lambda: first.upsert_nodes_batch([(key, {"version": 1}) for key in keys]))
        assert all(row["version"] == 1 for row in (await reader.get_nodes_batch(keys)).values())
        await read_across_commit(
            "_edges", lambda: reader.get_edges_batch(requested),
            lambda: first.upsert_edges_batch([(src, dst, {"version": 1}) for src, dst in pairs]))
        assert all(row["version"] == 1 for row in (await reader.get_edges_batch(requested)).values())

        rounds = 8
        async def writer(store, field):
            for value in range(1, rounds + 1):
                await store.upsert_nodes_batch([(key, {field: value}) for key in keys])
                await store.upsert_edges_batch([(src, dst, {field: value}) for src, dst in pairs])
        tasks = [asyncio.create_task(writer(first, "w0")), asyncio.create_task(writer(second, "w1"))]
        checks = 0
        while not all(task.done() for task in tasks):
            for rows in (await reader.get_nodes_batch(keys), await reader.get_edges_batch(requested)):
                assert len(rows) == len(keys)
                # One call is atomic across packets. Different calls need not
                # observe the same epoch, nor do node/edge writes share a txn.
                for field in ("w0", "w1"):
                    assert len({row[field] for row in rows.values()}) == 1, (field, rows)
            checks += 1
        await asyncio.gather(*tasks)
        assert checks > 0, "concurrent readers were not exercised"
        expected = {"version": 1, "w0": rounds, "w1": rounds}
        assert await reader.get_nodes_batch(keys) == {key: expected for key in keys}
        assert await reader.get_edges_batch(requested) == {pair: expected for pair in pairs}
        incident = await reader.get_nodes_edges_batch(keys)
        assert incident == {
            key: sorted([(key, keys[(i - 1) % len(keys)]), (key, keys[(i + 1) % len(keys)])])
            for i, key in enumerate(keys)
        }
        print("adapter concurrent batches: snapshots and disjoint updates passed", flush=True)
    finally:
        for task in tasks:
            if not task.done():
                task.cancel()
        await asyncio.gather(*tasks, return_exceptions=True)
        await reader.drop()


class ByteTokenizer:
    def encode(self, text):
        return list(text.encode("utf-8"))

    def decode(self, tokens):
        return bytes(tokens).decode("utf-8", errors="replace")

    def __deepcopy__(self, memo):
        return self


async def embeddings(texts):
    # Fixed vectors exercise real product/storage APIs, not embedding quality.
    return np.asarray([[1.0, 0.0, 0.0, 0.0] for _ in texts], dtype=np.float32)


async def no_llm(*args, **kwargs):
    raise AssertionError("retrieval parity test must not invoke an LLM")


def canonical(result):
    data = result.get("data", {})
    return {
        "status": result.get("status"),
        "entities": sorted((row["entity_name"], row.get("description"), row.get("source_id"))
                           for row in data.get("entities", [])),
        "relationships": sorted((tuple(sorted((row["src_id"], row["tgt_id"]))), row.get("description"),
                                 row.get("source_id")) for row in data.get("relationships", [])),
        "chunks": sorted((row.get("content"), row.get("file_path")) for row in data.get("chunks", [])),
    }


async def product_checks():
    register()
    kg = {
        "chunks": [{"source_id": "c1", "content": "Alice works at Acme.", "file_path": "source-v1.txt"}],
        "entities": [{"entity_name": name, "entity_type": "ENTITY", "description": desc,
                      "source_id": "c1", "file_path": "source-v1.txt"}
                     for name, desc in [("Alice", "engineer"), ("Acme", "employer")]],
        "relationships": [{"src_id": "Alice", "tgt_id": "Acme", "description": "Alice works at Acme",
                           "keywords": "employment", "weight": 1.0, "source_id": "c1",
                           "file_path": "source-v1.txt"}],
    }
    with tempfile.TemporaryDirectory(prefix="seekdb-lightrag-") as root:
        options = dict(llm_model_func=no_llm, embedding_func=EmbeddingFunc(
            embedding_dim=4, max_token_size=8192, func=embeddings, model_name="storage_contract_fixture"),
            tokenizer=Tokenizer("offline-byte", ByteTokenizer()), enable_llm_cache=False)
        name = "product_" + uuid.uuid4().hex
        baseline = LightRAG(working_dir=root + "/original", workspace=name + "_original", **options)
        replacement = LightRAG(working_dir=root + "/seekdb", workspace=name,
                               graph_storage="SeekDBGraphStorage", **options)
        await baseline.initialize_storages()
        await replacement.initialize_storages()
        assert isinstance(replacement.chunk_entity_relation_graph, SeekDBGraphStorage)
        try:
            for product in (baseline, replacement):
                await product.ainsert_custom_kg(kg)
                await product.ainsert_custom_kg(kg)  # product-level idempotent retry
                # Separate ingestion timestamps deliberately: parity must not
                # depend on both products writing in the same wall-clock second.
                if product is baseline:
                    await asyncio.sleep(1.1)
            for mode in ("local", "global", "hybrid", "mix"):
                param = QueryParam(mode=mode, ll_keywords=["Alice"], hl_keywords=["employment"],
                                   top_k=20, chunk_top_k=20, max_total_tokens=12000, enable_rerank=False)
                expected = canonical(await baseline.aquery_data("Where does Alice work?", param))
                actual = canonical(await replacement.aquery_data("Where does Alice work?", param))
                assert actual == expected, (mode, actual, expected)
                assert actual["status"] == "success" and actual["chunks"], (mode, actual)
            print("actual LightRAG ingestion/retrieval: four modes passed", flush=True)
            # Acme sorts before Alice. A canonical Acme->Alice incident pair
            # would be skipped by LightRAG's requested-source merge collector.
            for product in (baseline, replacement):
                await product.amerge_entities(["Alice"], "Carol")
                graph = product.chunk_entity_relation_graph
                edge = await graph.get_edge("Acme", "Carol")
                assert edge is not None and edge["source_id"], "merge dropped a live relation"
                assert edge["file_path"] == "source-v1.txt"
                assert await graph.get_node("Alice") is None
                assert await graph.get_edge("Acme", "Alice") is None
                assert await product.relationships_vdb.get_by_id(compute_mdhash_id("AcmeAlice", prefix="rel-")) is None
                assert await product.relationships_vdb.get_by_id(compute_mdhash_id("AcmeCarol", prefix="rel-")) is not None
                assert await product.relation_chunks.get_by_id(make_relation_chunk_key("Acme", "Alice")) is None
                assert await product.relation_chunks.get_by_id(make_relation_chunk_key("Acme", "Carol")) is not None
            original_edge = await baseline.chunk_entity_relation_graph.get_edge("Acme", "Carol")
            replacement_edge = await replacement.chunk_entity_relation_graph.get_edge("Acme", "Carol")
            assert isinstance(original_edge["created_at"], int)
            assert isinstance(replacement_edge["created_at"], int)
            assert {key: value for key, value in original_edge.items() if key != "created_at"} == {
                key: value for key, value in replacement_edge.items() if key != "created_at"}
            print("actual LightRAG merge: relation/vector/provenance preserved", flush=True)
        finally:
            await replacement.chunk_entity_relation_graph.drop()
            await baseline.finalize_storages()
            await replacement.finalize_storages()


async def retry_checks():
    graph = storage('retry_' + uuid.uuid4().hex)
    await graph.initialize()
    try:
        lock_attempts = 0
        class LockConflictCursor(pymysql.cursors.DictCursor):
            def execute(self, query, args=None):
                nonlocal lock_attempts
                if (isinstance(query, str) and query.startswith('SELECT id FROM lr_seekdb_scopes')
                        and query.endswith('FOR UPDATE')):
                    lock_attempts += 1
                    if lock_attempts < TRANSACTION_MAX_ATTEMPTS:
                        return super().execute("SIGNAL SQLSTATE '25000' SET MYSQL_ERRNO=6235, MESSAGE_TEXT='lock retry fixture'")
                return super().execute(query, args)
        original_cursor = graph._options['cursorclass']
        graph._options['cursorclass'] = LockConflictCursor
        try:
            await graph.upsert_node('lock_retry', {'value': 1})
            assert lock_attempts == TRANSACTION_MAX_ATTEMPTS
        finally:
            graph._options['cursorclass'] = original_cursor
        assert await graph.get_node('lock_retry') == {'value': 1}

        async def injected(prefix, failures, code=6235):
            attempts = []
            def write(cursor):
                cursor.execute('SELECT CONNECTION_ID() AS id')
                attempts.append(cursor.fetchone()['id'])
                previous = [f'{prefix}{i}' for i in range(1, len(attempts))]
                assert graph._nodes(cursor, previous) == {}, 'earlier attempt was not rolled back'
                graph._upsert_nodes(cursor, [(graph._scope, f'{prefix}{len(attempts)}'.encode(), '{"value":1}')])
                if len(attempts) <= failures:
                    cursor.execute(f"SIGNAL SQLSTATE '25000' SET MYSQL_ERRNO={code}, MESSAGE_TEXT='retry fixture'")
                return len(attempts)
            try:
                result = await graph._run(write, write=True)
                assert result == failures + 1
            except pymysql.OperationalError as error:
                assert error.args[0] == code
                assert failures >= TRANSACTION_MAX_ATTEMPTS or code != 6235
                result = None
            expected_attempts = min(failures + 1, TRANSACTION_MAX_ATTEMPTS) if code == 6235 else 1
            assert len(attempts) == expected_attempts and len(set(attempts)) == len(attempts)
            keys = [f'{prefix}{i}' for i in range(1, TRANSACTION_MAX_ATTEMPTS + 1)]
            assert await graph.get_nodes_batch(keys) == (
                {f'{prefix}{result}': {'value': 1}} if result is not None else {})
        # These failures occur after a real SQL write. A retry of just the failed
        # statement, rather than the complete transaction, leaves visible markers.
        await injected('eventual', TRANSACTION_MAX_ATTEMPTS - 1)
        await injected('exhausted', TRANSACTION_MAX_ATTEMPTS)
        await injected('not_retryable', 1, 1205)

        reached, resume = threading.Event(), threading.Event()
        attempts = 0
        def cancelled_retry(cursor):
            nonlocal attempts
            attempts += 1
            graph._upsert_nodes(cursor, [(graph._scope, f'cancel{attempts}'.encode(), '{"value":1}')])
            if attempts == 1:
                cursor.execute("SIGNAL SQLSTATE '25000' SET MYSQL_ERRNO=6235, MESSAGE_TEXT='retry fixture'")
            reached.set()
            if not resume.wait(10):
                raise RuntimeError('retry cancellation handshake timed out')
        task = asyncio.create_task(graph._run(cancelled_retry, write=True))
        try:
            assert await asyncio.to_thread(reached.wait, 10)
            task.cancel()
            await asyncio.sleep(.01)
            assert not task.done(), 'cancel escaped while retry worker was live'
            resume.set()
            try:
                await task
                raise AssertionError('retry cancellation was swallowed')
            except asyncio.CancelledError:
                pass
        finally:
            resume.set()
            await asyncio.gather(task, return_exceptions=True)
        assert attempts == 2
        assert await graph.get_nodes_batch(['cancel1', 'cancel2']) == {'cancel2': {'value': 1}}
        print('adapter serialization retries: passed', flush=True)
    finally:
        await graph.drop()


async def main():
    await contract_checks()
    await retry_checks()
    await concurrent_batch_checks()
    await product_checks()


if __name__ == "__main__":
    asyncio.run(main())
