# 原生入口与环境证据（2026-11-01）

原始 .log 日志与官方 channel-rust-1.98.1.toml 按原始字节保存在 `raw-evidence.tar.gz`；下文这些文件名指压缩包内路径，哈希见 `raw-file-manifest.json`。其余 JSON、驱动和入口输出可直接读取。

基线 bb488f7cfcdf102d8d11c5f44ac7f2fd02d86cec；完整说明见 ../../ENTRY_ENVIRONMENT_AUDIT_20261101.md。

- original-compile-entry.{json,log}：原镜像 PATH、旧依赖缓存，真实入口 0.715 秒退出 5，缺 rpm2cpio/cpio。
- original-rust-init.log：原 Rust 初始化命令因缺 rustup 退出 127。
- install-system-build-prerequisites.log：本验证实例实际安装 rpm2cpio/cpio 及其官方 Ubuntu 依赖；评分机仍需满足该前提，代码仅增加清晰诊断。
- exact-dependency-initialization.{json,log}：按源码精确清单真实下载、解包成功；独立缓存，未复用或修改镜像旧缓存。
- actual-rust-bootstrap.log：helper 下载官方 rustup-init 并真实安装 1.98.1。rust-download-cache.json 记录三个预置的官方下载包，未手动安装工具链；channel-rust-1.98.1.toml 是本次核对的官方 manifest。
- rust-script-host-failure.log：安装脚本域名返回 403；默认引导改用官方静态二进制地址。
- native-entry-status.json、native-compile-entry.log、native-compile/：真实 compile.sh、build.sh init 与 GPU CMake 配置通过，刻意在主 C++ 编译前以 guard 退出 42。不是完整 compile.sh 成功或新一轮完整编译。
- test_hipvs_*.py.log、regression-status.json：11 项入口、6 项 Rust、6 项 obtest，共 23 项 Linux 回归全部通过。
- candidate-source-provenance.json：远端真实入口、回归夹具中的产品脚本和测试哈希均匹配本次候选工作树。
- verify-native-entry.py、stop-before-cpp-build.sh：实际验证驱动及 guard；允许 CMake 编译器试编译，阻止最终 seekdb 主编译。
- driver-cwd-error-*、driver-guard-error*：两次验证驱动配置错误的原始记录，分别为 cwd 错误及 guard 误拦 CMake 试编译，不作为产品缺陷。
- platform-log-interface-audit.json：公开前端未找到 failMsg 解码；历史 failMsg=8 根因仍未确认。

上轮完整 GPU/TGZ/SQL 与性能结果单独保留在 ../radeon-20261101-package/，不将本次初始化/配置验证记为重新全量构建。
