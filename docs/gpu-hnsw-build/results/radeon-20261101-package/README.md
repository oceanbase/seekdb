# 2026-11-01 真实 TGZ 包验收

`package-status.json` 为最终通过状态；`package-sql.json` 包含六轮 CPU/GPU、逐查询召回、DML/过滤及隐藏 GPU 重启结果。完整编译的 9,408 个原始跟踪文件哈希在 `source-sha256.json`，构建至验收结束逐项不变，见 `final-provenance.json`。

编译提交为 cfae4ba92b14d3010f7c7aa6a87d064dfd7c0075。包数据提交 f18737e699ad0b29e38759fa821fec79e3940545 仅增加上游时区原件，未重编 C++；来源及 13 个恢复文件哈希见 restored-package-provenance.json。最终文档和 ZIP 的后续提交不记作再次完整编译。

首次 CPack 缺失时区数据的原始错误单独保留。首次解包性能验证因 TGZ 默认 DBA_WARN 没有 INFO GPU 标记而停止，日志以 first-log-level-attempt 开头。最终验收显式设置 syslog_level=INFO，必须采集 complete gpu=true fallback=false，未降低路径检查或 Recall 门槛。

TGZ 文件本身保存在本次实例专用目录，哈希与大小在 package-status.json；材料 ZIP 不内嵌 303 MiB 主程序运行包。完整服务日志压缩备份保留在工作区 completed-evidence.tar.gz，公开材料保留相关 GPU 事件和各步骤日志。

replay-scripts 为本轮实际脚本，绑定本轮 ROOT、源码 SHA、端口和状态文件。复用时先调整隔离目录，并按报告准备锁定工具链和依赖；不要操作其他数据库。验收未执行系统安装或包安装钩子，完成后专用端口已关闭。

最终交付另合并 5e35ceac 的 Farm/obtest 入口更新，11 项 compile-entry 和 6 项 obtest-copy Linux 回归单独执行（见 test_hipvs_*.py.log）。它们不改变本轮 C++/GPU/CMake 源码，编译 SHA 和 TGZ 包 SHA 的记录保持原样。
