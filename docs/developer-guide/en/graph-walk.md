# Bounded graph WALK queries

SeekDB supports bounded directed `WALK` patterns in `GRAPH_TABLE`. A walk may
visit the same vertex or edge more than once and preserves the multiplicity of
parallel edges and converging paths.

```sql
SELECT *
FROM GRAPH_TABLE(
  graph_social
  MATCH (a IS person WHERE a.id = 1)
        -[e IS knows WHERE e.since >= 2022]->{0,3}
        (b IS person WHERE b.age >= 18)
  ONE ROW PER STEP (v1, step_e, v2)
  COLUMNS (
    MATCHNUM() AS match_no,
    ELEMENT_NUMBER(step_e) AS step_no,
    VERTEX_ID(v1) AS from_id,
    EDGE_ID(step_e) AS edge_id,
    VERTEX_ID(v2) AS to_id
  )
) g;
```

## Supported syntax

| Feature | P2-WALK support |
| --- | --- |
| Bounds | `{n}`, `{n,m}`, `{,m}` with `0 <= n <= m <= 16` |
| Direction | Outgoing and incoming |
| Path mode | Implicit `WALK` |
| Pattern | One directed quantified edge segment |
| Default shape | `ONE ROW PER MATCH` |
| Step shape | `ONE ROW PER STEP (from,edge,to)` |
| Group edge projection | `COUNT(edge.property)`, `COUNT(EDGE_ID(edge))`, `JSON_ARRAYAGG(edge.property)`, and `JSON_ARRAYAGG(EDGE_ID(edge))` |
| Step projection | Iteration-variable properties, `MATCHNUM()`, `ELEMENT_NUMBER()`, `VERTEX_ID()`, and `EDGE_ID()` |
| Correlation | A start predicate may refer to the left side of an explicit `LATERAL` subquery |

`*`, `+`, `{n,}`, undirected edges, more than one quantified segment, a
quantified segment combined with another segment, named paths,
`TRAIL`/`SIMPLE`/`ACYCLIC`, and `ONE ROW PER VERTEX` are rejected explicitly.

## Result semantics

- The start predicate is evaluated once. The edge predicate is evaluated at
  every hop. The terminal predicate decides whether the current path is
  emitted, but does not stop expansion through an intermediate vertex.
- A zero-hop match is emitted only when the same vertex satisfies both endpoint
  patterns. In step shape it produces one row: the `from` variable is the real
  start vertex and the edge, `to`, and element number are `NULL`.
- `ELEMENT_NUMBER()` uses the element position in the alternating path: vertex
  positions are `1, 3, 5, ...` and edge positions are `2, 4, 6, ...`. Iterator
  variables must not reuse names declared in the graph pattern.
- `JSON_ARRAYAGG` follows traversal order. Its zero-hop result follows the
  existing empty-set aggregate result (`NULL`).
- Element identity is JSON with `GRAPH_OWNER`, `GRAPH_NAME`, `ELEM_TABLE`, and
  `KEY_VALUE`. Composite key members come from mapping-key declaration order;
  comparison uses typed key values rather than serialized JSON text.
- Results have bag semantics. Only an outer `DISTINCT` removes duplicates, and
  row order is unspecified without an outer `ORDER BY`.
- Every traversed edge is validated against an existing target vertex, even if
  no target property is projected. A missing endpoint removes that edge from
  the result; a scan, lookup, RPC, cancellation, timeout, or memory failure
  fails the whole query.

SeekDB does not auto-create an adjacency index. A suitable source/destination
prefix index can change the access plan, but cannot change paths or
multiplicity. There is no fan-out truncation, partial-success mode, or path
spill in P2-WALK.

## GraphRAG composition

Use an explicit lateral subquery to preserve each retrieval seed and score:

```sql
WITH seeds AS (
  SELECT person_id, l2_distance(embedding, '[0,0,0]') AS score
  FROM chunks
  ORDER BY score
  LIMIT 5
)
SELECT seeds.person_id, seeds.score, path.*
FROM seeds,
LATERAL (
  SELECT *
  FROM GRAPH_TABLE(
    graph_social
    MATCH (a IS person WHERE a.id = seeds.person_id)
          -[e IS knows]->{0,2}(b IS person)
    ONE ROW PER STEP (v1, step_e, v2)
    COLUMNS (
      MATCHNUM() AS match_no,
      ELEMENT_NUMBER(step_e) AS step_no,
      VERTEX_ID(v1) AS from_id,
      EDGE_ID(step_e) AS edge_id,
      VERTEX_ID(v2) AS to_id
    )
  ) g
) path;
```

The exact KNN form is appropriate for deterministic assertions. Approximate
retrieval can use the same composition, but its selected seeds are not a stable
test oracle.
