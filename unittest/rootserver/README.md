# Windows bootstrap timeout regression

This black-box lifecycle check covers seekdb issue [#1544](https://github.com/oceanbase/seekdb/issues/1544).

It creates isolated instances and verifies startup, a committed write, close,
reopen, and persistence with both the default RPC budget and an intentionally
short `rpc_timeout=50ms`. Global-stat initialization should use the internal SQL
budget even when the RPC budget is short. Startup failures retain their data
and logs, and print the instance directory.

Requirements:

- Windows x64 and Python 3.8 or newer.
- A built seekdb executable and its adjacent runtime DLLs.
- The public C driver `seekdb_driver.dll`, available in the official
  `pylibseekdb` Windows wheel (tested with `1.4.0.post2`).

Run from the source checkout, using paths to the runtime being checked:

```powershell
$env:SEEKDB_TEST_DRIVER = 'C:\path\to\pylibseekdb.libs\seekdb_driver.dll'
$env:SEEKDB_TEST_BINARY = 'C:\path\to\candidate-runtime\seekdb.exe'
python unittest/rootserver/test_bootstrap_timeout.py -v
```

Use an ASCII executable path with the tested driver version. The test uses the
driver's public binary override and lifecycle API; it does not use an existing
database or its credentials. It disables optional telemetry for the temporary
instances and restores the environment afterward.

The short-RPC case exercises the timeout-budget interaction. The installed
`1.4.0.post2` runtime failed this case at global-stat initialization, while both
unmodified current master and the patched build passed locally. This check does
not deterministically reproduce the issue on current master or measure the
original report's intermittent failure rate under the default two-second RPC
configuration.
