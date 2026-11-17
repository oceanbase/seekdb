# Collection set pruning regression

Run the case against an existing disposable test database and the patched
server. It creates and drops only `collection_set_pruning` in that database:

```sh
mysqltest --host=127.0.0.1 --port=2881 --user=root --database=test \
  --silent \
  --test-file="$PWD/tests/mysqltest/t/collection_set_pruning.test" \
  --result-file="$PWD/tests/mysqltest/r/collection_set_pruning.result"
```

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
