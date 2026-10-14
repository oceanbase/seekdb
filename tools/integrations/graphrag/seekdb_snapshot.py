# Copyright (c) 2025 OceanBase.
# Licensed under the Apache License, Version 2.0 (the "License");
# you may not use this file except in compliance with the License.
# You may obtain a copy of the License at http://www.apache.org/licenses/LICENSE-2.0
# Unless required by applicable law or agreed to in writing, software distributed
# under the License is distributed on an "AS IS" BASIS, WITHOUT WARRANTIES OR
# CONDITIONS OF ANY KIND, either express or implied. See the License for the
# specific language governing permissions and limitations under the License.

"""Bridge a SeekDB LightRAG export_snapshot() to Microsoft GraphRAG 3.3.0.

Use to_tables(snapshot, workspace=..., namespace=...) and save each returned
DataFrame as <name>.parquet in a new GraphRAG output directory. Add communities
with build_communities(tables), then run the official create_community_reports
BYOG workflow. The
caller must finish publishing all tables before starting GraphRAG; this module
does not write files, contact a model, or read a second database snapshot.

This is a full in-memory conversion of the adapter's undirected simple graph,
not arbitrary GRAPH_TABLE output, incremental indexing or a streaming export.
All nodes (including isolates), edges (including self-loops) and raw attributes
are retained. Community construction uses GraphRAG's hierarchical Leiden with
all connected components, adding singleton communities for isolated nodes.
Neither source document existence nor graph/document snapshot atomicity is
implied by source_id references. Reports and answers still require an LLM.
"""

import hashlib
import json
import math
from datetime import datetime, timezone

import pandas as pd


ENTITY_COLUMNS = ["id", "human_readable_id", "title", "type", "description",
                  "text_unit_ids", "frequency", "degree"]
RELATIONSHIP_COLUMNS = ["id", "human_readable_id", "source", "target",
                        "description", "weight", "combined_degree", "text_unit_ids"]
PROVENANCE_COLUMNS = ["id", "kind", "workspace", "namespace", "attributes_json"]


def _text(value):
    if not isinstance(value, str):
        raise ValueError("graph identifiers and text attributes must be strings")
    return value


def _json(value):
    return json.dumps(value, ensure_ascii=False, sort_keys=True, allow_nan=False,
                      separators=(",", ":"))


def _identity(kind, workspace, namespace, keys):
    # JSON framing avoids delimiter collisions; namespace names are not numeric
    # database surrogate IDs and remain stable across export/reimport.
    return hashlib.sha256(_json([kind, workspace, namespace, keys]).encode("utf-8")).hexdigest()


def _sources(row):
    # LightRAG stores the extracted chunk IDs using GRAPH_FIELD_SEP=<SEP>.
    # Preserve original attributes separately, including source/version fields.
    source = _text(row.get("source_id", ""))
    return sorted({part for part in source.split("<SEP>") if part})


def to_tables(snapshot, *, workspace, namespace):
    """Return entities, relationships and seekdb_provenance DataFrames.

    IDs are stable within workspace/namespace and independent of input ordering.
    Human-readable integer IDs are export-local citation IDs, not SeekDB keys.
    seekdb_provenance joins on id and contains the full exported record, so a
    citation can be traced back to exact keys, source IDs and stored versions.
    No records are dropped or coalesced: duplicates/orphans/invalid weights raise.
    Empty snapshots produce typed, writable tables, not synthetic communities.
    """
    _text(workspace)
    _text(namespace)
    nodes = {}
    for row in snapshot["nodes"]:
        key = _text(row["id"])
        if key in nodes:
            raise ValueError("duplicate node identity")
        nodes[key] = row
    edges = {}
    degree = dict.fromkeys(nodes, 0)
    for row in snapshot["edges"]:
        src, dst = sorted((_text(row["source"]), _text(row["target"])))
        if src not in nodes or dst not in nodes:
            raise ValueError("edge endpoint missing from snapshot")
        if (src, dst) in edges:
            raise ValueError("duplicate undirected relationship")
        weight = row.get("weight", 1)
        if (isinstance(weight, bool) or not isinstance(weight, (int, float))
                or not math.isfinite(weight) or weight <= 0):
            raise ValueError("community edge weight must be finite and positive")
        edges[src, dst] = row
        degree[src] += 1
        degree[dst] += 1  # A self-loop contributes two to undirected degree.

    entities, relationships, provenance = [], [], []
    for number, (key, row) in enumerate(sorted(nodes.items())):
        identity = _identity("node", workspace, namespace, [key])
        sources = _sources(row)
        entities.append([identity, number, key, _text(row.get("entity_type", "")),
                         _text(row.get("description", "")), sources, len(sources), degree[key]])
        provenance.append([identity, "node", workspace, namespace, _json(row)])
    for number, ((src, dst), row) in enumerate(sorted(edges.items())):
        identity = _identity("edge", workspace, namespace, [src, dst])
        relationships.append([identity, number, src, dst, _text(row.get("description", "")),
                              float(row.get("weight", 1)), degree[src] + degree[dst], _sources(row)])
        provenance.append([identity, "edge", workspace, namespace, _json(row)])

    result = {
        "entities": pd.DataFrame(entities, columns=ENTITY_COLUMNS),
        "relationships": pd.DataFrame(relationships, columns=RELATIONSHIP_COLUMNS),
        "seekdb_provenance": pd.DataFrame(provenance, columns=PROVENANCE_COLUMNS),
    }
    # Retain numeric schema even for empty snapshots.
    result["entities"] = result["entities"].astype({"human_readable_id": "int64",
                                                  "frequency": "int64", "degree": "int64"})
    result["relationships"] = result["relationships"].astype(
        {"human_readable_id": "int64", "weight": "float64", "combined_degree": "int64"})
    return result


def build_communities(tables, *, max_cluster_size=10, seed=42):
    """Build GraphRAG community rows with exact entity/relationship/source IDs.

    Use only with tables returned together by to_tables(). This wrapper uses
    GraphRAG's real cluster_graph algorithm, but assembles rows locally because
    the 3.3.0 workflow drops isolated nodes and fails when a community mixes
    empty and nonempty source lists. No synthetic chunk references are created.
    Self-loops are retained as evidence, not treated as pre-reduced communities
    during Leiden optimization. Cross-community edges remain in relationships;
    like GraphRAG, community relationship_ids contains intra-community edges.
    """
    from graphrag.data_model.schemas import COMMUNITIES_FINAL_COLUMNS
    from graphrag.index.operations.cluster_graph import cluster_graph

    if isinstance(max_cluster_size, bool) or not isinstance(max_cluster_size, int) or max_cluster_size <= 0:
        raise ValueError("max_cluster_size must be a positive integer")
    entities = tables["entities"]
    relationships = tables["relationships"]
    clustering_edges = relationships[relationships.source != relationships.target]
    clusters = (cluster_graph(clustering_edges, max_cluster_size, use_lcc=False, seed=seed)
                if not clustering_edges.empty else [])
    covered = {key for _, _, _, keys in clusters for key in keys}
    next_id = max((community for _, community, _, _ in clusters), default=-1) + 1
    for key in sorted(set(entities.title) - covered):
        clusters.append((0, next_id, -1, [key]))
        next_id += 1

    nodes = {row.title: row for row in entities.itertuples()}
    rows, memberships = {}, {}
    for level, community, parent, keys in clusters:
        keys = sorted(keys)
        ids = sorted(nodes[key].id for key in keys)
        rows[community] = {
            "id": hashlib.sha256(_json(["community", level, ids]).encode("utf-8")).hexdigest(),
            "human_readable_id": community, "community": community, "level": level,
            "parent": parent, "children": [], "title": f"Community {community}",
            "entity_ids": ids, "relationship_ids": [],
            "text_unit_ids": {source for key in keys for source in nodes[key].text_unit_ids},
            "period": datetime.now(timezone.utc).date().isoformat(), "size": len(keys),
        }
        membership = memberships.setdefault(level, {})
        for key in keys:
            membership[key] = community
    for row in rows.values():
        if row["parent"] != -1:
            rows[row["parent"]]["children"].append(row["community"])
    # One pass per hierarchy level, not one full edge scan per community.
    for membership in memberships.values():
        for edge in relationships.itertuples():
            community = membership.get(edge.source)
            if community is not None and community == membership.get(edge.target):
                rows[community]["relationship_ids"].append(edge.id)
                rows[community]["text_unit_ids"].update(edge.text_unit_ids)
    for row in rows.values():
        for column in ("children", "relationship_ids", "text_unit_ids"):
            row[column] = sorted(row[column])
    return pd.DataFrame([rows[key] for key in sorted(rows)], columns=COMMUNITIES_FINAL_COLUMNS)
