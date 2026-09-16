#!/usr/bin/env python3
# Copyright (c) 2026 OceanBase.
#
# Licensed under the Apache License, Version 2.0 (the "License");
# you may not use this file except in compliance with the License.
# You may obtain a copy of the License at
#
#     http://www.apache.org/licenses/LICENSE-2.0
#
# Unless required by applicable law or agreed to in writing, software
# distributed under the License is distributed on an "AS IS" BASIS,
# WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
# See the License for the specific language governing permissions and
# limitations under the License.

"""Compare bounded GRAPH_TABLE WALK results with an independent enumerator."""

import argparse
from collections import Counter, defaultdict
import random
import subprocess
import sys


DATABASE = "graph_walk_oracle"


def parse_args():
    parser = argparse.ArgumentParser()
    parser.add_argument("--client", required=True)
    parser.add_argument("--host", default="127.0.0.1")
    parser.add_argument("--port", type=int, required=True)
    parser.add_argument("--user", default="root")
    parser.add_argument("--password", default="")
    parser.add_argument("--keep", action="store_true")
    return parser.parse_args()


class Database:
    def __init__(self, args):
        self.command = [
            args.client,
            "-h{}".format(args.host),
            "-P{}".format(args.port),
            "-u{}".format(args.user),
            "-N",
            "-B",
        ]
        if args.password:
            self.command.append("-p{}".format(args.password))

    def query(self, sql, database=None):
        command = list(self.command)
        if database:
            command.append("-D{}".format(database))
        command.extend(["-e", sql])
        result = subprocess.run(
            command, check=False, stdout=subprocess.PIPE, stderr=subprocess.PIPE, text=True
        )
        if result.returncode != 0:
            raise RuntimeError(
                "query failed ({}): {}".format(result.returncode, result.stderr.strip())
            )
        return result.stdout


def make_graph():
    rng = random.Random(20260916)
    vertices = list(range(1, 7))
    edges = [
        (1, 1, 2),
        (2, 1, 2),  # parallel edge
        (3, 2, 2),  # self loop
        (4, 2, 3),
        (5, 3, 1),  # cycle
        (6, 4, 3),  # convergence
        (7, 1, 99),  # orphan target
        (8, 98, 1),  # orphan source
    ]
    for edge_id in range(9, 25):
        edges.append((edge_id, rng.choice(vertices), rng.choice(vertices)))
    return vertices, edges


def enumerate_paths(vertices, edges, seeds, direction, hop):
    vertex_set = set(vertices)
    adjacency = defaultdict(list)
    for edge_id, source, target in edges:
        current, next_vertex = (source, target) if direction == "OUT" else (target, source)
        if current in vertex_set and next_vertex in vertex_set:
            adjacency[current].append((edge_id, next_vertex))
    expected = Counter()
    for binding_id, seed in enumerate(seeds):
        frontier = [(seed, (seed,), ())]
        for _ in range(hop):
            next_frontier = []
            for current, path_vertices, path_edges in frontier:
                for edge_id, target in adjacency[current]:
                    next_frontier.append(
                        (
                            target,
                            path_vertices + (target,),
                            path_edges + (edge_id,),
                        )
                    )
            frontier = next_frontier
        for _, path_vertices, path_edges in frontier:
            expected[(binding_id, path_vertices, path_edges)] += 1
    return expected


def read_engine_paths(db, seeds, direction, hop):
    actual = Counter()
    arrow = "-[e IS knows]->" if direction == "OUT" else "<-[e IS knows]-"
    for binding_id, seed in enumerate(seeds):
        sql = """
SELECT match_no,step_no,from_id,edge_id,to_id
FROM GRAPH_TABLE(oracle_graph MATCH
  (a IS person WHERE a.id={seed}){arrow}{{{hop}}}(b IS person)
  ONE ROW PER STEP (v1,step_e,v2)
  COLUMNS(MATCHNUM() AS match_no,ELEMENT_NUMBER(step_e) AS step_no,
          v1.id AS from_id,step_e.id AS edge_id,v2.id AS to_id)) g
ORDER BY match_no,step_no
""".format(seed=seed, arrow=arrow, hop=hop)
        rows = []
        for line in db.query(sql, DATABASE).splitlines():
            fields = line.split("\t")
            if len(fields) != 5:
                raise RuntimeError("unexpected row: {!r}".format(line))
            rows.append(fields)
        grouped = defaultdict(list)
        for match_no, step_no, from_id, edge_id, to_id in rows:
            grouped[int(match_no)].append((step_no, from_id, edge_id, to_id))
        for steps in grouped.values():
            if hop == 0:
                if len(steps) != 1 or steps[0][0] != "NULL":
                    raise AssertionError("invalid zero-hop shape: {}".format(steps))
                path_vertices = (int(steps[0][1]),)
                path_edges = ()
            else:
                steps.sort(key=lambda row: int(row[0]))
                path_vertices = (int(steps[0][1]),) + tuple(int(row[3]) for row in steps)
                path_edges = tuple(int(row[2]) for row in steps)
            actual[(binding_id, path_vertices, path_edges)] += 1
    return actual


def setup(db, vertices, edges):
    vertex_values = ",".join("({})".format(value) for value in vertices)
    edge_values = ",".join(
        "({},{},{})".format(edge_id, source, target)
        for edge_id, source, target in edges
    )
    db.query(
        """
DROP DATABASE IF EXISTS {database};
CREATE DATABASE {database};
USE {database};
CREATE TABLE oracle_vertex(id BIGINT PRIMARY KEY);
CREATE TABLE oracle_edge(id BIGINT PRIMARY KEY,src BIGINT,dst BIGINT);
INSERT INTO oracle_vertex VALUES {vertex_values};
INSERT INTO oracle_edge VALUES {edge_values};
CREATE PROPERTY GRAPH oracle_graph
  VERTEX TABLES (oracle_vertex KEY(id) LABEL person PROPERTIES(id))
  EDGE TABLES (oracle_edge KEY(id)
    SOURCE KEY(src) REFERENCES oracle_vertex(id)
    DESTINATION KEY(dst) REFERENCES oracle_vertex(id)
    LABEL knows PROPERTIES(id));
""".format(database=DATABASE, vertex_values=vertex_values, edge_values=edge_values)
    )


def main():
    args = parse_args()
    db = Database(args)
    vertices, edges = make_graph()
    seeds = [1, 1, 2, 4, 5]
    setup(db, vertices, edges)
    try:
        for direction in ("OUT", "IN"):
            for hop in range(5):
                expected = enumerate_paths(vertices, edges, seeds, direction, hop)
                actual = read_engine_paths(db, seeds, direction, hop)
                if actual != expected:
                    missing = expected - actual
                    extra = actual - expected
                    raise AssertionError(
                        "{} {} hop mismatch\nmissing={}\nextra={}".format(
                            direction, hop, missing, extra
                        )
                    )
                print(
                    "PASS direction={} hop={} paths={}".format(
                        direction, hop, sum(actual.values())
                    )
                )
    finally:
        if not args.keep:
            db.query("DROP DATABASE IF EXISTS {};".format(DATABASE))
    return 0


if __name__ == "__main__":
    try:
        sys.exit(main())
    except Exception as error:
        print("FAIL: {}".format(error), file=sys.stderr)
        sys.exit(1)
