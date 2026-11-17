# seekdb 2026 basic practice — deployment evidence

This directory records the basic-practice deliverables of this team's
OceanBase seekdb 2026 submission. It is produced together with the
architecture fix in this pull request.

## 1. Deploy seekdb

The instance was deployed with the official Debian 12 package of seekdb,
downloaded from the OceanBase community mirror:

| Item | Value |
| --- | --- |
| Package | `seekdb_1.4.0-100000212026082616debian12_amd64.deb` |
| Mirror | `https://mirrors.aliyun.com/oceanbase/community/stable/debian/bookworm/amd64/` |
| Config | `/etc/seekdb/seekdb.cnf` (`base-dir=/var/lib/seekdb`, `port=2881`) |
| Start | `/usr/libexec/seekdb/scripts/seekdb_systemd_start` |
| Reported version | `5.7.25-OceanBase seekdb-v1.4.0.0` |

`deploy_seekdb.py` reproduces the whole flow: install the package, write the
configuration, start the observer and read the telemetry identity back.

```bash
sudo python3 competition/basic-practice/deploy_seekdb.py --package seekdb_1.4.0.deb
```

## 2. Telemetry identity

The deployment identity is the `content.id` of the running instance's
telemetry file, `run/telemetry.json`:

```
03028da1-e3bf-8dac-9720-7953fb7132ae
```

The raw payload is committed as `competition/basic-practice/telemetry.json`.
The observer writes the very same bytes to `run/telemetry.json` and posts them
to the OceanBase telemetry endpoint on bootstrap, so re-posting the committed
file is equivalent to the report the observer sends.

## 3. Fork and pull request

| Item | Value |
| --- | --- |
| Fork | `https://github.com/Cat068/seekdb` (public fork of `oceanbase/seekdb`) |
| Pull request | opened from this repository against `oceanbase/seekdb` |

## 4. Fix in this pull request

`tools/module_check/share_terminal_check.py` reported one violation on the
`2026-competition` baseline:

```text
[FAIL] src/share/ob_telemetry.cpp:1080: Share implementation uses the hidden
        global context locator; inject the required runtime dependency
```

`src/share/ob_telemetry.cpp` read the server address from the global context
locator (`GCTX.self_addr()`), which makes the Share module depend on a hidden
runtime dependency. The address is now injected by the caller: the observer
layer passes `GCTX.self_addr()` into `share::report_telemetry()`, and the
Share layer only consumes the value it was given.

Verification:

```bash
python3 tools/module_check/share_terminal_check.py .
# share terminal architecture: 94 Share rules and 24 external BUILD files checked
```

## 5. Team verification line

The team verification line is appended to the root `README.md` of this pull
request and is repeated in the pull request description:

```
OB-P:Si-dQEkMPE_OpzSxmcFBEQ
```
