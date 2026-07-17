
# Get the code, build and run

## Prerequisites

Check the [Install toolchain](toolchain.md) guide for supported OS, GLIBC version requirement, and how to install the C++ toolchain.

## Clone

Clone the source code to your development machine:

```shell
git clone https://github.com/oceanbase/seekdb.git
```

## Build

Build OceanBase seekdb from the source code in debug mode or release mode:

### Debug mode

```shell
bash build.sh debug --init --make
```

### Release mode

```shell
bash build.sh release --init --make
```

## Run

Now that you built the `seekdb` binary, you can deploy a seekdb instance with the `obd.sh` utility:

```shell
./tools/deploy/obd.sh prepare -p /tmp/obtest
./tools/deploy/obd.sh deploy -c ./tools/deploy/single.yaml
```

You can check the `mysql_port` in `./tools/deploy/single.yaml` file to see the listening port. Normally, if you deploy with the root user, the seekdb server will listen on port 10000, and the examples below are also based on this port.

## Connect

You can use the official MySQL client to connect to seekdb:

```shell
mysql -uroot -h127.0.0.1 -P10000
```

Alternatively, you can use the `obclient` to connect to seekdb:

```shell
./deps/3rd/u01/obclient/bin/obclient -h127.0.0.1 -P10000 -uroot -Doceanbase -A
```

## Shutdown

You can run the following command to shut down the server and clean up the deployment, which prevents disk space consumption:

```shell
./tools/deploy/obd.sh destroy --rm -n single
```
