# Fresh bootstrap regression (#1544)

`test_fresh_bootstrap.py` starts a built server repeatedly with fresh data
directories and checks SQL readiness through a MySQL-compatible command-line
client. It requires no third-party Python packages. It stops at the first failure
and preserves every trial's console output, server logs and data, plus a JSON
report. It never retries a failed directory or deletes evidence.

On a Windows machine with built binaries, run the same test against the base
revision and the patched revision, using separate output directories:

```powershell
python unittest/observer/test_fresh_bootstrap.py --binary C:/build/base/seekdb.exe --client C:/mysql/bin/mysql.exe --output C:/bootstrap-evidence/base --trials 20
python unittest/observer/test_fresh_bootstrap.py --binary C:/build/patched/seekdb.exe --client C:/mysql/bin/mysql.exe --output C:/bootstrap-evidence/patched --trials 20
```

Use matching hardware, configuration and toolchain. Allow enough disk space for
all fresh instances, which can consume several GB each. The script's default is
three trials. Even twenty successful trials do not prove the absence of an
intermittent failure; report the exact trial count, timings and failures.

The script passes no server timeout overrides. Its 240-second outer guard is
separate from the internal metadata timeout. On Windows it terminates its child
after the readiness check, so this is **not** a graceful-shutdown or persistence
test. It checks direct server startup, not the Go SDK's native extraction cache.

Additional checks with a configured C++/server test environment:

| Case | Expected behavior |
| --- | --- |
| Windows, default 2-second `rpc_timeout`, no context/worker deadline | Metadata fallback is 10 seconds |
| macOS, same inputs | Existing 10-second fallback is preserved |
| Linux, same inputs | Existing 2-second fallback is preserved |
| Windows/macOS, `rpc_timeout` above 10 seconds | Larger configured value is preserved |
| Explicit valid context deadline | Context deadline is preserved |
| No context deadline, valid worker deadline | Worker deadline is preserved |
| Expired context or worker deadline | `OB_TIMEOUT`; no extension of an expired deadline |

These checks and the repeated startup runs must be reported separately from
Python syntax checks. A script that parses successfully is not runtime evidence
that the server fix works.
