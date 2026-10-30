# ANN predicate absorption regression (#1535)

`check_ann_absorption.py` runs against an existing seekdb server using the `mysql`
CLI and Python 3. It creates a uniquely named test database and removes only that
database afterwards. It does not modify the server's global settings or existing
tables. Connection credentials can be supplied with `--defaults-extra-file`.

Run against a build containing the patch:

```sh
python check_ann_absorption.py --host 127.0.0.1 --port 2881 --user root
```

An explicit `--mysql /path/to/obclient` can select another compatible CLI.
Use `--output /path/to/results` to choose the artifact directory. Each invocation
writes a uniquely named subdirectory containing query plans and `summary.json`.

The test checks:

- `P AND (P OR Q) = P`, including a balanced tree with 1,024 repeated OR clauses.
- `P OR (P AND Q) = P`.
- Eligible IDs and ANN results with nullable category and parity columns.
- Absence of the redundant `parity` expression in the physical plan's
  `Outputs & filters` section, while still using a vector scan.

Against an unpatched build, run the same command without extra options first:
the residual-predicate assertion should fail. To collect all baseline plans
without asserting simplification, add `--observe-baseline`. This mode reports
`observed`, not `passed`.

The 32-row fixture is a correctness and rewrite regression. It does not measure
or establish the 43x latency improvement reported in the issue. Performance
validation requires the issue's larger dataset and paired ANN/exact measurements
on baseline and patched builds from the same source revision.

These database checks must be run on real servers; Python syntax checks alone
do not establish that the C++ fix works.
