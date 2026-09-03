# seekdb-cli

`seekdb-cli` is a small SQL client for inspecting data in a running embedded
seekdb database. It talks the MySQL wire protocol directly over the embedded
database's local socket (`<data-dir>/run/sql.sock`, or a named pipe via
`<data-dir>/run/sql.pipe` on Windows) or over TCP in server mode, and depends
only on the Python standard library.

The local-socket path works on Linux and macOS. Windows embedded mode uses
a named pipe published via the `run/sql.pipe` discovery file; connecting to it
requires Python 3.9+ on Windows (older versions should use `--host`/`--port`
TCP mode instead).

```bash
# attach to a database owned by a running pyseekdb application
python3 tools/seekdb-cli --data-dir ./agent_state.db

# list tables, then query a pyseekdb collection's rows
seekdb> SHOW TABLES;
seekdb> SELECT * FROM sdk_collections LIMIT 10;
seekdb> SELECT _id, document FROM `c$v2$<collection-id>` LIMIT 10;

# one-shot statement
python3 tools/seekdb-cli -d ./agent_state.db -e "SELECT count(*) FROM sdk_collections;"

# batch (tab-separated) output for scripts and pipelines
python3 tools/seekdb-cli -d ./agent_state.db --batch -e "SHOW TABLES;"

# server mode
python3 tools/seekdb-cli -h 127.0.0.1 -P 2881

# Windows (Python 3.9+): the named pipe is discovered automatically
python tools\seekdb-cli --data-dir .\agent_state.db
```

Any MySQL-protocol client can attach to the same socket, for example the
official `mysql` CLI:

```bash
mysql -S agent_state.db/run/sql.sock -u root
```

In installed packages `seekdb-cli` is also available on `PATH`
(`/usr/bin/seekdb-cli` on Linux, `/opt/seekdb/libexec/seekdb/seekdb-cli` on macOS).

Collections created by pyseekdb are stored as `c$v2$<collection-id>` tables
and registered in the `sdk_collections` catalog table.

If you attach while the embedded engine is still starting up, DDL statements
can block until startup completes; raise `--timeout` (default 30s) if needed.

Run the unit tests with:

```bash
python3 tools/seekdb_cli_test.py
```
