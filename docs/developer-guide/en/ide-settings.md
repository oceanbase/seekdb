# IDE settings

This guide uses Visual Studio Code as an example. Any editor that supports the Language Server Protocol can use the same compilation database.

## Work locally or over SSH

Install the VS Code C/C++ or clangd extension. For remote development, also install Remote - SSH, connect to the build host, and open the seekdb repository on that host. Build and index paths must refer to the remote filesystem.

## Generate the compilation database

The supported Release configuration exports a compilation database automatically:

```bash
source ~/.bashrc
./build.sh release --init
```

The generated file is:

```text
build_release/compile_commands.json
```

`build.sh ccls` and `OB_BUILD_CCLS` are not supported by the compatibility CMake build.

## Configure clangd

Point clangd at the Release build directory. For VS Code, add the following argument to the clangd extension configuration:

```json
{
  "clangd.arguments": [
    "--compile-commands-dir=build_release"
  ]
}
```

Restart the language server after regenerating the build. If an editor only reads `compile_commands.json` from the repository root, configure its compilation-database path explicitly rather than committing a generated symlink or file.

## Troubleshooting

- Confirm that `build_release/compile_commands.json` exists and contains paths valid on the machine running the language server.
- Re-run `./build.sh release --init` after build options or dependency paths change.
- Exclude build output and generated dependency directories from file watching if indexing consumes excessive resources.
