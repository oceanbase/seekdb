# Server-dev reference

这是同源码树、同构建配置的 C++ 深度插件示例。它使用真实的
`sql/optimizer/ob_optimizer.h` 中的 `NumberingCtx`，同时演示声明额外 C
导出。它没有注册 planner hook，也不贡献候选 path 或自定义执行计划。

在启用 `SEEKDB_ENABLE_EXPERIMENTAL_PLUGINS` 的 Linux 构建目录中：

```sh
cmake --build build_plugin_overlay_verify --target seekdb_server_dev_reference -j8
```

该目标默认不加入 ALL。构建会先完成宿主 `seekdb` 链接，再使用 Rust
工具读取宿主 GNU build ID、生成契约头文件并编译模块。产物位于
`plugins/server_dev_reference/seekdb_server_dev_reference.so`（相对构建目录）。
显式 profile 的 TOML 解析需要 CMake 选中的 Python 3.11+，以及可离线
构建现有 workspace 的 Cargo 依赖。

`plugin.toml` 中新增的构建声明：

```toml
api_profile = "server-dev"
server_headers = ["sql/optimizer/ob_optimizer.h"]
exports = ["seekdb_server_dev_numbering_probe"]
```

- 声明必须位于插件根目录的 `plugin.toml`，与源码边界检查使用同一份文件。
- 插件仍编写普通 v1 manifest；不要手写 Server-dev 后缀。构建器重命名
  原入口并生成带宿主绑定的公开入口，隐藏实现入口及未声明导出。
- 编译时取得 `ob_sql` 的传递头文件路径、宏和编译选项；不会链接它的
  对象或静态库。需要宿主非 inline 实现时仍须提供显式接口表/adapter，
  当前保留 `-z defs`，不能任意解析宿主进程符号。
- 源码检查允许声明的直接私有头文件；传递依赖使用宿主编译上下文。
  这是可信 native 插件的依赖策略，不是对恶意 CMake/C++ 的安全沙箱。
- 生成的契约只适用于指定宿主链接产物。GNU build ID 不是文件签名，
  也不能单独证明独立 SDK/header/toolchain 的来源正确。

当前还没有独立安装的 Server-dev SDK、Rust 深层 adapter、Bazel profile
或其他平台支持。GIS 保持原来的 C++ 实现，无需为使用 Rust runtime 而重写。
完整目标和边界见 [Server-dev 契约](../../docs/developer-guide/zh/plugin-server-dev-contract.md)。
