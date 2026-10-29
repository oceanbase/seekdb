# 运行 mysqltest

`tools/deploy/mysqltest.py` 直接启动 seekdb 并执行 SQL 回归测试。本地开发与 CI 使用同一个 runner。脚本在 Linux 和 macOS 上管理独立的临时实例。

## 准备二进制

按照[编译运行](build-and-run.md)准备依赖并编译 seekdb：

```bash
./build.py init
./build.py build seekdb
```

默认使用 `build/bin/src/observer/seekdb`、`deps/3rd/u01/obclient/bin/obclient` 和 `deps/3rd/u01/obclient/bin/mysqltest`。使用其他构建或客户端安装时，可以通过 `--seekdb`、`--obclient` 和 `--mysqltest` 覆盖这些路径。

## 选择并运行用例

在仓库根目录执行命令。先列出用例，不启动实例：

```bash
python3 tools/deploy/mysqltest.py list --suite expr
```

使用完整的 `suite.case` 名称运行单个用例：

```bash
python3 tools/deploy/mysqltest.py run \
  --base-dir "$PWD/build/mysqltest-instance" \
  --work-dir "$PWD/build/mysqltest-results" \
  --port 2881 --test-set expr.expr_ceil
```

请选择空闲的本地端口。`--base-dir` 必须是专用的临时目录：runner 会在运行前和结束后销毁受管理的实例，并拒绝接管它不管理的非空目录。需要保留数据的实例请使用[编译运行](build-and-run.md)中的 `sdb.py`。

把 `--test-set expr.expr_ceil` 换成 `--suite expr` 即可运行整个 suite；省略这两个选项则运行全部启用的用例。两个选项都支持逗号分隔及重复传入，同时指定时在选定 suite 内选择 case。名称无歧义时也可使用不带 suite 的 case 名称，suite 内的用例推荐使用 `suite.case`。顶层用例（例如 `minitest`）直接使用名称。

默认跳过 `tools/deploy/mysqltest_config.yaml` 排除列表中的用例。复现被禁用的用例时加上 `--include-excluded`。名称不存在或有歧义时，脚本会在启动或删除实例前报错。

## 录制预期结果

新增 `.test` 文件后，可以录制对应的 `.result` 文件：

```bash
python3 tools/deploy/mysqltest.py run \
  --base-dir "$PWD/build/mysqltest-instance" \
  --work-dir "$PWD/build/mysqltest-results" \
  --port 2881 --test-set expr.expr_ceil --record
```

`--record` 要求显式选择 case 或 suite，并写入源码目录中的预期结果文件；此模式允许结果文件尚不存在。检查生成的 diff 后，去掉 `--record` 再运行一次，验证预期结果。

## 输出与 CI 分片

runner 会初始化测试数据库和用户，再调用 mysqltest 二进制。用例失败或基础设施步骤失败时，返回非零退出码。结果写入 `--work-dir/seekdb_result.json`，客户端日志和失败诊断保存在同一个工作目录中。实例数据在运行结束后删除。

| 选项 | 含义 |
| --- | --- |
| `--suite`、`--test-set` | 选择 suite 或 case；省略时运行全部启用的用例。 |
| `--record` | 为显式选择的用例写入预期结果。 |
| `--include-excluded` | 包含共享配置中禁用的用例。 |
| `--parameter key=value` | 每次启动或重试时传入服务端参数，可重复指定。 |
| `--slice-index`、`--slice-count` | CI 分片的零起始索引和总数，默认分别为 `0` 和 `1`。 |

CI 继续使用 `.github/script/seekdb/mysqltest_slice.sh` 调用同一个 runner。合并分片结果所需的参数见 `python3 tools/deploy/mysqltest.py merge --help`。
