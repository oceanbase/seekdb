# JSON 协议自测工具

Python 3.9+，无需 pip 依赖。Docker 测试需本机 Docker 权限。
本目录随题包提供，不依赖组织方评测仓库，不包含隐藏测试、基线、评分、平台凭据或回执接口。
完整协议见根目录 `STDIO_PROTOCOL.md`。

## 第一步：只检查启动和关闭

在题包根目录执行，替换为自己的已构建镜像：

```bash
export VECTORDB_IMAGE='你的镜像名:标签'
python3 protocol_check/run.py --image "$VECTORDB_IMAGE" \
  --handshake-only --output "protocol-results/handshake-$(date +%Y%m%d-%H%M%S)"
```

工具先启动容器，等待程序主动输出 ready，再发送 shutdown。它不会先发送 SQL 来诱导程序输出 ready，
因此能发现“等输入才 ready”、没有换行或没有 flush 等问题。
解析失败时显示 stdout 错误行前240字节，stderr 单独保存；不要在协议输出中打印密钥等敏感信息。

## 第二步：运行一个公开用例

```bash
python3 protocol_check/run.py --image "$VECTORDB_IMAGE" \
  --case sql_crud --output "protocol-results/crud-$(date +%Y%m%d-%H%M%S)"
```

## 第三步：运行题包中全部公开用例

```bash
python3 protocol_check/run.py --image "$VECTORDB_IMAGE" \
  --output "protocol-results/public-$(date +%Y%m%d-%H%M%S)"
```

工具读取当前题包 `mysqltest/manifest.json` 与其中列出的 test/result 文件；不拿内部同名用例替代公开用例。
每个用例独立数据卷，同一用例的 `--restart` 复用卷，`--mode ann` 改变后续请求模式。
第一个超时后停止执行剩余用例，标记 SKIPPED；清理容器、保留报告和日志。

输出目录必须是新目录。`result.json` 中 `passed=true` 才表示本次选中的检查通过；进程退出码0表示通过，1表示失败。
只检查握手通过不代表 SQL 正确；公开用例通过不代表正式29个用例或性能/Agent评审通过。

`config.json` 使用当前判题资源与基础比较参数：启动120秒、单请求60秒、普通用例600秒，
2 CPU、2 GiB、128 PID，绝对/相对浮点容差均为1e-5。正式平台配置更新时应同步题包。
本地可用 `--config` 指定配置，但放宽本地参数不能改变正式判题要求。

## 通信层骨架

`example_stdio.py` 演示序列化、flush、请求分发、错误响应和shutdown。
它**没有实现数据库**：execute总是返回NOT_IMPLEMENTED，不能用于通过SQL用例，更不是参考数据库答案。
请接入自己的恢复、SQL执行、事务回滚和持久化逻辑；stdout不输出调试日志。

可用可信本地进程演示握手：

```bash
python3 protocol_check/run.py \
  --command '["python3","protocol_check/example_stdio.py"]' \
  --handshake-only --output "protocol-results/example-$(date +%Y%m%d-%H%M%S)"
```

`--command` 不提供容器隔离，只用于自己的可信程序。需要临时数据目录时，在 argv 中使用 `{data_dir}` 占位符。
正式提交前必须再测实际Docker镜像，检查默认启动参数、依赖和只读根文件系统下的行为。

## 公开断言与实际响应的区别

`.test` 的SQL必须包装成execute JSON；`.result` 只是结果断言，不是完整通信报文。
断言 `{}` 仍要求 `ok:true`；断言包含 `error` 时要求 `ok:false` 和非空 `error.code`。
BIGINT必须保持整数精度，列顺序/行数/重复行不能丢失，向量使用float32语义。

本工具复用了当前判题器的通信和结果比较语义。工具不会帮学生把READY或纯SQL协议自动转换成JSON协议。
