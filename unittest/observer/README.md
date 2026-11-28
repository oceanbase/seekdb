# Windows bootstrap 集成验证

测试使用编译生成的 Windows `seekdb.exe` 和 PyMySQL，实际启动数据库、执行 SQL、释放 embedded 客户端文件锁、重新启动数据库并检查已提交记录。

在仓库根目录执行：

```powershell
python -m pip install PyMySQL
python unittest/observer/test_bootstrap_lifecycle.py --binary build/bin/src/observer/seekdb/seekdb.exe --work-dir build_verification/bootstrap-lifecycle --trials 10 --cpu-affinity-mask 0xf --copy-runtime-per-trial
```

Python 需要 3.11 或更新版本。`--work-dir` 必须指向尚未创建的目录；每轮使用独立实例目录和两个独立进程。测试默认传递 `memory_budget=1G`、`log_disk_size=2G`、`mysql_port_mode=specified`，通过 `--port` 指定 TCP 端口，通过可重复的 `--parameter KEY=VALUE` 设置其他参数。`--startup-timeout` 和 `--shutdown-timeout` 的单位为秒。

可选的 `--cpu-affinity-mask` 接受十进制或 `0x` 前缀的十六进制正整数。测试在创建每个进程后立即调用 Windows `SetProcessAffinityMask`，再读取并核对生效掩码；`0xf` 允许进程使用前四个逻辑处理器。省略该参数时保持系统分配的处理器范围。请求掩码及每个进程的原始、生效、系统掩码都会写入报告。参数必须属于系统允许的处理器范围，设置失败会立即结束测试。[Windows API 说明](https://learn.microsoft.com/en-us/windows/win32/api/winbase/nf-winbase-setprocessaffinitymask)

可选的 `--copy-runtime-per-trial` 为每轮创建独立的 `runtime` 目录，使用 `shutil` 复制输入的可执行文件及其同目录全部 `*.dll`，每轮首次启动与重新打开共用该轮的 runtime 副本。测试核对复制后的可执行文件 SHA-256 与输入文件相同，并记录实际执行路径、SHA-256、复制文件清单及复制耗时。该操作保留输入文件，复制内容仅包含指定的可执行文件和 DLL；每个子进程的工作目录设置为实际可执行文件所在目录。省略该参数时，各轮使用输入的可执行文件。

测试等待本次进程的 `observer start service` 日志，首次启动还必须包含 `init_global_stat execute success` 和 bootstrap 成功日志，然后仅建立一次 SQL 连接。它检查 `SELECT 1`，创建数据库与表，提交包含中文和 emoji 的记录，并在重新启动后核对完整数据。

测试在启动前通过 Windows `LockFileEx` 持有 `<base-dir>/run/seekdb.clients` 的共享锁，并使用 `--embedded` 启动服务。关闭 SQL 连接后释放该锁，服务端取得独占锁，停止 SQL listener 并调用 `_Exit(0)`。测试确认释放共享锁前进程存活、释放后在指定期限内退出且状态为零值，并在重新打开后通过真实 SQL 核对已提交数据。报告另外记录是否观察到 `no clients remaining, exiting`：服务端通过异步日志记录该消息，退出路径直接调用 `_Exit(0)`，消息可能尚未写入文件。这项检查覆盖数据库现有的 embedded 文件锁退出机制；SDK `Close` 调用及完整的模块停止流程需要各自的测试覆盖。

每轮阶段耗时、命令、进程编号、退出状态、日志证据，以及二进制 SHA-256 保存在工作目录的 `report.json`。任何启动失败、超时、SQL 错误或数据不符都会立即结束测试，保留全部实例和日志。异常清理只使用 `TerminateProcess` 结束本测试创建且仍然运行的进程，并在报告中单独记录。清理发生异常时，测试仍会尝试关闭其他已创建的资源并保存失败报告，记录操作异常与清理异常，随后继续传播异常。
