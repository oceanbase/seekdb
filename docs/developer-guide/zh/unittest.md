# 编写与运行单元测试

C++ 单元测试使用 Google Test，并通过 Bazel `cc_test` 显式声明。当前仓库有 `unittest/oblib/` 下的两个分配器测试。

## 在 Linux 上编译

```bash
./build.py init
./build.py build //unittest/oblib:test_malloc_backend
./build.py build --sanity //unittest/oblib:test_jemalloc_hook
```

`build` 只编译和链接，不运行测试。jemalloc hook 测试需要 `--sanity`，以启用项目自带的 jemalloc 后端。测试二进制位于 `build/bin/` 下对应的 target 包目录。

列出仓库中的测试目标：

```bash
./build.py query 'kind(cc_test, //unittest/...)'
```

## 运行测试

```bash
./build.py test //unittest/oblib:test_malloc_backend --test_output=errors
./build.py test --sanity //unittest/oblib:test_jemalloc_hook --test_output=errors
```

## 添加测试

模块测试放在 `unittest/<module>/`，文件命名为 `test_*.cpp`，在 `BUILD.bazel` 中显式声明依赖。使用模块自身的测试接口和运行库，不依赖其他模块的私有实现。每个测试可执行文件需要一个 Google Test main。后续检查无法继续时使用 `ASSERT_*`；失败后仍可继续时使用 `EXPECT_*`。

SQL 集成测试使用独立的 [mysqltest 流程](mysqltest.md)。在 PR 描述中记录实际编译或运行的目标。
