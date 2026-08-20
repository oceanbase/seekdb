# Install and manage seekdb with systemd

The seekdb RPM and DEB packages install a single-node server managed by systemd. Use this deployment for development, evaluation, and other non-critical single-node workloads. Back up important data and follow the [product deployment documentation](https://docs.seekdb.ai/seekdb/deploy-by-systemd/) for the current production-support policy.

## Prerequisites

- A systemd-based Linux distribution and a user that can run `sudo`
- At least 1 CPU core and 2 GiB of available memory
- `curl`, `jq`, and a MySQL-compatible client
- Enough free disk space for the configured data and redo directories

The current package documentation covers these tested families:

- RPM: Anolis OS 8/23, CentOS 7/9, and openEuler 22.03/24.03
- DEB: Debian 11/12/13 and Ubuntu 20.04/22.04/24.04

Use a package built for the target operating system and CPU architecture.

## Install on an RPM system

### Online installation

The official installer configures the current package source and installs the latest release:

```bash
curl -fsSL https://obportal.s3.ap-southeast-1.amazonaws.com/download-center/opensource/seekdb/seekdb_install.sh | sudo bash
```

### Offline installation

Download the matching RPM from the [seekdb download center](https://www.oceanbase.ai/download), copy it to the target host, and run:

```bash
sudo rpm -ivh seekdb-*.rpm
```

## Install on a Debian or Ubuntu system

### Online installation

Install the tools used to identify the distribution, add the seekdb repository, and install the package:

```bash
sudo apt-get update
sudo apt-get install -y lsb-release ca-certificates curl jq default-mysql-client
echo "deb [trusted=yes] https://mirrors.aliyun.com/oceanbase/community/stable/$(lsb_release -is | awk '{print tolower($0)}')/$(lsb_release -cs)/$(dpkg --print-architecture)/ ./" | sudo tee /etc/apt/sources.list.d/oceanbase.list
sudo apt-get update
sudo apt-get install -y seekdb
```

This repository currently uses APT's `trusted=yes` form and does not require the deprecated `apt-key` command.

### Offline installation

Download the matching DEB from the [seekdb download center](https://www.oceanbase.ai/download), copy it to the target host, and run:

```bash
sudo dpkg -i seekdb-*.deb
```

If `dpkg` reports missing dependencies, install them from the configured operating-system repositories before retrying.

## Installed files

| Path | Purpose |
| --- | --- |
| `/usr/bin/seekdb` | seekdb server binary |
| `/usr/bin/obshell` | obshell agent, when included by the package |
| `/etc/seekdb/seekdb.cnf` | systemd startup configuration |
| `/usr/lib/systemd/system/seekdb.service` | systemd unit |
| `/usr/libexec/seekdb/scripts/` | service start, stop, and telemetry scripts |
| `/usr/share/seekdb/` | administration SQL and runtime data files |

## Configure the first start

Edit `/etc/seekdb/seekdb.cnf` before starting the service for the first time:

```ini
# Permanently selects the database directory used by the service.
base-dir=/var/lib/oceanbase

# Passed when a new database is initialized.
data-dir=/var/lib/oceanbase/store
redo-dir=/var/lib/oceanbase/store/redo
port=2881
cpu_count=4
```

The service start script always reads `base-dir`. It passes `data-dir`, `redo-dir`, `port`, `cpu_count`, and other parameter entries only while initializing a new database. After initialization, changing those entries in `seekdb.cnf` does not reconfigure the existing database; use the supported SQL configuration interface for dynamic parameters.

Changing `base-dir` points the service at a different database directory. Do not change it accidentally after data has been created.

## Manage the service

Reload systemd after installing or replacing the unit, then start seekdb:

```bash
sudo systemctl daemon-reload
sudo systemctl start seekdb
sudo systemctl status seekdb
```

The service uses `Type=notify`. A successful start reports `seekdb is ready and running`; a bootstrap failure is returned to systemd as a failed service.

Common operations are:

```bash
sudo systemctl stop seekdb
sudo systemctl restart seekdb
sudo systemctl enable seekdb
sudo systemctl disable seekdb
```

Inspect startup and service errors with:

```bash
sudo journalctl -u seekdb --since today
sudo journalctl -u seekdb -b --no-pager
```

Server logs are under `<base-dir>/log/`; with the packaged default, the main log is `/var/lib/oceanbase/log/seekdb.log`.

## Uninstall

Stop the service and remove the package with the matching package manager:

```bash
sudo systemctl stop seekdb
sudo yum erase seekdb        # RPM systems
sudo apt-get remove seekdb   # Debian/Ubuntu systems
```

Package removal preserves database data. The uninstall script may create `/var/lib/seekdb/seekdb_clean.sh` for optional cleanup.

> **Danger:** The following command permanently deletes seekdb data and cannot be undone. Review the generated script and confirm every target path before running it.

```bash
sudo bash /var/lib/seekdb/seekdb_clean.sh
```
