# AI function enhancement verification

The contest specification is at
https://oceanbase.yuque.com/org-wiki-obtech-vh7w9r/hc3pio/psu9fks8dexldoum.
The implementation is based on `2026-competition`, commit
`5bce9e06d1281347ad61344e4b01fab9158c58a2`.

## Integration regression

Build and start **this checkout's** seekdb binary with a disposable `test`
database. Do not run against production: the test creates temporary models,
endpoints and a table using a process-specific prefix. It cleans them up on
normal success or failure.

Run with Node 18 or newer and a MySQL CLI on PATH:

```sh
node unittest/sql/engine/expr/ai_enhancement_integration.cjs
```

Environment overrides:

- `MYSQL_BIN`: path to the MySQL CLI.
- `MYSQL_PYTHON`: optional Python interpreter with PyMySQL available; uses
  `ai_mysql_client.py` instead of the MySQL CLI. The database still executes
  every AI function in the seekdb SQL engine.
- `MYSQL_ARGS`: JSON array of connection arguments; default
  `["-h127.0.0.1","-P2881","-uroot","-Dtest"]`.
- `MYSQL_PWD`: optional connection password, inherited by the MySQL CLI.
- `AI_MOCK_PORT`: mock HTTP port; default 8000.

The mock and database must share the same network namespace because the
endpoints point to `127.0.0.1`. No real model or model credentials are needed.
Do not start a second mock on the same port while running the official tests.

Coverage includes JSON prompt structure, nested and repeated placeholders,
100 KB LOB input, invalid placeholder overflow, JSON and NULL configuration,
native Ollama requests, requested/default embedding dimensions, 137-row
batching with NULLs and out-of-order responses, filtering/LIMIT, 47-document
reranking with stable ties and original IDs, malformed response rejection,
and successful execution after an error. Request inspection explicitly checks
that at least one embedding HTTP request carries multiple SQL rows.

This is supplemental coverage, not a substitute for the official mysqltest
suite and competition evaluator. The public base commit contains the official
SQL cases under `tools/deploy/mysql_test/test_suite/ai_function`; the two C++
unit-test files named in the task page are absent from that commit.

## Verification status

- JavaScript syntax and Git whitespace checks passed.
- All seven modified C++ translation units pass syntax-only compilation with
  the generated project compiler flags (including Prompt, Complete, Embed,
  Rerank, the shared utilities/client and Endpoint configuration).
- PyMySQL test-client import and Python syntax checks pass.
- Project Windows dependencies, Windows SDK and MSVC are available. CMake
  compiler self-tests pass with a writable workspace TEMP/TMP directory.
- CMake configure/generate succeeds with Python 3.12.10 and Cargo explicitly
  supplied. A completed build and database integration execution are still
  pending.
- `seekdb.exe` builds and boots on Windows (clang-cl 18 + Ninja + MSVC 2022).
- `ai_function_enhancement.test` and `ai_prompt.test` were replayed statement by
  statement against that binary; both outputs match the shipped `.result`
  baselines line for line (0 unexpected error codes).
  Two fixes were required for the end-to-end run:
  `AI model endpoint` rows now treat a NULL `access_key` as empty (an `ALTER`
  without `access_key` used to break keyless Ollama calls), and the
  `enable_file_sql` system variable stores its enum index as default so that
  observer bootstrap no longer fails with `-4226`.
- No official score has been obtained.
