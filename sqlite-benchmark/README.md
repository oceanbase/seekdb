# SQLite Benchmark

对比 SQLite 与 SeekDB 的性能基准测试。每个测试同时编译两个后端：`_sqlite` 和 `_seekdb`。

## 编译

通过项目根目录的 CMake 构建，在 build 目录下执行：

```bash
cd build_release/sqlite-benchmark
ob-make
```

产出：`speedtest1_sqlite`、`speedtest1_seekdb`、`simple_insert_sqlite`、`simple_insert_seekdb`。

## simple_insert

单表 INSERT 基准测试，支持 batch 和 prepared statement 两个正交选项。

### 表结构

```sql
CREATE TABLE t1 (id INTEGER PRIMARY KEY, name VARCHAR(256), value INTEGER, category INTEGER)
CREATE INDEX idx_category ON t1(category)
```

每条 INSERT 独立提交（无显式事务）。SQLite 默认启用 WAL + synchronous=FULL，保证不丢数据。

### 运行

```bash
./simple_insert_sqlite <num_rows> <db_path> [--batch N] [--ps]
./simple_insert_seekdb <num_rows> <db_path> [--batch N] [--ps]
```

| 参数 | 说明 |
|------|------|
| `num_rows` | 插入行数，默认 200000 |
| `db_path` | 数据库文件路径，默认 test.db |
| `--batch N` | 每条 INSERT 包含 N 行，默认 1 |
| `--ps` | 使用 prepared statement，默认 text SQL |

四种模式（`--batch` 和 `--ps` 完全正交）：

```bash
./simple_insert_sqlite 50000 /tmp/t.db                    # batch=1,  text SQL
./simple_insert_sqlite 50000 /tmp/t.db --ps               # batch=1,  PS
./simple_insert_sqlite 50000 /tmp/t.db --batch 500        # batch=500, text SQL
./simple_insert_sqlite 50000 /tmp/t.db --batch 500 --ps   # batch=500, PS
```

## speedtest1

SQLite 官方性能测试套件（speedtest1），覆盖 INSERT、SELECT、UPDATE、DELETE、CREATE INDEX、REPLACE、JOIN 等场景。SQLite 默认启用 WAL + synchronous=FULL。

### 运行

```bash
./speedtest1_sqlite [--options] <db_path>
./speedtest1_seekdb [--options] <db_path>
```

常用选项：

| 参数 | 说明 |
|------|------|
| `--size N` | 数据规模倍数，默认 100 |
| `--journal M` | journal 模式（sqlite 默认 wal） |
| `--nosync` | 关闭 synchronous（不保证持久化） |
| `--big-transactions` | 大测试用例包裹 BEGIN/END |

示例：

```bash
./speedtest1_sqlite --size 10 /tmp/st.db
./speedtest1_seekdb --size 10 /tmp/st.db
```
