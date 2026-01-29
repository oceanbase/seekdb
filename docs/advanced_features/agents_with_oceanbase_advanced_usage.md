# OceanBase + Apache Camel Cookbook (Advanced Usage)

This cookbook shows how to spin up OceanBase CE quickly with Docker and wire it into Apache Camel routes for agent-style workflows. It also includes index tuning guidance for different data scales.

## Motivation

When building AI/agent pipelines, you often need a reliable OLTP + search backend with MySQL compatibility. OceanBase fits this role, and Apache Camel helps connect data sources, orchestrate steps, and move data reliably. This document provides a focused, repeatable setup plus tuning advice for scale.

## Solution Overview

- **Docker-first setup** for OceanBase CE (4.3.5+), suitable for local dev and CI.
- **Camel integration patterns** using JDBC/SQL components.
- **Index tuning cookbook** with scale-based guidance and a quick decision checklist.

---

## Recipe 1: Install OceanBase CE with Docker (4.3.5+)

> **Scope**: Single-node, local development and testing. For production, use official deployment tooling (OBD/Kubernetes) and production-grade configs.

### Prerequisites

- Docker 20.x+
- 8 GB+ RAM recommended for local usage

### Option A: `docker run` (quick start)

```bash
# pick a CE tag >= 4.3.5
OB_VERSION=4.3.5-lts

# local data directories
mkdir -p ./ob-data ./ob-cluster

# run OceanBase CE
docker run -d --name oceanbase-ce \
  -p 2881:2881 \
  -e MODE=mini \
  -e OB_SYS_PASSWORD=SysPassw0rd! \
  -e OB_TENANT_PASSWORD=TenantPassw0rd! \
  -v "$PWD/ob-data:/root/ob" \
  -v "$PWD/ob-cluster:/root/.obd/cluster" \
  oceanbase/oceanbase-ce:${OB_VERSION}
```

Check the container status:

```bash
docker ps | grep oceanbase-ce
docker logs -f oceanbase-ce
```

Connect with a MySQL client (OceanBase MySQL mode):

```bash
# sys tenant
mysql -h127.0.0.1 -P2881 -uroot -p

# or a user tenant, for example test tenant (if created)
mysql -h127.0.0.1 -P2881 -uroot@test -p
```

### Option B: `docker compose`

```yaml
# docker-compose.yml
services:
  oceanbase:
    image: oceanbase/oceanbase-ce:4.3.5-lts
    container_name: oceanbase-ce
    environment:
      MODE: mini
      OB_SYS_PASSWORD: SysPassw0rd!
      OB_TENANT_PASSWORD: TenantPassw0rd!
    ports:
      - "2881:2881"
    volumes:
      - ./ob-data:/root/ob
      - ./ob-cluster:/root/.obd/cluster
```

Run it:

```bash
docker compose up -d
```

> **Notes**
> - Use a tag **>= 4.3.5** for OceanBase CE.
> - The `MODE=mini` profile is suitable for development only.
> - If you need different initialization parameters, refer to the official image documentation.

---

## Recipe 2: Apache Camel connection pattern (JDBC + SQL)

OceanBase is MySQL protocol-compatible, so you can use the MySQL JDBC driver with Camel.

### Camel (Java DSL) example

```java
// Requires: camel-jdbc or camel-sql, plus MySQL JDBC driver
from("direct:load")
  .setBody(constant("SELECT id, title, updated_at FROM docs WHERE updated_at > ?"))
  .to("jdbc:obDataSource")
  .to("direct:embed");
```

### DataSource configuration (Spring Boot)

```properties
spring.datasource.url=jdbc:mysql://127.0.0.1:2881/test?useSSL=false&characterEncoding=utf8
spring.datasource.username=root@test
spring.datasource.password=TenantPassw0rd!
```

> **Tip**: Use tenant-qualified user names (e.g., `root@test`) when connecting to user tenants.

---

## Recipe 3: Index tuning best practices by scale

### 1) Small datasets (<= 10M rows)

- Favor **simplicity**: only create indexes for proven query patterns.
- Avoid low-selectivity indexes (e.g., boolean columns) unless they combine with other predicates.
- Prefer composite indexes that align with the **leftmost prefix** of frequent queries.

### 2) Medium datasets (10M - 1B rows)

- Use **covering indexes** to reduce table lookups for hot read paths.
- Consolidate overlapping indexes to reduce write amplification.
- Monitor query plans and refactor indexes based on the **actual** workload.

### 3) Large datasets (>= 1B rows)

- Partition tables on the most common filter (time, tenant, region) to **prune scans**.
- Prefer **local indexes** aligned with partitions to minimize cross-partition overhead.
- Keep secondary indexes lean; too many will hurt write throughput and compaction.

### Index design checklist

- Are the most frequent filters the **leftmost** columns in composite indexes?
- Do you have **covering** indexes for latency-critical queries?
- Are there **redundant** indexes with the same leading columns?
- Is write throughput suffering due to excessive secondary indexes?
- Can partitioning reduce full scans or large range scans?

---

## References

- OceanBase CE Docker image: https://hub.docker.com/r/oceanbase/oceanbase-ce
- OceanBase documentation portal: https://www.oceanbase.com/docs
