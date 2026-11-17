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
the capability guard still wins when the hint overrides that setting. Scalar
keys remain eligible, UNION ALL preserves duplicates, and the session is
restored to enabled before the ordinary collection DISTINCT negative check.
Raw scalar, ARRAY and MAP UNION ALL queries label both branches and require
five rows from each, including duplicate collections and NULLs. These also
check that allocating a forced-partition iterator for one multi-value index
scan does not suppress the iterator needed by its sibling scan.

Use the normal client authentication mechanism for the endpoint; keep passwords
out of command arguments. The test is separate from the legacy suite under
`tools/` so new test-only files remain outside that directory.

The fixture contains real ARRAY, nested ARRAY and MAP columns, duplicate rows,
and duplicate NULL values. Constant-only queries can fold away DISTINCT and
cannot reproduce this defect. The case checks empty left/right/all branches,
three-branch UNION, INTERSECT, EXCEPT, UNION ALL, and UNION LIMIT. The ordered
LIMIT check requires duplicate elimination before limiting; the UNION ALL
control retains both NULL rows. Aggregates verify both row counts and payloads.
The final SELECT DISTINCT over an ARRAY column must still return 5083.

SQL bounds on SMALLINT are included as an additional control. The primary
empty-branch cases use literal false predicates so they do not depend on type
range inference or other pruning rules.

These fixes retain the final set operator's duplicate elimination. They do
not add ordinary collection DISTINCT support or replace the pre-existing
planner strategy for UNION with more than two surviving branches; that
strategy can still use UNION ALL plus ordinary DISTINCT.

Run `array_avg_integer_bounds.test` with its matching result file to check
integer ARRAY averages whose intermediate sum exceeds 64 bits. Its signed
and unsigned columns contain extreme values, cancellation, NULL elements,
and ordinary controls. Both stored-column evaluation and scalar subqueries
must return the existing DOUBLE result without a spurious 1690. Exact
cancellation checks retain low bits, including `[INT64_MIN, INT64_MAX]` and
`[INT64_MAX, 1, -INT64_MAX]`. ARRAY SUM must still reject true signed and
unsigned 64-bit overflow; the case does not broaden the result type of SUM.

Run `array_difference_integer_bounds.test` with its matching result file to
check the signed BIGINT result range for signed and unsigned input arrays.
Unsigned values above INT64_MAX must retain their magnitude until subtraction:
representable high-value differences and the exact INT64_MIN boundary succeed,
while unrepresentable differences report 1690 instead of wrapping. Stored
columns, a scalar subquery, a two-row batch, NULL/empty arrays, existing NULL
element rejection and small-integer/floating controls are included. The input
arrays and the result type are not narrowed to hide overflow.
