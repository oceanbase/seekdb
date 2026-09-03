# seekdb-cli

`seekdb-cli` is a small SQL client for inspecting data in a running embedded
seekdb database. It talks the MySQL wire protocol directly over the embedded
database's local socket (`<data-dir>/run/sql.sock`) or over TCP in server
mode, and depends only on the Python standard library.

```bash
# attach to a database owned by a running pyseekdb application
python3 tools/seekdb-cli --data-dir ./agent_state.db

# one-shot statement
python3 tools/seekdb-cli -d ./agent_state.db -e "SELECT * FROM memory LIMIT 10;"

# batch (tab-separated) output for scripts and pipelines
python3 tools/seekdb-cli -d ./agent_state.db --batch -e "SHOW TABLES;"

# server mode
python3 tools/seekdb-cli -h 127.0.0.1 -P 2881
```

Run the unit tests with:

```bash
python3 tools/seekdb_cli_test.py
```
