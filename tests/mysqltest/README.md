# Collection set pruning regression

Run the case against an existing disposable test database and the patched
server. It creates and drops only `collection_set_pruning` in that database:

```sh
mysqltest --host=127.0.0.1 --port=2881 --user=root --database=test \
  --silent \
  --test-file="$PWD/tests/mysqltest/t/collection_set_pruning.test" \
  --result-file="$PWD/tests/mysqltest/r/collection_set_pruning.result"
```

Run `collection_set_pushdown.test` with its matching `.result` file using the
same command to cover physical partial DISTINCT on a partitioned table with
a multi-value index. It uses real duplicate ARRAY/MAP values and NULLs,
`parallel(2)`, and `DISTINCT_PUSHDOWN(@"SET$1")` targeting the SET parent.
The forced-hint case also runs with session pushdown disabled to verify that
the capability guard still wins when the hint overrides that setting. The
scalar control exercises partial DISTINCT. This case reenables session
pushdown before the ordinary collection DISTINCT negative check.

Use the normal client authentication mechanism for the endpoint; keep passwords
out of command arguments. The test is separate from the legacy suite under
`tools/` so new test-only files remain outside that directory.

The fixture contains real ARRAY, nested ARRAY and MAP columns, duplicate rows,
and duplicate NULL values. Constant-only queries can fold away DISTINCT and
cannot reproduce this defect. The case checks empty left/right/all branches,
three-branch UNION, INTERSECT, EXCEPT, and UNION ALL. Aggregates verify both
row counts and payloads.
The final SELECT DISTINCT over an ARRAY column must still return 5083.

SQL bounds on SMALLINT are included as an additional control. The primary
empty-branch cases use literal false predicates so they do not depend on type
range inference or other pruning rules.

These fixes retain the final set operator's duplicate elimination. They do
not add ordinary collection DISTINCT support or replace the pre-existing
planner strategy for UNION with more than two surviving branches; that
strategy can still use UNION ALL plus ordinary DISTINCT.

Run `collection_set_merge_capability.test` with its matching result file to
verify that collection set keys retain hash-set execution when a merge-set
candidate would require unsupported collection ordering. Real duplicate ARRAY,
nested ARRAY and MAP values plus NULLs check UNION/INTERSECT/EXCEPT semantics.
`NO_USE_HASH_SET(@"SET$1")` targets the set parent: the unsupported merge hint
must fall back to a valid hash plan, while scalar keys can still use merge.
The grouped case combines aggregate/scalar/collection outputs, HAVING and
branch ORDER BY/LIMIT, and another case keeps the empty LIMIT window. UNION
ALL preserves both branches and repeated NULLs. Explicit collection ORDER BY
must still report 5083. The positive tests do not force hash execution.

For an additional plan check, EXPLAIN the first MAP INTERSECT and the scalar
INTERSECT with the same scoped hint. The former must select HASH INTERSECT
without MERGE INTERSECT or a collection sort; the scalar control must retain
MERGE INTERSECT. EXPLAIN the grouped INTERSECT as well: scalar group/order
sorts remain valid, but the set parent must use HASH INTERSECT. These plan
checks accompany the native mysqltest runner so cost/timing numbers are not
part of the semantic golden output.
