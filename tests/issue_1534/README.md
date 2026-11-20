# Issue #1534: Redundant Vector Filters

Reproduction and regression tests for:
https://github.com/oceanbase/seekdb/issues/1534

## Test Environment

- Host: 127.0.0.1
- Port: 2881
- Database: bug1471
- Table: vector_test
- Vector index: HNSW

## Run

Before the fix:
python tests/issue_1534/test_redundant_vector_filters.py --mode before

After building and running the patched seekdb:
python tests/issue_1534/test_redundant_vector_filters.py --mode after

## Current Status

- Bug reproduced: yes
- C++ patch written: yes
- C++ compilation: not verified
- Patched execution plan: not verified
- Patched performance: not verified

This test currently requires an existing database fixture.
