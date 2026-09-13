# SEEK-533 Windows path validation tools

The approved scope is Windows 11 x64, local NTFS,
embedded base directories up to 2048 UTF-16 units and complete file paths up to 4096,
under both LongPathsEnabled=0 and 1. No aliases, VFS replacement or policy
requirement may silently replace this contract.

## Current product checks

After building the native product through `build.ps1 native-startup`, use:

```powershell
.\build.ps1 native-cli-smoke
.\build.ps1 native-cli-preflight
.\build.ps1 native-cli-reader
.\build.ps1 native-cli-sql
.\build.ps1 native-product-identity
```

`native-startup-contract` runs the existing startup-context test, including
real child argv round trips, path budgets, allocation failures and unchanged cwd.
It uses a new disposable root and preserves a failed fixture for diagnosis.

`native-install` enables the existing Windows package install rules in the native
build graph, builds `seekdb`, and installs the server component into a new task
prefix. It verifies EXE/DLL/license identities and runs maximum Unicode daemon
SQL initialization/restart under policies 0 and 1 with build dependency paths
removed from PATH. It restores the policy and keeps the installed directory and
evidence. This exercises the install tree; it does not build or install an MSI.
`native_cli_sql.py --exe` selects that installed product without replacing the
build output. Missing configurator or MSI coverage is reported separately.
`SEEKDB_NATIVE_SQL_EXE` provides the same selection through `build.ps1 native-cli-sql`.
A SQL pass against a partially installed runtime does not make a failed server
component installation pass; preserve the install exit code and missing inputs.

The SQL check starts the actual product, reads the instance discovery file,
connects to its named pipe, checks loaded EXE/SQLite identities, commits and
rolls back writes, then restarts and verifies persistent data. Set
`SEEKDB_NATIVE_SQL_BASE_UNITS`, `SEEKDB_NATIVE_SQL_UNICODE=1`,
`SEEKDB_NATIVE_SQL_DAEMON=1`, and `SEEKDB_NATIVE_SQL_DEFAULT_TCP=1` to select
the maximum Unicode path and the default daemon/TCP behavior. These variables
are test inputs, not product configuration variables. The check records the
actual system policy without changing it. Dual-instance and extra SSTable
freeze/compaction tests are outside SEEK-533 acceptance; their historical
evidence is retained separately, and they are not publication gates.


`native-cli-reader` reproduces the CRT discovery reader sharing failure using
a file held with DELETE access, verifies the native share-delete reader, and
checks that real sharing denial, missing files and malformed content still fail.
The SQL readiness loop only waits for missing discovery files; it propagates
permission and sharing errors with their original Win32 codes.

`native-cli-preflight` launches the actual product suspended into a private
Windows Job before resuming it. Its default-daemon and nodaemon rejection cases
require exit 2, exactly one process, specific diagnostics, no created target
directories, and an unchanged original-cwd sentinel. It covers base 2049,
data/redo 4097, a 256-unit component, reserved names, trailing dots and invalid
UTF-16. It neither initializes a database nor changes the system path policy.

`SEEKDB_NATIVE_SQL_CLEANUP_SUCCESS=1` removes only the new test's disposable
stores after all SQL/restart/exit checks and file inventories succeed. Logs,
configuration, identities and failed instances are retained. No existing user
database is accepted as a test target.

`native-product-identity` extracts the manifest resource from the distribution
EXE. Set `SEEKDB_NATIVE_IDENTITY_INSTANCE_RESULT` to a successful current-product
SQL run's `result.json` to also check its instance EXE copy. The executable
hashes must agree and both embedded manifests must enable `longPathAware`.

`native-service` installs a uniquely named disposable service from a short ASCII
installation directory, connects to its discovered pipe, commits SQL data, stops
it through SCM, restarts and reads the data, then stops and uninstalls it. It
records both the actual process exit and SCM status. The existing service stop
handler terminates its process; this check is not evidence of graceful shutdown.
An embedded client-release exit bypasses SCM reporting and is not the service
stop scenario exercised by this check. Existing services are never reused.
Set `SEEKDB_NATIVE_SQL_EXE` to select an extracted package EXE for this test.

`native-sql-tls` enables the existing SQL TLS configuration on a new 2048-unit
Unicode instance. It generates disposable certificates inside the test run,
loads the wallet from the instance while leaving an invalid wallet in the
original cwd, and performs encrypted SQL over the discovered named pipe. It
checks trusted and anonymous TLS clients, untrusted client rejection, restart
readback, and invalid-wallet startup rejection without endpoint publication.
Private test keys are neither printed nor included in exported reports.

`native-standby-tls` checks the existing non-embedded standby mTLS service in a
short ASCII instance, launched from a different cwd containing an unrelated
wallet. Non-embedded Windows startup retains the legacy instance cwd for
unmigrated consumers; the native long-path contract applies to embedded mode.
Set `SEEKDB_NATIVE_SQL_EXE` to the product and optionally
`SEEKDB_NATIVE_STANDBY_DAEMON=1` to exercise the default daemon. The check verifies
SQL/process/module identity, the authenticated server certificate and HTTP/2
traffic, anonymous/untrusted client rejection, and certificate renewal at the
unmodified 3600-second watcher interval. `SEEKDB_NATIVE_STANDBY_SKIP_ROTATION=1`
only runs a startup smoke and is not rotation evidence. The test terminates its
own disposable non-embedded process tree and retains logs and certificate
fingerprints; it does not claim graceful shutdown or crash recovery. Test keys
are generated locally and must not be included in exported evidence. No gRPC
vendor patch or new long standby-wallet requirement is introduced.

`sqlite-process-long-test` runs three fixed rounds for each of the original,
control and candidate DLLs. Two independent processes open the same 4092-unit
Unicode database through the production extended spelling using win32-longpath;
WAL/SHM names fit in 4096 units. File identity, competing writer rejection,
reader isolation, rollback, checkpoint and reopened content are checked.
`sqlite-process-test` retains the short-path default-VFS interoperation case.
`sqlite-process-long-interop-test` additionally uses the ordinary long spelling
in the child. That raw SQLite input succeeds under policy 1 and returned rc14
under policy 0 in all three DLLs; production adds the extended prefix before
calling SQLite. Keep this diagnostic separate from the production-path check.
The product checks record policy without changing it; record the current policy
for SQLite actions in the outer runner and serialize external policy runs.

`sqlite-input-test` verifies an offline, relocated copy of the pinned source
cache and original package inputs. Missing/corrupted archive, missing port file,
and altered feature header or license must fail with the specific diagnostic.
`sqlite-install-test` verifies relocation plus changed DLL/license and missing
installation identity. Candidate installations include the original verified
SQLite copyright notice, installed in `share/licenses/sqlite3` in the product
package. Dependency preparation and packaging must keep that notice intact.

## Build configuration and component checks

Use the repository entry with an existing supported Windows toolchain and the
verified SQLite candidate installation. `OB_SQLITE_DIR` must identify matching
headers, import library, DLL and installation identity; the general vcpkg root
must not overwrite this selection. The compiler, SDK, CRT and compatibility
macros come from the product CMake interfaces.

```powershell
.\build.ps1 native-startup -DOB_SQLITE_DIR=<candidate-install> `
    -DOB_BUILD_WINDOWS_NATIVE_PRODUCT=ON `
    -DOB_BUILD_WINDOWS_LOG_LIFECYCLE=ON `
    -DOB_BUILD_WINDOWS_SQLITE_POOL=ON `
    -DOB_BUILD_WINDOWS_DATA_VERSION=ON `
    -DOB_BUILD_WINDOWS_ROUTER=ON `
    -DOB_BUILD_WINDOWS_BLOCK_FILE=ON `
    -DOB_BUILD_WINDOWS_TELEMETRY=ON -j 2
.\build.ps1 native-startup-contract
```

`native-startup` uses `build_phase0_nio`, builds the requested product/component
objects, and runs the enabled log, SQLite pool, data-version, router, block-file
and telemetry checks. The options default to OFF on a new configuration; a
successful build without an enabled test is not evidence that the test ran.
The startup-context executable is run separately by `native-startup-contract`.
`native-rebuild` reuses the existing configuration for incremental product work.

Component checks are selected only when affected by a retained change.
The component tests exercise the production implementations: log rotation and
compression, SQLite connection-pool concurrency and handle measurements,
data-version history and failure recovery, owned router paths, PALF directory
operations and block-file failure handling, and telemetry instance identity.

`build.ps1 native-palf-directory -j 2` builds and runs the production PALF
directory/block-file test in the existing native-startup graph. Its disposable
directory ACL permits read-only opens and child-file creation but denies the
write access required by directory flushing. PALF must reject that directory
before calling the removal pool; restoring the ACL must allow removal and flush.
The original ACL is restored on test failure as well as success.
The test temporarily disables backup/restore privileges in its own process so
an elevated VM runner cannot bypass the ACL, and verifies the raw write-open
denial before exercising the production removal caller.

`build.ps1 native-telemetry-https -j 2` reuses the telemetry target and starts
two loopback HTTPS servers with disposable CAs. The test supplies its own CA
bundle to libcurl without overriding the production peer/hostname checks or
changing the system trust store. Generated CRLs are served on loopback so
Schannel's revocation checks stay enabled. It checks untrusted-CA and wrong-host rejection,
HTTP 503 preserving the pending state, trusted 2xx persisting `sent=true`, stable
identity, and no resend. Only the trusted server may receive the payload; the
result records request counts and hashes, not payload contents or private keys.
Logs and `result.json` are under the printed `TELEMETRY_HTTPS_ROOT`. These are
transport/state tests against local peers, not permission to contact the public
telemetry endpoint. The separate `native-telemetry` entry keeps sending disabled
while checking long-path state-file ownership and replacement failures.
They do not replace CLI SQL/restart. Additional storage compaction/recovery
scenarios are not required for SEEK-533.

Other repository entries retain their established names:

| Entry | Coverage |
| --- | --- |
| `native-path` | Production Windows file-path helper, file boundaries and fixture |
| `native-sqlite-pool` | Production connection pool, boundary paths, rejection and resource observations |
| `native-log-lifecycle` | Production log-file lifecycle |
| `native-slog-reader` | Existing configured production slog-reader test |
| `phase0-nio` | Actual Rust library and C++ driver: owned run paths, discovery, pipe greeting and teardown |
| `phase0-nio-abi` | Pinned old/new object combinations; requires `OB_PHASE0_NIO_BASELINE_ROOT` |
| `phase0` / `phase0-context` | Earlier isolated file/SQLite and startup-context feasibility probes |

The NIO legacy entry returns EABI before allocating resources. Production uses
`nio_start_v27`; C, C++ and Rust objects must be built and rolled back together.
Component pipe greetings are not real SQL/storage acceptance. gRPC watcher and
standby TLS changes are outside this task; they are not required by these checks.

## Running and interpreting the matrix

Use the VM client and authorization supplied by the current task. Wait for each
upload to finish, verify the remote SHA-256, then start commands that use it.
The client does not guarantee submission-order execution of overlapping calls.
Do not transfer Git metadata, credentials, private test keys or unrelated files.

For policy tests, record the original LongPathsEnabled value, run fresh processes
serially under 0 and 1, and restore the original value in a `finally` block.
Do not run concurrent policy-changing wrappers. For every case retain its native
exit code, result JSON, input and loaded-module hashes, and restoration result.
An outer wrapper exit or restoration marker cannot override failed child tests.

Final acceptance is the native single-instance CLI flow: initialize a missing
long base directory, write/read committed SQL data, exit, restart and read/write
again without aliases. Record actual log, redo and sstable paths crossing 260
units. Existing boundary/encoding/policy evidence remains tied to its tested
inputs; after source changes select a proportionate short control and long
Unicode run on the final product. Do not add optional instance-isolation,
service, TLS, package or extra SSTable scenarios as new acceptance gates.
Retained ABI, SQLite and path-boundary changes require their focused checks
when those inputs change.

Run only against new test-owned roots. Preserve failed instances and evidence.
Record ordinary versus extended SQLite spellings and file identity separately;
raw ordinary long-path observations must not be substituted for the product's
selected extended representation. A probe manifest is not the product manifest.
Results must be tied to the actual tested EXE, DLL, source and configuration.

Linux uses `./build.sh release --make` and `./build.sh native-cli-sql` to check the
existing cwd and Unix-socket behavior, committed data and restart. The latter
requires the existing PyMySQL dependency and permission to create the sockets
used by server startup. `./build.sh native-cli-mysqltest` additionally runs the
repository's unchanged `big_trans_with_mutil_redo` and `ms_lose_rollback` cases
through the dependency-provided mysqltest binary on that isolated instance.
It compares the checked-in results, disables client defaults, and records each
case's exit code and input hashes. It does not run the full OBD/Farm suite.
Windows component results do not cover Linux, macOS,
bindings wheels, an MSI, or the current-commit regression pipeline.
