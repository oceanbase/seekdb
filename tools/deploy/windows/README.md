# Running SeekDB on Windows

`seekdb_windows.py` helps you run a local SeekDB instance on Windows x64
without administrator rights and without registering a Windows service.

Everything the instance needs lives in a single *home* directory:

```
<home>/bin      seekdb.exe and the observer helpers
<home>/etc      seekdb.cnf
<home>/share    static resources shipped with the package
<home>/store    data directory and redo log
<home>/run      runtime files, including telemetry.json
<home>/log      server log
```

## Prerequisites

* A SeekDB package for Windows x64, or a local build of the observer.
  The official releases publish `seekdb-<version>-win64.msi`; the MSI can be
  unpacked without installing it:

  ```
  msiexec /a seekdb-<version>-win64.msi /qn TARGETDIR=D:\payload
  ```

  The unpacked payload contains `PFiles64\seekdb\{bin,etc,share}`.

* Python 3.8 or newer.  PyMySQL is optional and only enables the SQL
  round-trip part of `verify`.

## Quick start

```
# 1. create the home directory and write seekdb.cnf
python seekdb_windows.py --home D:\seekdb_home layout --payload D:\payload\PFiles64\seekdb

# 2. start the observer
python seekdb_windows.py --home D:\seekdb_home start

# 3. check that the instance is healthy
python seekdb_windows.py --home D:\seekdb_home verify
```

The observer listens on `127.0.0.1:2881` by default and accepts the `root`
account with an empty password, so a plain MySQL client is enough to connect:

```
mysql -h 127.0.0.1 -P 2881 -u root -e "SELECT VERSION();"
```

## Commands

| Command | Description |
| --- | --- |
| `layout` | Create the home directory, link or copy `bin`, `etc` and `share` from the package and write `seekdb.cnf`. |
| `start` | Start `seekdb.exe` and wait until the observer port accepts connections. |
| `stop` | Stop a running observer. |
| `status` | Print the process state, the port state and the telemetry UUID. |
| `telemetry` | Print only the telemetry UUID. |
| `verify` | Self-check: required files, process, port, SQL round-trip and telemetry UUID. |

Common options:

```
--home <dir>            seekdb home directory (required)
--port <n>              observer port, default 2881
--memory-limit <size>   memory limit written to seekdb.cnf, default 4G
--cpu-count <n>         cpu_count written to seekdb.cnf, default 8
--timeout <seconds>     how long "start" waits for the port, default 240
```

## Telemetry UUID

The observer records a telemetry document at `<home>/run/telemetry.json`. The
instance identifier is the `content.id` field, which is a canonical UUID
(8-4-4-4-12, RFC 9562 UUID v8 derived from the machine ID):

```json
{
  "component": "seekdb",
  "content": {
    "id": "3f6b1c2e-9d84-4a51-8b0e-6c7d5a2f1e93",
    "version": "seekdb-v1.3.0.0",
    "event": "start"
  }
}
```

`telemetry` and `verify` print that value, and `verify` additionally checks
that it is a canonical UUID.

## Notes

* The helper never touches system directories and never registers a service.
  `bin`, `etc` and `share` are linked to the package payload with directory
  junctions when possible, so the package can be removed afterwards.
* `base-dir`, `data-dir` and `redo-dir` in `seekdb.cnf` are written with
  forward slashes, which is the form the observer expects on Windows.
* The instance writes its server log to `<home>/log/seekdb.log`.
