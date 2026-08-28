# 编写与运行单元测试

seekdb 使用 [Google Test](https://google.github.io/googletest/) 编写 C++ 单元测试。测试位于 `unittest/`，并按 CMake 模块组织为 `observer_tests`、`storage_tests`、`sql_tests` 等目标。

## 配置测试构建

CMake 单元测试目标仅在 Linux 上提供。编译模块测试前，先初始化依赖并配置 Release 构建：

```bash
./build.sh release --init
```

`--init` 通常只在首次构建或依赖定义变化后使用。后续需要重新配置时，执行不带 `--init` 的 `./build.sh release`。

## 选择并编译模块

使用以下命令列出可用的模块目标：

```bash
make -C build_release help | grep '_tests'
```

编译受修改影响的模块，例如：

```bash
make -C build_release observer_tests
make -C build_release storage_tests
```

每个测试二进制位于 `build_release/unittest/<module>/<module>_tests`。

## 运行模块测试

配置阶段生成的 `run_tests.sh` 会通过 CTest 设置测试所需的环境和工作目录。使用正则表达式运行一个模块的全部分片，例如：

```bash
./build_release/unittest/run_tests.sh \
  -R '^observer_tests_shard_[0-9]+$' \
  --output-on-failure
```

只运行一个 Google Test 用例时，在保留模块分片筛选的同时设置 `GTEST_FILTER`：

```bash
GTEST_FILTER='ComponentName.handles_invalid_input' \
  ./build_release/unittest/run_tests.sh \
  -R '^observer_tests_shard_[0-9]+$' \
  --output-on-failure
```

先运行覆盖修改范围的最小模块和用例，再根据需要扩大测试范围。

## 编写测试

测试文件使用 `test_*.cpp` 命名，放在对应的 `unittest/<module>/` 目录下，包含 `<gtest/gtest.h>`，并编写聚焦的 `TEST` 或 `TEST_F` 用例。CMake 会自动发现模块中的 C++ 测试源文件。沿用模块的共享 main 模式，不要单独添加 `main()`。

```cpp
TEST(ComponentName, handles_invalid_input)
{
  ASSERT_EQ(OB_INVALID_ARGUMENT, call_with_invalid_input());
}
```

当失败后无法继续执行测试时使用 `ASSERT_*`；后续检查仍有意义时使用 `EXPECT_*`。

## 编译并运行全部模块

`pretest` 目标会编译全部 CMake 单元测试模块。编译完成后，再单独运行已注册的 CTest 分片：

```bash
make -C build_release pretest
./build_release/unittest/run_tests.sh --output-on-failure
```

适合并行执行时，可以向 `run_tests.sh` 传入 `-j <jobs>`。Pull Request 的 CI 也会运行单元测试和 mysqltest。请在 Pull Request 描述中记录实际执行的目标和筛选条件。
