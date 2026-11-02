# AI Function Regression Tests

Run these commands inside WSL from the repository root. Build the server:

```bash
python3 build.py build seekdb -j 2
```

Start the deterministic HTTP fixture in another terminal:

```bash
python3 tools/deploy/mysql_test/test_suite/ai_function/mock_ai_service.py
```

The fixture binds to `127.0.0.1:8000`. Run the suite using a disposable database
directory dedicated to this test run:

```bash
python3 tools/deploy/mysqltest.py run \
  --suite ai_function \
  --base-dir "$HOME/ai-enhancement-check" \
  --work-dir build/mysqltest-ai-enhancement \
  --port 2887
```

The runner recreates its `--base-dir`, initializes the database, compares output
against the checked-in result files, and stops its database instance afterward.
Stop the fixture with Ctrl+C after testing. No real model or API key is needed.

`ai_function_enhancement.test` covers long and nested prompts, JSON arguments,
repeated placeholders, Unicode and escaping, Completion configuration and invalid
responses, native Ollama generation and embedding, and Rerank batching, ties and
original document IDs. Embedding tests cover reordered provider responses,
NULLs, filtering, LIMIT, dimensions varying by row, and 1,100 input rows. The
fixture records the maximum input batch size so the test also checks that a
single HTTP request contains multiple inputs. Negative cases cover HTTP errors,
invalid vectors, duplicate or missing indices, and invalid reranking responses.
