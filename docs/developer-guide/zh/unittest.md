# 编写与运行单元测试

本文档介绍如何在 OceanBase SeekDB 项目中编写和运行单元测试。

## 概述

SeekDB 使用 [Google Test](https://github.com/google/googletest) 作为单元测试框架。单元测试代码位于项目根目录下的 `unittest` 目录中。

## 相关文档

- [编译与运行](build-and-run.md) - 编译项目的基础
- [编程惯例](coding-convention.md) - 了解 SeekDB 的编程风格
- [调试方法](debug.md) - 调试测试代码

## 编译及运行所有单元测试

SeekDB 有两个单元测试目录：

- `unittest`：主要的单元测试用例，测试 `src` 目录中的代码
- `deps/oblib/unittest`：oblib 库的测试用例

### 编译单元测试

默认情况下，构建 SeekDB 项目时不会自动编译单元测试。需要显式编译：

```bash
# 1. 首先编译项目（Debug 模式）
bash build.sh debug --init --make

# 2. 进入构建目录的 unittest 目录
cd build_debug/unittest
# 或者编译 oblib 的测试
# cd build_debug/deps/oblib/unittest

# 3. 编译单元测试
make -j4
```

### 运行所有测试

编译完成后，可以运行 `run_tests.sh` 脚本来运行所有测试用例：

```bash
./run_tests.sh
```

## 编译并运行单个测试

可以编译并运行单个测试用例，这对于调试特定的测试很有用。

### 步骤

1. **进入构建目录**（不要进入 `unittest` 目录）：

```bash
cd build_debug
```

2. **编译特定测试用例**：

```bash
make -j4 test_chunk_row_store
```

3. **查找并运行测试二进制文件**：

```bash
# 查找测试文件位置
find . -name "test_chunk_row_store"

# 运行测试（示例路径）
./unittest/sql/engine/basic/test_chunk_row_store
```

> **注意**：必须在 `build_debug` 目录下执行 `make` 命令，而不是在 `unittest` 目录下。

## 编写单元测试

### 测试文件命名

SeekDB 使用 `test_xxx.cpp` 作为单元测试文件名。创建测试文件后，需要将文件名添加到对应的 `CMakeLists.txt` 文件中。

### 测试文件模板

每个测试文件需要包含以下基本结构：

1. **包含 Google Test 头文件**：

```cpp
#include <gtest/gtest.h>
```

2. **添加 main 函数**：

```cpp
int main(int argc, char **argv)
{
  testing::InitGoogleTest(&argc, argv);
  return RUN_ALL_TESTS();
}
```

接着可以添加一些函数来测试不同的场景。下面是 `test_ra_row_store_projector.cpp` 的一个例子。

```cpp
///
/// TEST 是 google test 的一个宏。
/// 用来添加一个新的测试函数。
///
/// RARowStore 是一个测试套件的名字，alloc_project_fail 是测试的名字。
///
TEST(RARowStore, alloc_project_fail)
{
  ObEmptyAlloc alloc;
  ObRARowStore rs(&alloc, true);

  ///
  /// ASSERT_XXX 是一些测试宏，可以帮助我们判断结果是否符合预期，如果失败会终止测试。
  ///
  /// 还有一些其它的测试宏，以 `EXPECT_` 开头，如果失败不会终止测试。
  ///
  ASSERT_EQ(OB_SUCCESS, rs.init(100 << 20));
  const int64_t OBJ_CNT = 3;
  ObObj objs[OBJ_CNT];
  ObNewRow r;
  r.cells_ = objs;
  r.count_ = OBJ_CNT;
  int64_t val = 0;
  for (int64_t i = 0; i < OBJ_CNT; i++) {
    objs[i].set_int(val);
    val++;
  }

  int32_t projector[] = {0, 2};
  r.projector_ = projector;
  r.projector_size_ = ARRAYSIZEOF(projector);

  ASSERT_EQ(OB_ALLOCATE_MEMORY_FAILED, rs.add_row(r));
}
```

### 更多信息

- [Google Test 文档](https://google.github.io/googletest/) - 了解 `TEST`、`ASSERT` 和 `EXPECT` 等宏的详细用法
- [编程惯例](coding-convention.md) - 了解 SeekDB 的编程风格

## CI 中的单元测试

在合并拉取请求之前，GitHub CI 会自动运行测试。`Farm` 将测试 `mysqltest` 和 `unittest`。您可以查看下面的 `Details` 链接查看详细的测试结果。

### 查看测试结果

在 Pull Request 页面，可以看到 CI 测试的状态。点击 `Details` 链接可以查看：

- 测试执行情况
- 失败的测试用例
- 测试覆盖率等信息

![github ci](images/unittest-github-ci.png)

![github ci farm 详情](images/unittest-ci-details.png)

![Farm unittest](images/unittest-unittest.png)
