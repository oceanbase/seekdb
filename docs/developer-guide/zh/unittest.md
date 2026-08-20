# 编写与运行单元测试

seekdb 使用 [Google Test](https://google.github.io/googletest/) 编写 C++ 单元测试。测试位于 `unittest/`，并按模块组织为 `observer_tests`、`storage_tests`、`sql_tests` 等目标。

## 查找测试目标

Bazel 是权威的模块化构建和测试图。使用以下命令列出单元测试规则：

```bash
./bazel.py query 'attr(name, ".*_tests", //unittest/...)'
```

每个模块在 `unittest/<module>/BUILD.bazel` 中定义测试目标。新增测试文件时，应将它加入相应模块的 Bazel 目标，不要为单个文件创建 CMake 可执行程序。

## 编译并运行模块测试

直接运行受影响的模块目标，例如：

```bash
./bazel.py test //unittest/observer:observer_tests
./bazel.py test //unittest/storage:storage_tests
```

需要筛选输出或增加诊断信息时，可以传入标准 Bazel 测试选项。先运行覆盖修改范围的最小模块，再根据需要扩大测试范围。

## 编写测试

测试文件使用 `test_*.cpp` 命名，包含 `<gtest/gtest.h>`，并编写聚焦的 `TEST` 或 `TEST_F` 用例。沿用所属模块现有的初始化和共享 main 模式；除非目标明确要求，不要在每个测试文件中重复添加 `main()`。

```cpp
TEST(ComponentName, handles_invalid_input)
{
  ASSERT_EQ(OB_INVALID_ARGUMENT, call_with_invalid_input());
}
```

当失败后无法继续执行测试时使用 `ASSERT_*`；后续检查仍有意义时使用 `EXPECT_*`。

## CMake pretest 兼容路径

CMake 构建保留了历史 Farm 契约所需的 `pretest` 目标：

```bash
./build.sh release --init
cd build_release
make pretest
```

只有验证兼容路径时才使用该目标。新增模块测试的归属和日常本地执行应以 Bazel 为准。

Pull Request 的 CI 会运行单元测试和 mysqltest。请在 Pull Request 描述中记录实际执行的目标和用例。
