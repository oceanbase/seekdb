# IDE 配置

本文以 Visual Studio Code 为例。其他支持 Language Server Protocol 的编辑器也可以使用同一份编译数据库。

## 本地或远程开发

安装 VS Code C/C++ 或 clangd 扩展。远程开发还需安装 Remote - SSH，连接编译主机后打开该主机上的 seekdb 仓库。构建路径和索引路径必须对应远程文件系统。

## 生成编译数据库

受支持的 Release 配置会自动导出编译数据库：

```bash
source ~/.bashrc
./build.sh release --init
```

生成文件位于：

```text
build_release/compile_commands.json
```

兼容 CMake 构建不支持 `build.sh ccls` 和 `OB_BUILD_CCLS`。

## 配置 clangd

将 clangd 指向 Release 构建目录。在 VS Code 的 clangd 扩展配置中加入：

```json
{
  "clangd.arguments": [
    "--compile-commands-dir=build_release"
  ]
}
```

重新生成构建配置后，应重启语言服务。如果编辑器只会读取仓库根目录的 `compile_commands.json`，请显式配置编译数据库路径，不要提交生成的软链接或文件。

## 常见问题

- 确认 `build_release/compile_commands.json` 存在，并且其中路径对运行语言服务的机器有效。
- 构建选项或依赖路径变化后，重新运行 `./build.sh release --init`。
- 如果索引占用资源过多，从文件监视范围中排除构建产物和生成的依赖目录。
