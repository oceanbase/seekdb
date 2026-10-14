# Copyright (c) 2025 OceanBase.
# Licensed under the Apache License, Version 2.0 (the "License");
# you may not use this file except in compliance with the License.
# You may obtain a copy of the License at http://www.apache.org/licenses/LICENSE-2.0
# Unless required by applicable law or agreed to in writing, software distributed
# under the License is distributed on an "AS IS" BASIS, WITHOUT WARRANTIES OR
# CONDITIONS OF ANY KIND, either express or implied. See the License for the
# specific language governing permissions and limitations under the License.

"""Run with requirements.txt: python -m unittest discover -s this_directory.

Uses real Microsoft GraphRAG table readers and hierarchical Leiden, no LLM.
Live database export is exercised by the existing LightRAG adapter integration
suite; these checks cover conversion and downstream product compatibility.
"""

import copy
import itertools
import json
import unittest

import pandas as pd
from graphrag.data_model.data_reader import DataReader
from graphrag.index.workflows.create_communities import create_communities
from graphrag_storage.memory_storage import MemoryStorage
from graphrag_storage.tables.parquet_table_provider import ParquetTableProvider

from seekdb_snapshot import build_communities, to_tables


def fixture():
    keys = ["A", "a", "中文", "D", "E", "F", "", "isolated"]
    nodes = [{"id": key, "description": "Entity " + key, "source_id": "doc1<SEP>doc2",
              "version": 7, "file_path": "source.txt"} for key in keys]
    pairs = list(itertools.combinations(keys[:3], 2)) + list(itertools.combinations(keys[3:6], 2))
    edges = [{"source": a, "target": b, "weight": 10,
              "description": a + " knows " + b, "source_id": "chunk1<SEP>chunk2", "version": 7}
             for a, b in pairs]
    return {"nodes": nodes, "edges": edges}


class SnapshotTests(unittest.IsolatedAsyncioTestCase):
    def convert(self, snapshot):
        return to_tables(snapshot, workspace="tenant", namespace="graph")

    async def test_real_community_workflow(self):
        snapshot = fixture()
        tables = self.convert(snapshot)
        provider = ParquetTableProvider(MemoryStorage())
        for name, frame in tables.items():
            await provider.write_dataframe(name, frame)
        reader = DataReader(provider)
        entities = await reader.entities()
        relationships = await reader.relationships()
        self.assertEqual(len(entities), 8)
        self.assertEqual(len(relationships), 6)
        async with provider.open("entities") as source, provider.open("communities") as target:
            await create_communities(target, source, relationships, max_cluster_size=10,
                                     use_lcc=False, seed=42)
        communities = await reader.communities()
        key_by_id = dict(zip(entities.id, entities.title))
        memberships = {frozenset(key_by_id[key] for key in ids) for ids in communities.entity_ids}
        self.assertEqual(memberships, {frozenset(["A", "a", "中文"]), frozenset(["D", "E", "F"])})
        # Isolated nodes remain in exported entities, but official edge-based
        # clustering does not assign them. Do not claim full node coverage.
        self.assertEqual(set(entities.title) - set().union(*memberships), {"", "isolated"})
        edge_ids = set(relationships.id)
        self.assertEqual(set().union(*(set(ids) for ids in communities.relationship_ids)), edge_ids)
        self.assertTrue(all(set(ids) == {"chunk1", "chunk2"} for ids in communities.text_unit_ids))
        provenance = await provider.read_dataframe("seekdb_provenance")
        self.assertEqual(set(provenance.id), set(entities.id) | edge_ids)
        for row in provenance.itertuples():
            raw = json.loads(row.attributes_json)
            self.assertEqual(raw["version"], 7)
            self.assertIn(raw, snapshot["nodes" if row.kind == "node" else "edges"])

    async def test_shapes_and_identity(self):
        snapshot = fixture()
        original = copy.deepcopy(snapshot)
        first = self.convert(snapshot)
        second = self.convert({"nodes": list(reversed(snapshot["nodes"])),
                               "edges": list(reversed(snapshot["edges"]))})
        for key in first:
            pd.testing.assert_frame_equal(first[key], second[key])
        self.assertEqual(snapshot, original)
        other = to_tables(snapshot, workspace="other", namespace="graph")
        self.assertFalse(set(first["seekdb_provenance"].id) & set(other["seekdb_provenance"].id))
        # ID framing cannot confuse a delimiter inside an endpoint with a pair.
        special = {"nodes": [{"id": key} for key in ["a|b", "c", "a", "b|c"]],
                   "edges": [{"source": "a|b", "target": "c"}, {"source": "a", "target": "b|c"}]}
        self.assertEqual(self.convert(special)["relationships"].id.nunique(), 2)
        loop = self.convert({"nodes": [{"id": ""}], "edges": [{"source": "", "target": ""}]})
        self.assertEqual(loop["entities"].degree.tolist(), [2])
        self.assertEqual(loop["relationships"].combined_degree.tolist(), [4])
        provider = ParquetTableProvider(MemoryStorage())
        for name, frame in self.convert({"nodes": [], "edges": []}).items():
            await provider.write_dataframe(name, frame)
            self.assertTrue((await provider.read_dataframe(name)).empty)

    async def test_complete_community_coverage(self):
        snapshot = fixture()
        # Mixed missing/present sources crashes upstream 3.3.0 row assembly.
        # These must remain missing, not become invented chunk IDs.
        snapshot["edges"][0].pop("source_id")
        snapshot["edges"].append({"source": "", "target": "", "description": "self"})
        tables = self.convert(snapshot)
        communities = build_communities(tables)
        by_id = dict(zip(tables["entities"].id, tables["entities"].title))
        memberships = {frozenset(by_id[key] for key in ids) for ids in communities.entity_ids}
        self.assertEqual(memberships, {frozenset(["A", "a", "中文"]), frozenset(["D", "E", "F"]),
                                      frozenset([""]), frozenset(["isolated"])})
        self.assertEqual(set().union(*(set(ids) for ids in communities.relationship_ids)),
                         set(tables["relationships"].id))
        self.assertEqual(set().union(*(set(ids) for ids in communities.text_unit_ids)),
                         {"doc1", "doc2", "chunk1", "chunk2"})
        pd.testing.assert_frame_equal(communities, build_communities(tables))
        empty = self.convert({"nodes": [], "edges": []})
        self.assertTrue(build_communities(empty).empty)
        solo = self.convert({"nodes": [{"id": "solo"}], "edges": []})
        row = build_communities(solo).iloc[0]
        self.assertEqual(row.entity_ids, solo["entities"].id.tolist())
        self.assertEqual(row.relationship_ids, [])
        self.assertEqual(row.text_unit_ids, [])
        provider = ParquetTableProvider(MemoryStorage())
        await provider.write_dataframe("communities", communities)
        actual = await DataReader(provider).communities()
        self.assertEqual(actual.id.tolist(), communities.id.tolist())

    async def test_invalid_input(self):
        for weight in [0, -1, float("nan"), float("inf"), True, "1"]:
            snapshot = fixture()
            snapshot["edges"][0]["weight"] = weight
            with self.assertRaises(ValueError):
                self.convert(snapshot)
        for kind in ["duplicate_node", "duplicate_edge", "orphan", "text", "json"]:
            snapshot = fixture()
            if kind == "duplicate_node":
                snapshot["nodes"].append(snapshot["nodes"][0])
            elif kind == "duplicate_edge":
                edge = snapshot["edges"][0]
                snapshot["edges"].append({**edge, "source": edge["target"], "target": edge["source"]})
            elif kind == "orphan":
                snapshot["nodes"].pop(0)
            elif kind == "text":
                snapshot["nodes"][0]["description"] = None
            else:
                snapshot["nodes"][0]["version"] = float("nan")
            with self.subTest(kind=kind), self.assertRaises(ValueError):
                self.convert(snapshot)

    async def test_hierarchy_and_cross_community_evidence(self):
        snapshot = fixture()
        snapshot["edges"].append({"source": "A", "target": "D", "weight": 0.01})
        tables = self.convert(snapshot)
        communities = build_communities(tables, max_cluster_size=2)
        entity_ids = dict(zip(tables["entities"].title, tables["entities"].id))
        rows = {row.community: row for row in communities.itertuples()}
        roots = [row for row in rows.values() if row.parent == -1]
        self.assertEqual(sorted(key for row in roots for key in row.entity_ids), sorted(entity_ids.values()))
        for row in rows.values():
            if row.parent != -1:
                self.assertIn(row.community, rows[row.parent].children)
                self.assertLess(rows[row.parent].level, row.level)
                self.assertLessEqual(set(row.entity_ids), set(rows[row.parent].entity_ids))
            expected = {edge.id for edge in tables["relationships"].itertuples()
                        if entity_ids[edge.source] in row.entity_ids and entity_ids[edge.target] in row.entity_ids}
            self.assertEqual(set(row.relationship_ids), expected)
        # A bridge not used in a community report remains available globally.
        self.assertEqual(len(tables["relationships"]), 7)

    async def test_nonempty_hierarchy(self):
        snapshot = {"nodes": [{"id": str(i)} for i in range(30)],
                    "edges": [{"source": str(i), "target": str((i + 1) % 30)} for i in range(30)]}
        tables = self.convert(snapshot)
        communities = build_communities(tables, max_cluster_size=3)
        rows = {row.community: row for row in communities.itertuples()}
        children = [row for row in rows.values() if row.parent != -1]
        self.assertTrue(children, "fixture must exercise hierarchical Leiden")
        for row in children:
            parent = rows[row.parent]
            self.assertIn(row.community, parent.children)
            self.assertEqual(row.level, parent.level + 1)
            self.assertLessEqual(set(row.entity_ids), set(parent.entity_ids))
        for row in rows.values():
            if row.children:
                self.assertEqual(sorted(key for child in row.children for key in rows[child].entity_ids),
                                 sorted(row.entity_ids))
        self.assertTrue(all(not ids for ids in communities.text_unit_ids))


if __name__ == "__main__":
    unittest.main()
